"""
IoT Lab voice server
--------------------
Runs on your laptop. The ESP32 gadget records a few seconds of audio and
POSTs it here; Whisper turns it into text and we work out the command.

  POST /listen   body = raw 16-bit little-endian mono PCM at 16 kHz
                 reply = {"text": ..., "action": ..., "seconds": ..., "reply": ..., "say": "/say/N"}
  GET  /say/N    the reply spoken by the Windows voice: raw 16-bit mono PCM at 22050 Hz
  GET  /ping     reply = {"ok": true}

Everything runs locally: no audio leaves this computer.
Start it with:  .venv\\Scripts\\python server.py
"""
import json
import os
import re
import subprocess
import tempfile
import threading
import time
import wave
from difflib import SequenceMatcher
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import numpy as np
from faster_whisper import WhisperModel

import spotify

PORT = int(os.environ.get("VOICE_PORT", "5005"))
# Tested on real gadget recordings: "small" with Hindi got ~13 of 18 phrases right in ~3 s;
# "base" got ~3 (and auto-detect doubles the time). "base" is faster if accuracy matters less.
MODEL = os.environ.get("VOICE_MODEL", "small")
# Commands are short English words ("play", "next", "volume up"), so listen in English:
# singer names then come out in Roman letters, which Spotify search likes best.
# "hi" = Hindi in Devanagari; empty = detect each time (twice as slow, Hindi often comes out as Urdu).
LANGUAGE = os.environ.get("VOICE_LANGUAGE", "en") or None
SAMPLE_RATE = 16000
SPEAK_RATE = 22050       # the gadget's speaker runs at this rate (AUDIO_SAMPLE_RATE)
VOICE_NAME = os.environ.get("VOICE_SPEAKER", "Microsoft Zira Desktop")

# ---------------- Understanding the command ----------------

NUMBER_WORDS = {
    # Hindi (Devanagari)
    "एक": 1, "दो": 2, "तीन": 3, "चार": 4, "पांच": 5, "पाँच": 5, "छह": 6, "छः": 6, "सात": 7,
    "आठ": 8, "नौ": 9, "दस": 10, "ग्यारह": 11, "बारह": 12, "पंद्रह": 15, "बीस": 20,
    "पच्चीस": 25, "तीस": 30, "आधा": 0.5,
    # English numbers as Hindi mode writes them
    "वन": 1, "टू": 2, "थ्री": 3, "फोर": 4, "फाइव": 5, "सिक्स": 6, "सेवन": 7, "एट": 8, "नाइन": 9,
    "टेन": 10, "फिफ्टीन": 15, "ट्वेंटी": 20, "थर्टी": 30,
    # Hindi (Latin) and English
    "ek": 1, "do": 2, "teen": 3, "char": 4, "chaar": 4, "paanch": 5, "panch": 5, "chhe": 6,
    "saat": 7, "aath": 8, "nau": 9, "das": 10, "pandrah": 15, "bees": 20, "tees": 30,
    "one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6, "seven": 7, "eight": 8,
    "nine": 9, "ten": 10, "fifteen": 15, "twenty": 20, "thirty": 30,
}

# Spelling varies ("मोसम", "प्यानो", "खल्कुलेटर"; in Roman letters "Moosam ke saai"),
# so longer keywords also match similar spellings (see matches()).
INTENTS = [  # checked in this order
    ("stop",       ["बंद", "बन्द", "स्टॉप", "रोको", "band", "bandh", "stop", "off"]),
    ("timer",      ["टाइमर", "टायमर", "टाईमर", "timer", "taimar"]),
    ("radio",      ["रेडियो", "रेडिओ", "गाना", "गाने", "संगीत", "म्यूजिक", "म्यूज़िक", "एफएम",
                    "radio", "redio", "gaana", "gana", "gane", "gaane", "music", "fm"]),
    ("weather",    ["मौसम", "मोसम", "वेदर", "तापमान", "mausam", "mosam", "mousam", "moosam", "mausum",
                    "weather", "temperature"]),
    ("time",       ["समय", "टाइम", "बजे", "घड़ी", "time", "samay", "baje", "bje", "bajhe", "bhaje",
                    "baaje", "clock", "ghadi"]),
    ("calculator", ["कैलकुलेटर", "कल्कुलेटर", "हिसाब", "calculator", "hisab"]),
    ("piano",      ["पियानो", "प्यानो", "piano", "pyano"]),
    ("menu",       ["मेन्यू", "मेनू", "होम", "वापस", "menu", "home", "wapas"]),
]
ALL_KEYWORDS = {w for _, words in INTENTS for w in words}
TIME_UNIT = re.compile(r"मिनट|सेकंड|\bmin|minute|second|\bsec")

