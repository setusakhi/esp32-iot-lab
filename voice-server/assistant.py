"""
AI buddy: answers general questions ("who sang Lag Ja Gale?", "how far is the moon?")
with a small local model running in Ollama on this laptop: free, and nothing leaves it.

Answers are kept to one or two short sentences, because they are spoken aloud and shown
on the gadget's small screen. The last few exchanges are remembered for follow-up questions
("and how old is she?") and forgotten after a few quiet minutes.

  Install once:  winget install Ollama.Ollama   then   ollama pull llama3.2:3b
  Test alone:    .venv\\Scripts\\python.exe assistant.py "who wrote the Gita"
"""
import os
import re
import sys
import threading
import time
from datetime import datetime

import requests

OLLAMA_URL = os.environ.get("VOICE_OLLAMA_URL", "http://localhost:11434")
MODEL = os.environ.get("VOICE_AI_MODEL", "llama3.2:3b")
CITY = os.environ.get("VOICE_CITY", "Jaipur")
HISTORY_TURNS = 4            # question + answer pairs kept for follow-ups
FORGET_AFTER = 300           # seconds of quiet before starting a fresh conversation
MAX_CHARS = 240              # what fits the screen and stays short to listen to

SYSTEM = ("You are IoT Lab, a friendly voice assistant on a small desk gadget in {city}, India. "
          "Your answer is spoken aloud by a text-to-speech voice and shown on a small screen. "
          "Reply in plain English in one or two short sentences, at most 40 words. "
          "Answer the question directly, like a knowledgeable friend. "
          "No lists, no markdown, no emoji. You have no camera or sensors, so do not claim "
          "to know what is happening around the gadget right now. If you are not sure of a "
          "fact, say so briefly instead of guessing. Today is {date}.")

_history = []                # [{"role": "user"|"assistant", "content": ...}]
_last_at = 0.0
_lock = threading.Lock()


def _clean(text):
    """Plain speakable text: no markdown, one line, cut at a sentence end if too long."""
    text = re.sub(r"[*_#`>]+", "", text)
    # Math the voice can read and the screen font can show: "A = πr^2" -> "A = pi r squared"
    for symbol, words in (("π", " pi "), ("^2", " squared"), ("²", " squared"), ("^3", " cubed"),
                          ("³", " cubed"), ("√", "square root of "), ("°C", " degrees Celsius"),
                          ("°", " degrees"), ("×", " times ")):
        text = text.replace(symbol, words)
    text = re.sub(r"\s+", " ", text).strip()
    if len(text) <= MAX_CHARS:
        return text
    cut = text[:MAX_CHARS]
    end = max(cut.rfind(". "), cut.rfind("? "), cut.rfind("! "))
    return cut[:end + 1] if end > 60 else cut.rsplit(" ", 1)[0] + "..."


def ask(question):
    """The model's short answer, or a friendly message if Ollama is not running."""
    global _last_at
    with _lock:
        if time.time() - _last_at > FORGET_AFTER:
            _history.clear()
        system = SYSTEM.format(city=CITY, date=datetime.now().strftime("%A, %d %B %Y"))
        messages = [{"role": "system", "content": system}] + _history + \
                   [{"role": "user", "content": question}]
        try:
            r = requests.post(f"{OLLAMA_URL}/api/chat", timeout=45, json={
                "model": MODEL, "messages": messages, "stream": False, "keep_alive": "30m",
                "options": {"num_predict": 90, "temperature": 0.3}})
            r.raise_for_status()
            answer = _clean(r.json()["message"]["content"])
        except requests.ConnectionError:
            print("AI buddy: Ollama is not running (start the Ollama app)")
            return "Sorry, my AI brain is not running on the laptop"
        except (requests.RequestException, KeyError, ValueError) as e:
            print(f"AI buddy failed: {e}")
            return "Sorry, I could not think of an answer"
        if not answer:
            return "Sorry, I could not think of an answer"
        _history.extend([{"role": "user", "content": question},
                         {"role": "assistant", "content": answer}])
        del _history[:-HISTORY_TURNS * 2]
        _last_at = time.time()
        return answer


def warm_up():
    """Loads the model in the background at start, so the first question isn't slow."""
    def run():
        try:
            requests.post(f"{OLLAMA_URL}/api/generate", timeout=120,
                          json={"model": MODEL, "prompt": "", "keep_alive": "30m"})
            print(f"AI buddy ready ({MODEL})")
        except requests.RequestException:
            print("AI buddy: Ollama is not running, general questions will not be answered")
    threading.Thread(target=run, daemon=True).start()


if __name__ == "__main__":
    for q in sys.argv[1:] or ["Who sang Lag Ja Gale?", "How old was she when it came out?"]:
        started = time.time()
        print(f"Q: {q}\nA: {ask(q)}  ({time.time() - started:.1f}s)\n")
