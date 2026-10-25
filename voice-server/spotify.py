"""
Spotify control for the voice server (official Spotify Web API, needs Premium).

Music plays on your own Spotify device (phone, laptop, speaker); the gadget is the remote.

One-time login (opens your browser):   .venv\\Scripts\\python spotify.py login
Keys come from spotify_config.json; the login is saved in spotify_token.json.
Both files are private: don't share them.
"""
import base64
import json
import os
import re
import secrets
import sys
import time
import urllib.parse
import webbrowser
from difflib import SequenceMatcher
from http.server import BaseHTTPRequestHandler, HTTPServer

import requests

HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG_FILE = os.path.join(HERE, "spotify_config.json")
TOKEN_FILE = os.path.join(HERE, "spotify_token.json")
API = "https://api.spotify.com/v1"
SCOPES = "user-read-playback-state user-modify-playback-state user-read-currently-playing"


def load_config():
    try:
        with open(CONFIG_FILE, encoding="utf-8") as f:
            cfg = json.load(f)
    except (OSError, ValueError):
        return None
    if "PASTE" in cfg.get("client_id", "PASTE") or "PASTE" in cfg.get("client_secret", "PASTE"):
        return None
    return cfg


def _basic_auth(cfg):
    pair = f"{cfg['client_id']}:{cfg['client_secret']}".encode()
    return {"Authorization": "Basic " + base64.b64encode(pair).decode()}


def _save_token(data):
    data["expires_at"] = time.time() + data.get("expires_in", 3600) - 60
    old = _read_token() or {}
    data.setdefault("refresh_token", old.get("refresh_token"))   # Spotify may not resend it
    with open(TOKEN_FILE, "w", encoding="utf-8") as f:
        json.dump(data, f)