# Shown on the gadget screen and spoken through its speaker
REPLIES = {
    "stop": "Stopping",
    "radio": "Playing the radio",
    "weather": "Here is the weather",
    "time": "Here is the time",
    "calculator": "Opening the calculator",
    "piano": "Opening the piano",
    "menu": "Going home",
    "unknown": "Sorry, I did not understand",
    "nothing": "I heard nothing",
}


def to_ascii_digits(text):
    return text.translate(str.maketrans("०१२३४५६७८९", "0123456789"))


def find_seconds(text):
    """Timer length from text like '5 minute', 'पांच मिनट', '30 second'."""
    # Split on spaces: \w would cut Hindi words at their vowel signs
    words = [w.strip(".,!?।\"'") for w in to_ascii_digits(text.lower()).split()]
    for i, w in enumerate(words):
        value = float(w) if re.fullmatch(r"\d+(\.\d+)?", w) else NUMBER_WORDS.get(w)
        if value is None:
            continue
        unit = " ".join(words[i + 1:i + 3])
        if re.search(r"सेकंड|second|sec", unit):
            return int(value)
        return int(value * 60)          # minutes by default
    return 0


def duration(secs):
    m, s = divmod(secs, 60)
    parts = []
    if m:
        parts.append(f"{m} minute" + ("s" if m > 1 else ""))
    if s:
        parts.append(f"{s} second" + ("s" if s > 1 else ""))
    return " ".join(parts)


def matches(keyword, text, latin_words, hindi_words):
    if keyword.isascii():
        words, threshold = latin_words, 0.8
        if len(keyword) <= 4:
            return keyword in words                # short words must match exactly
    else:
        if keyword in text:                        # Devanagari: substring first
            return True
        words, threshold = hindi_words, 0.75
        if len(keyword) <= 4:
            return False
    # A word that is exactly another command's keyword ("time") is not a misspelling of this one ("timer")
    return any(SequenceMatcher(None, keyword, w).ratio() >= threshold
               for w in words if w == keyword or w not in ALL_KEYWORDS)


def timer_result(text):
    secs = find_seconds(text)
    if secs <= 0:
        return {"action": "timer", "seconds": 0, "reply": "Opening the timer"}
    return {"action": "timer", "seconds": secs, "reply": f"Timer set for {duration(secs)}"}


def understand(text):
    t = text.lower()
    latin_words = re.findall(r"[a-z]+", t)
    hindi_words = [w.strip(".,!?।\"'") for w in t.split() if not w.isascii()]
    for action, words in INTENTS:
        if any(matches(w, t, latin_words, hindi_words) for w in words):
            if action == "timer":
                return timer_result(text)
            return {"action": action, "seconds": 0, "reply": REPLIES[action]}
    if TIME_UNIT.search(t) and find_seconds(text):  # just "5 minute"
        return timer_result(text)
    return {"action": "unknown", "seconds": 0, "reply": REPLIES["unknown"]}


# ---------------- Radio stations by name ----------------
# "Play Vividh Bharati", "Play news", "Play kirtan". Names must match the gadget's lists
# in esp32-iot-lab/src/radio_lists.h. Checked before Spotify, so "play news" isn't a song search.
STATIONS = [
    ("Vividh Bharati",       ["vividh bharati", "vividh bharti", "vivid bharati", "vivid bharti", "vividh",
                              "akashvani", "all india radio", "विविध भारती", "आकाशवाणी"]),
    ("Hindi News",           ["news", "khabar", "samachar", "न्यूज़", "न्यूज", "समाचार", "खबर"]),
    ("Gurbani Kirtan",       ["gurbani", "गुरबाणी", "गुरबानी"]),     # before "kirtan" below
    ("Golden Temple Kirtan", ["golden temple", "kirtan", "harmandir", "darbar sahib", "कीर्तन"]),
    ("SVBC TTD Tirupati",    ["tirupati", "balaji", "venkateswara", "ttd"]),
    ("Shaiva Lahari",        ["shiva", "shiv", "shaiva", "mahadev"]),
    ("Mahakatha Radio",      ["bhajan", "bhajans", "devotional", "bhakti", "mahakatha", "katha", "भजन"]),
    ("Radio Sai",            ["sai baba", "radio sai", "sai ram"]),
    ("AIR Alwar",            ["alwar"]),
    ("Jaipur Radio",         ["jaipur"]),
]


