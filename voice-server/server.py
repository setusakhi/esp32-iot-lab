"""
IoT Lab voice server
--------------------
Runs on your laptop. The ESP32 gadget records a few seconds of audio and
POSTs it here; Whisper turns it into text and we work out the command.

  POST /listen   body = 8-bit mu-law (Content-Type audio/basic) or 16-bit PCM, mono 16 kHz
                 reply = {"text", "action", "seconds", "amount", "station", "reply", "say": "/say/N"}
  GET  /say/N    the reply spoken by the Windows voice: raw 16-bit mono PCM at 22050 Hz
  GET  /market   stocks, gold & currencies, fund NAVs for the Market screen (see market.py)
  GET  /cricket  cricket scores for the Cricket screen (see cricket.py)
  GET  /ping     reply = {"ok": true}

Questions that are not gadget commands go to the AI buddy (assistant.py, a local model in Ollama).

Everything runs locally: no audio leaves this computer (the weather comes from Open-Meteo,
prices from Yahoo Finance and mfapi.in, cricket scores from Cricbuzz).
Start it with:  start-server.bat
"""
import ast
import json
import operator
import os
import re
import subprocess
import tempfile
import threading
import time
import wave
from datetime import datetime
from difflib import SequenceMatcher
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import numpy as np
import requests
from faster_whisper import WhisperModel

import assistant
import cricket
import market

PORT = int(os.environ.get("VOICE_PORT", "5005"))
# Tested on real gadget recordings: "small" understood ~12 of 16 phrases, "base" ~3.
MODEL = os.environ.get("VOICE_MODEL", "small")
# Commands are short English words ("next station", "volume up"), so listen in English.
# "hi" = Hindi in Devanagari; empty = detect each time (twice as slow, Hindi often comes out as Urdu).
LANGUAGE = os.environ.get("VOICE_LANGUAGE", "en") or None
SAMPLE_RATE = 16000
SPEAK_RATE = 22050       # the gadget's speaker runs at this rate (AUDIO_SAMPLE_RATE)
VOICE_NAME = os.environ.get("VOICE_SPEAKER", "Microsoft Zira Desktop")
# Weather for "what's the weather" (same place as the gadget's clock screen)
CITY = os.environ.get("VOICE_CITY", "Jaipur")
CITY_LAT = float(os.environ.get("VOICE_LAT", "26.91"))
CITY_LON = float(os.environ.get("VOICE_LON", "75.79"))

# ---------------- Gadget screens ----------------

NUMBER_WORDS = {
    "one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6, "seven": 7, "eight": 8,
    "nine": 9, "ten": 10, "fifteen": 15, "twenty": 20, "thirty": 30, "a": 1, "an": 1,
    "half": 0.5,
    # Hindi, in case it comes through
    "ek": 1, "do": 2, "teen": 3, "char": 4, "paanch": 5, "panch": 5, "das": 10,
    "एक": 1, "दो": 2, "तीन": 3, "चार": 4, "पांच": 5, "पाँच": 5, "दस": 10,
}

# Longer keywords also match similar spellings (see matches()), as Whisper's spelling varies
INTENTS = [  # checked in this order
    ("stop",       ["stop", "off", "pause", "band", "bandh", "बंद"]),
    ("timer",      ["timer", "taimar", "टाइमर"]),
    ("calculator", ["calculator", "कैलकुलेटर"]),
    ("piano",      ["piano", "pyano", "पियानो"]),           # before radio: "play piano"
    ("menu",       ["menu", "home", "होम"]),
    ("radio",      ["radio", "redio", "music", "fm", "gaana", "gana", "play", "रेडियो"]),
]
ALL_KEYWORDS = {w for _, words in INTENTS for w in words}
TIME_UNIT = re.compile(r"\bmin|minute|second|\bsec|मिनट")
QUESTION = re.compile(r"(who|what|why|how|when|where|which|whose|tell me|explain|"
                      r"is|are|can|could|do|does|did|should|will)\b")