def _read_token():
    try:
        with open(TOKEN_FILE, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


# ---------------- One-time login ----------------

def login():
    cfg = load_config()
    if not cfg:
        sys.exit("Fill in client_id and client_secret in spotify_config.json first.")
    redirect = urllib.parse.urlparse(cfg["redirect_uri"])
    state = secrets.token_urlsafe(16)
    result = {}

    class Callback(BaseHTTPRequestHandler):
        def do_GET(self):
            query = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            ok = query.get("state", [""])[0] == state and "code" in query
            result["code"] = query["code"][0] if ok else None
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            msg = "Spotify connected. You can close this tab." if ok else "Login failed, please try again."
            self.wfile.write(f"<h2>{msg}</h2>".encode())

        def log_message(self, *args):
            pass

    url = "https://accounts.spotify.com/authorize?" + urllib.parse.urlencode({
        "client_id": cfg["client_id"], "response_type": "code", "redirect_uri": cfg["redirect_uri"],
        "scope": SCOPES, "state": state})
    print("Opening the Spotify login page in your browser...")
    print("If it doesn't open, copy this address into your browser:\n" + url)
    webbrowser.open(url)
    server = HTTPServer((redirect.hostname, redirect.port), Callback)
    while "code" not in result:
        server.handle_request()
    if not result["code"]:
        sys.exit("Login failed.")

    r = requests.post("https://accounts.spotify.com/api/token", headers=_basic_auth(cfg), data={
        "grant_type": "authorization_code", "code": result["code"], "redirect_uri": cfg["redirect_uri"]})
    r.raise_for_status()
    _save_token(r.json())
    print("Spotify login saved. Restart the voice server to use it.")


# ---------------- Web API ----------------

class NotReady(Exception):
    """Spotify is not set up, or no device is available."""


def _access_token():
    cfg, tok = load_config(), _read_token()
    if not cfg or not tok or not tok.get("refresh_token"):
        raise NotReady("Spotify is not set up")
    if time.time() > tok.get("expires_at", 0):
        r = requests.post("https://accounts.spotify.com/api/token", headers=_basic_auth(cfg), data={
            "grant_type": "refresh_token", "refresh_token": tok["refresh_token"]}, timeout=10)
        r.raise_for_status()
        _save_token(r.json())
        tok = _read_token()
    return tok["access_token"]


def _call(method, path, **kw):
    r = requests.request(method, API + path, timeout=10,
                         headers={"Authorization": "Bearer " + _access_token()}, **kw)
    if r.status_code == 404 and "NO_ACTIVE_DEVICE" in r.text:
        raise NotReady("No Spotify device is active")
    r.raise_for_status()
    try:
        return r.json() if r.content else None
    except ValueError:                 # some commands (next, pause) answer with a non-JSON body
        return None


def configured():
    return load_config() is not None and bool((_read_token() or {}).get("refresh_token"))


def player():
    """Current playback, or None when nothing is playing anywhere."""
    return _call("GET", "/me/player")


def _device_id(fresh=False):
    """The active device, else the first one Spotify knows (phone, laptop...).
    fresh: skip the remembered active device and ask for the current device list."""
    state = None if fresh else player()
    if state and state.get("device"):
        return state["device"]["id"]
    devices = (_call("GET", "/me/player/devices") or {}).get("devices", [])
    if not devices:
        raise NotReady("Open Spotify on your phone first")
    return devices[0]["id"]


def _start(body=None):
    """Starts playback (body = what to play, None = continue). A phone that went to sleep
    can leave a stale device id behind (404), so try once more with a fresh device list."""
    for fresh in (False, True):
        try:
            _call("PUT", "/me/player/play", params={"device_id": _device_id(fresh)}, json=body)
            return
        except requests.HTTPError as e:
            if fresh or e.response is None or e.response.status_code != 404:
                raise


# ---------------- Pausing while the gadget listens ----------------
# With music playing nearby, the microphone hears the song instead of the command.
# The gadget calls duck() just before recording; after the command, unduck() resumes
# unless the command itself decided what plays next.

_ducked = False
HANDLES_PLAYBACK = {"play", "play_artist", "play_album", "stop", "next", "previous", "resume",
                    "radio", "station"}                # the gadget's own radio takes over


def duck():
    """Pauses Spotify if it is playing. True when it did."""
    global _ducked
    if not configured():
        return False
    state = player()
    if state and state.get("is_playing"):
        pause()
        _ducked = True
    return _ducked


def unduck(command):
    global _ducked
    if _ducked:
        _ducked = False
        if command not in HANDLES_PLAYBACK:
            resume()


# ---------------- Fixing mis-heard artist names ----------------
# Whisper often gets names slightly wrong ("लखा मंगी उसकर" for Lata Mangeshkar). We write the
# words in Roman letters and pick the closest known artist. Add your favourites here.
KNOWN_ARTISTS = [
    "Arijit Singh", "Lata Mangeshkar", "Kishore Kumar", "Mohammed Rafi", "Asha Bhosle",
    "Shreya Ghoshal", "Sonu Nigam", "Atif Aslam", "Jubin Nautiyal", "Neha Kakkar",
    "Kumar Sanu", "Udit Narayan", "Alka Yagnik", "A. R. Rahman", "Pritam", "Badshah",
    "Yo Yo Honey Singh", "Diljit Dosanjh", "Sidhu Moose Wala", "AP Dhillon", "Guru Randhawa",
    "Mukesh", "Jagjit Singh", "Nusrat Fateh Ali Khan", "Rahat Fateh Ali Khan", "KK",
    "Shaan", "Sunidhi Chauhan", "Armaan Malik", "Darshan Raval", "B Praak", "Vishal Mishra",
    "Sachet Tandon", "Shankar Mahadevan", "Kailash Kher", "Hariharan", "Mohit Chauhan",
    "Masoom Sharma", "Khesari Lal Yadav", "Pawan Singh", "Anuradha Paudwal", "Gulzar",
    "Lucky Ali", "Papon", "King", "Karan Aujla", "Shubh", "Amit Trivedi", "Vishal Dadlani",
]

_CONSONANTS = dict(zip(
    "क ख ग घ ङ च छ ज झ ञ ट ठ ड ढ ण त थ द ध न प फ ब भ म य र ल व श ष स ह".split(),
    "k kh g gh n ch chh j jh n t th d dh n t th d dh n p ph b bh m y r l v sh sh s h".split()))
_VOWELS = dict(zip("अ आ इ ई उ ऊ ए ऐ ओ औ ऋ".split(), "a aa i ee u oo e ai o au ri".split()))
_SIGNS = dict(zip("ा ि ी ु ू े ै ो ौ ृ ॉ".split(), "aa i ee u oo e ai o au ri o".split()))
_NUKTA = {"ज़": "z", "फ़": "f", "ड़": "d", "ढ़": "dh", "क़": "k", "ख़": "kh", "ग़": "g"}


def romanize(text):
    """Rough Devanagari -> Roman letters, good enough for comparing names."""
    for k, v in _NUKTA.items():
        text = text.replace(k, v + "्")
    out, i = [], 0
    while i < len(text):
        c = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if c in _CONSONANTS:
            out.append(_CONSONANTS[c])
            if nxt in _SIGNS:
                out.append(_SIGNS[nxt])
                i += 1
            elif nxt == "्":
                i += 1
            else:
                out.append("a")                     # the vowel every consonant carries
        elif c in _VOWELS:
            out.append(_VOWELS[c])
        elif c in "ंँ":
            out.append("n")
        elif c not in "़्":
            out.append(c)
        i += 1
    # Hindi drops the last "a" of a word in speech ("singha" -> "singh")
    return re.sub(r"a\b", "", "".join(out)).lower()


def _sound(text):
    """Letters only, with spellings that sound alike made equal (aa=a, ee=i, sh=s...)."""
    t = re.sub(r"[^a-z]", "", text.lower())
    for a, b in (("aa", "a"), ("ee", "i"), ("oo", "u"), ("sh", "s"), ("ph", "f"), ("w", "v"),
                 ("kh", "k"), ("gh", "g"), ("th", "t"), ("dh", "d"), ("bh", "b"), ("z", "j")):
        t = t.replace(a, b)
    return re.sub(r"(.)\1+", r"\1", t)            # double letters -> single


def closest_artist(query, threshold):
    """The known artist whose name sounds closest to query, or None."""
    q = _sound(romanize(query) if not query.isascii() else query)
    if len(q) < 3:
        return None
    score, name = max((SequenceMatcher(None, q, _sound(a)).ratio(), a) for a in KNOWN_ARTISTS)
    return name if score >= threshold else None


def play_search(query, want_artist=False):
    """Plays the best match for query and returns a short description (None if nothing found).
    want_artist: the user asked for an artist ("X ke gaane"), so take Spotify's top artist.
    Otherwise a song, unless the words clearly name an artist ("play Arijit Singh").
    Hindi-script queries work: Spotify matches अरिजीत सिंह to Arijit Singh by itself."""
    # Measured on real mis-hearings: right names scored 0.79-1.0, wrong ones up to 0.73
    # ("Kesariya" vs Khesari Lal Yadav). "play X" is usually a song, so it is a bit stricter.
    known = closest_artist(query, 0.75 if want_artist else 0.78)
    if known:
        print(f"Spotify: '{query}' sounds like {known}")
        query, want_artist = known, True
    found = _call("GET", "/search", params={"q": query, "type": "artist,track", "limit": 5, "market": "IN"})
    artists = found.get("artists", {}).get("items", [])
    tracks = found.get("tracks", {}).get("items", [])
    q = query.lower()
    named_artist = next((a for a in artists[:3]
                         if SequenceMatcher(None, q, a["name"].lower()).ratio() >= 0.8), None)
    artist = artists[0] if want_artist and artists else named_artist
    if not artist and not tracks and artists:
        artist = artists[0]
    if not artist and not tracks:
        return None
    if artist:
        _start({"context_uri": artist["uri"]})
        return artist["name"]
    t = tracks[0]
    _start({"uris": [t["uri"]]})
    return f"{t['name']} by {t['artists'][0]['name']}"


def play_album(query):
    """Plays a film soundtrack ("Dhurandhar movie songs"), searched as an album."""
    found = _call("GET", "/search", params={"q": query, "type": "album", "limit": 3, "market": "IN"})
    albums = found.get("albums", {}).get("items", [])
    if not albums:
        return None
    _start({"context_uri": albums[0]["uri"]})
    return albums[0]["name"]


def resume():
    _start()


def pause():
    _call("PUT", "/me/player/pause")


def next_track():
    _call("POST", "/me/player/next")


def previous_track():
    _call("POST", "/me/player/previous")


def change_volume(step):
    state = player()
    if not state:
        raise NotReady("Nothing is playing on Spotify")
    if not state["device"].get("supports_volume", True):
        # Spotify does not let other apps change the volume on phones
        raise NotReady("Spotify does not allow volume control on your phone")
    vol = max(0, min(100, state["device"].get("volume_percent", 50) + step))
    _call("PUT", "/me/player/volume", params={"volume_percent": vol})
    return vol


def now_playing():
    state = player()
    if not state or not state.get("item"):
        return None
    item = state["item"]
    return f"{item['name']} by {item['artists'][0]['name']}"


# ---------------- Understanding Spotify commands ----------------

# Short English commands: "play <artist or song>", "play", "stop", "pause", "next",
# "previous", "volume up", "volume down", "what's playing". The server listens in English,
# so singer names arrive in Roman letters, which Spotify search handles best.
WORDS = {  # checked in this order; whole words only
    "playing":  ["what's playing", "what is playing", "which song", "song name", "what song"],
    "next":     ["next", "skip"],
    "previous": ["previous", "last song"],
    "stop":     ["stop", "pause"],
    "resume":   ["resume", "continue"],
}
LOUDER = ["volume up", "louder", "increase volume", "volume increase", "turn it up", "turn up"]
SOFTER = ["volume down", "softer", "quieter", "decrease volume", "volume decrease", "turn it down", "turn down"]
# Words after "play" that mean "just play something on Spotify"
RESUME_WORDS = {"", "music", "the music", "song", "a song", "songs", "spotify", "something", "some music"}
# Words after "play" that mean the gadget's own radio
RADIO_WORDS = {"radio", "the radio", "fm", "fm radio", "the fm"}
# "play" anywhere: extra words often come before it ("Kishore, ... play Arijit Singh song")
PLAY = re.compile(r"\b(?:play|played|plays|playing)\b\s*(.*?)(?:\s+on\s+spotify)?(?:\s+please)?$")
# "play Dhurandhar movie songs", "play songs from the movie Dhurandhar" = a film soundtrack
MOVIE = [re.compile(r"^(.+?)\s+(?:movie|film|picture)(?:\s+songs?)?$"),
         re.compile(r"^(?:some\s+)?songs?\s+(?:of|from)\s+(?:the\s+)?(?:movie|film)\s+(.+)$")]
# "play songs of X", "play X songs" = an artist
ARTIST = [re.compile(r"^(?:some\s+)?songs?\s+(?:of|by|from)\s+(.+)$"), re.compile(r"^(.+?)\s+songs?$")]


def _clean(text):
    return re.sub(r"\s+", " ", re.sub(r"[.,!?\"]", " ", text.lower())).strip()


def _has(text, phrase):
    return re.search(r"\b" + re.escape(phrase) + r"\b", text) is not None


def parse(text):
    """(command, value) for a Spotify command, or None if the text isn't one."""
    t = _clean(text)
    if any(_has(t, w) for w in WORDS["playing"]):  # before PLAY: "what's playing" has "playing"
        return "playing", None
    m = PLAY.search(t)
    if m:
        what = m.group(1).strip()
        if what in RADIO_WORDS:
            return None                            # the gadget radio (server.py)
        if what in RESUME_WORDS:
            return "resume", None
        for pattern in MOVIE:
            a = pattern.match(what)
            if a:
                return "play_album", a.group(1).strip()
        for pattern in ARTIST:
            a = pattern.match(what)
            if a:
                return "play_artist", a.group(1).strip()
        return "play", what
    if any(_has(t, w) for w in LOUDER):
        return "volume", 15
    if any(_has(t, w) for w in SOFTER):
        return "volume", -15
    for command, words in WORDS.items():
        if any(_has(t, w) for w in words):
            return command, None
    # "play" itself was mis-heard ("Rai Lata Mangeshkar"), but a known singer was said
    for artist in KNOWN_ARTISTS:
        if len(artist) > 4 and _has(t, artist.lower()):
            return "play_artist", artist
    return None


def handle(text):
    """Runs a Spotify command. Returns {"action", "command", "reply"} or None if it isn't one."""
    cmd = parse(text) if configured() else None
    if not cmd:
        return None
    command, value = cmd
    try:
        if command in ("play", "play_artist"):
            what = play_search(value, want_artist=command == "play_artist")
            reply = f"Playing {what} on Spotify" if what else f"I could not find {value} on Spotify"
        elif command == "play_album":
            what = play_album(value)
            reply = f"Playing {what} on Spotify" if what else f"I could not find {value} on Spotify"
        elif command == "stop":
            state = player()
            if not (_ducked or (state and state.get("is_playing"))):
                return None                        # nothing on Spotify: "stop" is for the gadget
            if not _ducked:                        # already paused for listening
                pause()
            reply = "Stopped"
        elif command == "resume":
            resume()
            reply = "Resuming Spotify"
        elif command == "next":
            next_track()
            reply = "Next song"
        elif command == "previous":
            previous_track()
            reply = "Previous song"
        elif command == "volume":
            reply = f"Spotify volume {change_volume(value)} percent"
        else:
            playing = now_playing()
            reply = f"This is {playing}" if playing else "Nothing is playing on Spotify"
    except NotReady as e:
        reply = str(e)
    except requests.HTTPError as e:
        print(f"Spotify error: {e}")
        forbidden = e.response is not None and e.response.status_code == 403
        reply = ("Spotify does not allow that on this device" if forbidden
                 else "Spotify did not respond")
    except requests.RequestException as e:
        print(f"Spotify error: {e}")
        reply = "Spotify did not respond"
    return {"action": "spotify", "command": command, "reply": reply}


if __name__ == "__main__":
    if sys.argv[1:] == ["login"]:
        login()
    elif sys.argv[1:2] == ["test"]:
        print(handle(" ".join(sys.argv[2:])))
    else:
        print(__doc__)