def find_station(text):
    t = text.lower()
    for name, words in STATIONS:
        for w in words:
            if (w in t) if not w.isascii() else re.search(r"\b" + re.escape(w) + r"\b", t):
                return {"action": "station", "station": name, "seconds": 0, "reply": f"Playing {name}"}
    return None


# ---------------- Speech to text ----------------

# Words Whisper should expect. Commands only: tested on 50 recordings, adding singer names
# here made Whisper insert them where nobody said them ("Kumar, volume down") and once
# loop for 2 minutes. Singer names are corrected afterwards instead (spotify.KNOWN_ARTISTS).
HINT_WORDS = ("play, stop, pause, next, previous, volume up, volume down, what's playing, radio, "
              "weather, time, timer, minutes, seconds, piano, calculator, Spotify, "
              "Vividh Bharati, news, kirtan, bhajan")

print(f"Loading Whisper model '{MODEL}' (first time downloads it, please wait)...")
model = WhisperModel(MODEL, device="cpu", compute_type="int8")
print("Model ready")


def transcribe(pcm_bytes):
    audio = np.frombuffer(pcm_bytes, dtype="<i2").astype(np.float32) / 32768.0
    peak = float(np.max(np.abs(audio))) if audio.size else 0.0
    if peak < 0.01:
        return "", peak
    audio *= 0.9 / peak                  # make quiet recordings loud enough
    # Short commands: one greedy pass, no temperature retries, few tokens.
    # Those retries make Whisper loop on unclear audio. Hint words, though, help a lot:
    # on real recordings they turned "अरी जी सींकर" into "अरिजीत सिंह" for ~0.7 s extra.
    options = dict(beam_size=1, temperature=0.0, without_timestamps=True,
                   condition_on_previous_text=False, max_new_tokens=40, hotwords=HINT_WORDS)
    segments, info = model.transcribe(audio, language=LANGUAGE or None, **options)
    text = " ".join(s.text.strip() for s in segments).strip()
    if LANGUAGE is None and info.language not in ("hi", "en"):
        # Hindi is often detected as Urdu (Arabic script): ask for Hindi
        segments, _ = model.transcribe(audio, language="hi", **options)
        text = " ".join(s.text.strip() for s in segments).strip()
    return text, peak


# ---------------- Spoken replies (Windows voice, offline) ----------------

speech_lock = threading.Lock()
speech_keys = {}      # text -> number used in /say/N
speech_audio = []     # number -> PCM bytes

# Text and file name are passed through environment variables, not the command line
SPEAK_SCRIPT = (
    "Add-Type -AssemblyName System.Speech;"
    "$s = New-Object System.Speech.Synthesis.SpeechSynthesizer;"
    "try { $s.SelectVoice($env:SAY_VOICE) } catch {};"
    "$f = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo("
    f"{SPEAK_RATE}, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,"
    " [System.Speech.AudioFormat.AudioChannel]::Mono);"
    "$s.SetOutputToWaveFile($env:SAY_OUT, $f); $s.Speak($env:SAY_TEXT); $s.Dispose()"
)