REPLIES = {  # shown on the gadget and spoken; made once at start
    "stop": "Stopping",
    "radio": "Playing the radio",
    "calculator": "Opening the calculator",
    "piano": "Opening the piano",
    "menu": "Going home",
    "radio_next": "Next station",
    "radio_prev": "Previous station",
    "volume_up": "Volume up",
    "volume_down": "Volume down",
    "timer_cancel": "Timer stopped",
    "unknown": "Sorry, I did not understand",
    "nothing": "I heard nothing",
}


def find_seconds(text):
    """Length from text like '5 minutes', 'a minute', '30 seconds' (minutes if no unit)."""
    words = [w.strip(".,!?\"'") for w in text.lower().split()]
    for i, w in enumerate(words):
        value = float(w) if re.fullmatch(r"\d+(\.\d+)?", w) else NUMBER_WORDS.get(w)
        if value is None:
            continue
        unit = " ".join(words[i + 1:i + 3])
        if not TIME_UNIT.search(unit) and w in ("a", "an"):
            continue                    # "a" only counts as a number before a unit
        if re.search(r"second|sec", unit):
            return int(value)
        return int(value * 60)
    return 0


def duration(secs):
    m, s = divmod(secs, 60)
    parts = []
    if m:
        parts.append(f"{m} minute" + ("s" if m > 1 else ""))
    if s:
        parts.append(f"{s} second" + ("s" if s > 1 else ""))
    return " ".join(parts)


def matches(keyword, text, words):
    if not keyword.isascii():
        return keyword in text
    if len(keyword) <= 4:
        return keyword in words                    # short words must match exactly
    # A word that is exactly another command's keyword is not a misspelling of this one
    return any(SequenceMatcher(None, keyword, w).ratio() >= 0.8
               for w in words if w == keyword or w not in ALL_KEYWORDS)


def has(text, *phrases):
    return any(re.search(r"\b" + re.escape(p) + r"\b", text) for p in phrases)


def reply(action, text=None, **extra):
    return dict({"action": action, "seconds": 0, "reply": text or REPLIES[action]}, **extra)


# ---------------- Radio stations by name ----------------
# Names must match the gadget's lists in esp32-iot-lab/src/radio_lists.h.
STATIONS = [
    ("Vividh Bharati",       ["vividh bharati", "vividh bharti", "vivid bharati", "vivid bharti", "vividh",
                              "akashvani", "all india radio"]),
    ("Hindi News",           ["news", "khabar", "samachar"]),
    ("Gurbani Kirtan",       ["gurbani"]),                          # before "kirtan" below
    ("Golden Temple Kirtan", ["golden temple", "kirtan", "harmandir", "darbar sahib"]),
    ("SVBC TTD Tirupati",    ["tirupati", "balaji", "venkateswara", "ttd"]),
    ("Shaiva Lahari",        ["shiva", "shiv", "shaiva", "mahadev"]),
    ("Mahakatha Radio",      ["bhajan", "bhajans", "devotional", "bhakti", "mahakatha", "katha"]),
    ("Radio Sai",            ["sai baba", "radio sai", "sai ram"]),
    ("AIR Alwar",            ["alwar"]),
    ("Jaipur Radio",         ["jaipur radio", "jaipur"]),
]


def find_station(t):
    for name, words in STATIONS:
        if has(t, *words):
            return reply("station", f"Playing {name}", station=name)
    return None


# ---------------- Spoken answers: time, date, weather ----------------

def time_answer():
    return f"It's {datetime.now().strftime('%I:%M %p').lstrip('0')}"


def date_answer():
    now = datetime.now()
    return f"Today is {now.strftime('%A')}, {now.day} {now.strftime('%B')}"


WEATHER_TEXT = {0: "clear sky", 1: "mostly clear", 2: "partly cloudy", 3: "cloudy", 45: "foggy",
                48: "foggy", 51: "light drizzle", 53: "drizzle", 55: "heavy drizzle", 61: "light rain",
                63: "rain", 65: "heavy rain", 71: "light snow", 73: "snow", 75: "heavy snow",
                80: "rain showers", 81: "rain showers", 82: "heavy showers", 95: "a thunderstorm",
                96: "a thunderstorm with hail", 99: "a thunderstorm with hail"}
_weather_cache = {"at": 0, "text": None}


def weather_answer():
    if time.time() - _weather_cache["at"] < 600 and _weather_cache["text"]:
        return _weather_cache["text"]
    try:
        r = requests.get("https://api.open-meteo.com/v1/forecast", timeout=8, params={
            "latitude": CITY_LAT, "longitude": CITY_LON, "timezone": "auto",
            "current": "temperature_2m,relative_humidity_2m,weather_code"})
        cur = r.json()["current"]
        sky = WEATHER_TEXT.get(cur["weather_code"], "mixed weather")
        text = (f"{CITY}: {round(cur['temperature_2m'])} degrees, {sky}, "
                f"humidity {cur['relative_humidity_2m']} percent")
        _weather_cache.update(at=time.time(), text=text)
        return text
    except (requests.RequestException, KeyError, ValueError) as e:
        print(f"Weather failed: {e}")
        return "Here is the weather"


# ---------------- Voice calculator ----------------

OPS = {ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: operator.mul,
       ast.Div: operator.truediv, ast.Pow: operator.pow}
SPOKEN_OP = {"+": "plus", "-": "minus", "*": "times", "/": "divided by"}


def _evaluate(node):
    """Safe arithmetic: numbers and + - * / ** only."""
    if isinstance(node, ast.Expression):
        return _evaluate(node.body)
    if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)):
        return node.value
    if isinstance(node, ast.BinOp) and type(node.op) in OPS:
        right = _evaluate(node.right)
        if isinstance(node.op, ast.Pow) and abs(right) > 10:
            raise ValueError("power too big")
        return OPS[type(node.op)](_evaluate(node.left), right)
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
        return -_evaluate(node.operand)
    raise ValueError("not arithmetic")


def _number(x):
    return str(int(round(x))) if abs(x - round(x)) < 1e-9 else f"{x:.2f}".rstrip("0").rstrip(".")


def math_answer(t):
    """'What is 25 times 4' -> '25 times 4 is 100', or None if it isn't a sum."""
    t = re.sub(r"(?<=\d),(?=\d{3})", "", t)                     # 1,000 -> 1000
    m = re.search(r"(\d+(?:\.\d+)?)\s*(?:%|percent)\s*of\s*(\d+(?:\.\d+)?)", t)
    if m:
        a, b = float(m.group(1)), float(m.group(2))
        return f"{_number(a)} percent of {_number(b)} is {_number(a * b / 100)}"
    m = re.search(r"square root of\s*(\d+(?:\.\d+)?)", t)
    if m:
        return f"The square root of {m.group(1)} is {_number(float(m.group(1)) ** 0.5)}"
    for words, symbol in (("multiplied by", "*"), ("divided by", "/"), ("times", "*"), ("into", "*"),
                          ("×", "*"), ("÷", "/"), (" x ", " * "), ("plus", "+"), ("minus", "-"),
                          ("over", "/"), ("squared", "** 2"), ("cubed", "** 3")):
        t = t.replace(words, f" {symbol} ")
    expr = " ".join(re.findall(r"\d+(?:\.\d+)?|\*\*|[-+*/()]", t))
    if len(re.findall(r"\d+(?:\.\d+)?", expr)) < 2 and "**" not in expr:
        return None
    try:
        value = _evaluate(ast.parse(expr, mode="eval"))
    except (ValueError, SyntaxError, ZeroDivisionError, TypeError):
        return None
    spoken = " ".join(SPOKEN_OP.get(tok, "to the power" if tok == "**" else tok) for tok in expr.split())
    return f"{spoken} is {_number(value)}"


# ---------------- Understanding the command ----------------