def synthesize(text):
    """Speaks text into raw PCM, with the silence at both ends cut off."""
    path = os.path.join(tempfile.gettempdir(), f"iotlab_say_{threading.get_ident()}.wav")
    env = dict(os.environ, SAY_TEXT=text, SAY_OUT=path, SAY_VOICE=VOICE_NAME)
    subprocess.run(["powershell", "-NoProfile", "-Command", SPEAK_SCRIPT],
                   env=env, check=True, timeout=20, creationflags=subprocess.CREATE_NO_WINDOW)
    with wave.open(path) as w:
        pcm = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2")
    os.remove(path)
    loud = np.nonzero(np.abs(pcm) > 300)[0]
    if loud.size:
        pcm = pcm[max(loud[0] - 400, 0):loud[-1] + 2000]
    return pcm.tobytes()


def speech_for(text):
    """Number of the spoken version of text, made once and then cached."""
    with speech_lock:
        if text not in speech_keys:
            speech_audio.append(synthesize(text))
            speech_keys[text] = len(speech_audio) - 1
        return speech_keys[text]


# ---------------- Saving recordings (for tuning accuracy) ----------------

SAVE_RECORDINGS = os.environ.get("VOICE_SAVE", "1") == "1"
RECORDINGS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "recordings")


def save_recording(pcm, text, action):
    """recordings/<time>.wav plus a line in recordings/log.tsv with what was heard."""
    os.makedirs(RECORDINGS_DIR, exist_ok=True)
    name = time.strftime("%Y%m%d-%H%M%S") + ".wav"
    with wave.open(os.path.join(RECORDINGS_DIR, name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(pcm)
    with open(os.path.join(RECORDINGS_DIR, "log.tsv"), "a", encoding="utf-8") as f:
        f.write(f"{name}\t{action}\t{text}\n")


# ---------------- HTTP ----------------

class Handler(BaseHTTPRequestHandler):
    def send_json(self, data, code=200):
        body = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/ping":
            self.send_json({"ok": True})
        elif self.path == "/listen-start":
            # The gadget is about to record: pause Spotify so the mic hears the command, not the song
            paused = False
            try:
                paused = spotify.duck()
            except Exception as e:
                print(f"Spotify pause failed: {e}")
            self.send_json({"ok": True, "paused": paused})
        elif re.fullmatch(r"/say/\d+", self.path):
            n = int(self.path.rsplit("/", 1)[1])
            if n >= len(speech_audio):
                self.send_json({"error": "not found"}, 404)
                return
            pcm = speech_audio[n]
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(pcm)))
            self.end_headers()
            self.wfile.write(pcm)
        else:
            self.send_json({"error": "not found"}, 404)

    def do_POST(self):
        if self.path != "/listen":
            self.send_json({"error": "not found"}, 404)
            return
        length = int(self.headers.get("Content-Length", 0))
        if not 0 < length <= SAMPLE_RATE * 2 * 10:      # up to 10 seconds
            self.send_json({"error": "bad length"}, 400)
            return
        pcm = self.rfile.read(length)
        started = time.time()
        text, peak = transcribe(pcm)
        if not text:
            result = {"action": "unknown", "seconds": 0, "reply": REPLIES["nothing"]}
        else:
            result = find_station(text)                  # "play Vividh Bharati", "play news"
            if not result:
                spotify_result = spotify.handle(text)    # "play Arijit Singh", "next"...
                result = dict(spotify_result, seconds=0) if spotify_result else understand(text)
        try:
            spotify.unduck(result.get("command") or result["action"])   # resume if paused for listening
        except Exception as e:
            print(f"Spotify resume failed: {e}")
        result["text"] = text
        try:
            result["say"] = f"/say/{speech_for(result['reply'])}"
        except Exception as e:                          # the gadget still shows the reply
            print(f"Speech failed: {e}")
        print(f"[{self.client_address[0]}] {length / 2 / SAMPLE_RATE:.1f}s audio, peak {peak:.2f}, "
              f"{time.time() - started:.1f}s -> {text!r} => {result['action']} {result['seconds'] or ''}")
        self.send_json(result)
        if SAVE_RECORDINGS:
            save_recording(pcm, text, result["action"])

    def log_message(self, *args):
        pass                              # we print our own one-line log


if __name__ == "__main__":
    print("Preparing spoken replies...")
    for reply in REPLIES.values():
        speech_for(reply)
    print("Spotify: " + ("connected" if spotify.configured() else
                         "not set up (see README: spotify.py login)"))
    print(f"Voice server listening on port {PORT} (Ctrl+C to stop)")
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