def understand(text):
    t = re.sub(r"\s+", " ", re.sub(r"[?!\"]", " ", text.lower())).strip().rstrip(".")
    words = re.findall(r"[a-z]+", t)

    # Small talk
    if has(t, "thank you", "thanks", "dhanyavad", "shukriya"):
        return reply("answer", "You're welcome")
    if has(t, "how are you"):
        return reply("answer", "I am fine, thank you. How can I help?")
    if has(t, "who are you", "your name"):
        return reply("answer", "I am IoT Lab, your voice assistant")
    if re.match(r"(hello|hi|hey|namaste|namaskar)\b", t):
        return reply("answer", "Hello! What can I do for you?")

    # Voice calculator: "what is 25 times 4"
    answer = math_answer(t)
    if answer:
        return reply("answer", answer)

    # Timer controls, before plain "stop" and "timer"
    if has(t, "timer") and has(t, "stop", "cancel", "off", "reset"):
        return reply("timer_cancel")
    if has(t, "add", "extend", "more") and find_seconds(t):
        secs = find_seconds(t)
        return reply("timer_add", f"Added {duration(secs)}", seconds=secs)

    # Cricket and market, before the radio controls ("next match" is not "next station")
    if has(t, "cricket", "score", "scores", "match"):
        return reply("cricket", cricket.spoken())
    # A name alone ("gold") is not enough when the radio is asked for: "play the Hindi gold radio"
    if has(t, "market", "stock", "stocks", "share", "shares", "sensex") or \
            (market.mentioned(t) and not has(t, "radio", "play", "station")):
        return reply("market", market.spoken(t))

    # Radio controls
    if has(t, "next", "skip", "change station", "change the station", "change channel", "another station"):
        return reply("radio_next")
    if has(t, "previous", "last station", "go back to the last station"):
        return reply("radio_prev")
    # "voice" and "sound" too: "increase the voice" was heard on 27 Oct and not understood
    if has(t, "volume up", "louder", "increase volume", "increase the volume", "turn it up", "turn up",
           "voice up", "increase voice", "increase the voice", "sound up", "increase sound",
           "increase the sound"):
        return reply("volume_up", amount=3)
    if has(t, "volume down", "softer", "quieter", "decrease volume", "decrease the volume",
           "turn it down", "turn down", "lower the volume", "reduce volume", "reduce the volume",
           "voice down", "decrease voice", "decrease the voice", "reduce the voice", "sound down",
           "decrease sound", "decrease the sound", "reduce the sound"):
        return reply("volume_down", amount=-3)

    # A station name plays when the radio is asked for, or when it is said alone
    # ("Vividh Bharati"); "tell me something about Jaipur" is a question, not Jaipur Radio.
    station = find_station(t)
    if station and (has(t, "play", "radio", "station", "listen", "tune", "put on") or len(words) <= 3):
        return station

    # Spoken answers; the gadget also opens its clock screen
    if has(t, "weather", "temperature", "mausam", "hot", "cold", "rain"):   # before "today"
        return reply("weather", weather_answer())
    if has(t, "date", "what day", "which day", "today"):
        return reply("time", date_answer())
    if has(t, "time", "clock", "baje", "samay"):
        return reply("time", time_answer())

    # A real question goes to the AI buddy, even if it contains a screen word:
    # "who built the Hawa Mahal" was heard as "who built the home hall" and went home.
    if QUESTION.match(t) and len(words) >= 4 and not TIME_UNIT.search(t):
        return reply("answer", assistant.ask(text.strip()))

    # Screens
    for action, keywords in INTENTS:
        if any(matches(k, t, words) for k in keywords):
            if action == "timer":
                return timer_result(t)
            return reply(action)
    if TIME_UNIT.search(t) and find_seconds(t):            # just "5 minutes"
        return timer_result(t)

    # Anything else is a question for the AI buddy. One or two words ("and") are
    # usually a misheard command, so those still get "Sorry, I did not understand".
    if len(words) >= 3:
        return reply("answer", assistant.ask(text.strip()))
    return reply("unknown")


def timer_result(t):
    secs = find_seconds(t)
    if secs <= 0:
        return reply("timer", "Opening the timer")
    return reply("timer", f"Timer set for {duration(secs)}", seconds=secs)


# ---------------- Speech to text ----------------

# Words Whisper should expect: commands only (tested: names here made Whisper insert them
# where nobody said them, and once loop for 2 minutes).
HINT_WORDS = ("play, stop, next station, previous station, volume up, volume down, radio, "
              "weather, time, date, timer, minutes, seconds, plus, minus, times, divided by, "
              "piano, calculator, home, Vividh Bharati, news, kirtan, bhajan, "
              "cricket score, market, stocks")

print(f"Loading Whisper model '{MODEL}' (first time downloads it, please wait)...")
model = WhisperModel(MODEL, device="cpu", compute_type="int8")
print("Model ready")


def drop_repeats(text):
    """Whisper sometimes loops on unclear audio: 'sky blue, sky blue, sky blue, ...'.
    A phrase of 1-4 words said three or more times in a row is kept only once."""
    words = text.split()
    for size in range(1, 5):
        out, i = [], 0
        while i < len(words):
            chunk = [w.strip(".,!?").lower() for w in words[i:i + size]]
            repeats = 1
            while [w.strip(".,!?").lower() for w in words[i + repeats * size:i + (repeats + 1) * size]] == chunk:
                repeats += 1
            if repeats >= 3 and len(chunk) == size:
                out.extend(words[i:i + size])
                i += repeats * size
            else:
                out.append(words[i])
                i += 1
        words = out
    return " ".join(words).rstrip(",")


def transcribe(pcm_bytes):
    audio = np.frombuffer(pcm_bytes, dtype="<i2").astype(np.float32) / 32768.0
    peak = float(np.max(np.abs(audio))) if audio.size else 0.0
    if peak < 0.01:
        return "", peak
    audio *= 0.9 / peak                  # make quiet recordings loud enough
    # Short commands: one greedy pass, no temperature retries, few tokens.
    # Those retries make Whisper loop on unclear audio.
    options = dict(beam_size=1, temperature=0.0, without_timestamps=True,
                   condition_on_previous_text=False, max_new_tokens=40, hotwords=HINT_WORDS)
    segments, info = model.transcribe(audio, language=LANGUAGE or None, **options)
    text = drop_repeats(" ".join(s.text.strip() for s in segments).strip())
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


# ---------------- Mu-law (the gadget records 8-bit to save memory) ----------------

def _ulaw_table():
    """8-bit mu-law (ITU-T G.711) -> 16-bit sample, for all 256 values."""
    table = np.zeros(256, dtype="<i2")
    for code in range(256):
        u = ~code & 0xFF
        sample = ((((u & 0x0F) << 3) + 0x84) << ((u >> 4) & 0x07)) - 0x84
        table[code] = -sample if u & 0x80 else sample
    return table


ULAW_TO_PCM = _ulaw_table()


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
        elif self.path in ("/market", "/cricket"):
            try:
                self.send_json(market.market_data() if self.path == "/market" else cricket.cricket_data())
            except Exception as e:                      # the gadget shows "not available"
                print(f"{self.path} failed: {e}")
                self.send_json({"error": str(e)}, 502)
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
        if self.headers.get("Content-Type", "").startswith("audio/basic"):
            pcm = ULAW_TO_PCM[np.frombuffer(pcm, dtype=np.uint8)].tobytes()   # 8-bit mu-law from the gadget
        started = time.time()
        text, peak = transcribe(pcm)
        heard_at = time.time()
        result = understand(text) if text else reply("unknown", REPLIES["nothing"])
        result["text"] = text
        try:
            result["say"] = f"/say/{speech_for(result['reply'])}"
        except Exception as e:                          # the gadget still shows the reply
            print(f"Speech failed: {e}")
        print(f"[{self.client_address[0]}] {len(pcm) / 2 / SAMPLE_RATE:.1f}s audio, peak {peak:.2f}, "
              f"whisper {heard_at - started:.1f}s + answer {time.time() - heard_at:.1f}s "
              f"-> {text!r} => {result['action']}: {result['reply']}")
        self.send_json(result)
        if SAVE_RECORDINGS:
            save_recording(pcm, text, result["action"])

    def log_message(self, *args):
        pass                              # we print our own one-line log


if __name__ == "__main__":
    print("Preparing spoken replies...")
    for text in REPLIES.values():
        speech_for(text)
    assistant.warm_up()
    print(f"Voice server listening on port {PORT} (Ctrl+C to stop)")
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
