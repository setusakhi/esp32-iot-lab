"""
Cricket scores for the gadget's Cricket screen, read from Cricbuzz's live-scores page
(unofficial: the page carries the match list as JSON). Matches with India come first,
then other internationals and leagues; live matches before upcoming and finished ones.
"""
import json
import re
import threading
import time
from datetime import datetime

import requests

PAGE = "https://www.cricbuzz.com/cricket-match/live-scores"
HEADERS = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                         "(KHTML, like Gecko) Chrome/130 Safari/537.36"}
MAX_MATCHES = 6
DONE_STATES = {"complete", "abandon", "no result"}
UPCOMING_STATES = {"preview", "upcoming", "toss"}
CHUNK = re.compile(r'self\.__next_f\.push\(\[1,"((?:[^"\\]|\\.)*)"\]\)')


def page_matches(html):
    """Every {"matchInfo", "matchScore"} on the page, once per match."""
    text = "".join(json.loads(f'"{c}"') for c in CHUNK.findall(html))
    decoder = json.JSONDecoder()
    found = {}
    for m in re.finditer(r'\{"matchInfo":\{', text):
        try:
            match = decoder.raw_decode(text, m.start())[0]
            found.setdefault(match["matchInfo"]["matchId"], match)
        except (ValueError, KeyError, TypeError):
            continue
    return list(found.values())


def _overs(o):
    return f"{o:g}" if o is not None else ""


def team_score(score):
    """'245/3 (38.2)' for one innings, '113 & 118/5' for two."""
    innings = [score[k] for k in sorted(score or {}) if k.startswith("inngs")]
    parts = []
    for inn in innings:
        runs, wkts = inn.get("runs", 0), inn.get("wickets", 0)
        parts.append(f"{runs}" if wkts >= 10 else f"{runs}/{wkts}")
    text = " & ".join(parts)
    if len(innings) == 1 and innings[0].get("overs") is not None:
        text += f" ({_overs(innings[0]['overs'])})"
    return text


def state_of(info):
    s = info.get("state", "").lower()
    if s in DONE_STATES:
        return "done"
    if s in UPCOMING_STATES:
        return "upcoming"
    return "live"


def starts_at(ms):
    start = datetime.fromtimestamp(ms / 1000)
    day = "today" if start.date() == datetime.now().date() else start.strftime("%d %b")
    return f"Starts {day} at {start.strftime('%I:%M %p').lstrip('0')}"


def simplify(match):
    info, score = match["matchInfo"], match.get("matchScore", {})
    state = state_of(info)
    teams = []
    for key in ("team1", "team2"):
        t = info.get(key, {})
        teams.append({"name": t.get("teamSName", "?"), "full": t.get("teamName", "?"),
                      "score": team_score(score.get(f"{key}Score"))})
    status = info.get("status", "")
    if state == "upcoming" and info.get("startDate"):
        status = starts_at(int(info["startDate"]))
    return {
        "title": f"{info.get('matchDesc', '')} - {info.get('seriesName', '')}"[:56],   # fits the screen
        "state": state,
        "teams": teams,
        "status": status[:60],
        "_rank": rank(info, state, teams),
    }


def rank(info, state, teams):
    india = any(t["full"] == "India" or t["full"].startswith("India ") for t in teams)
    kind = {"international": 40, "league": 25, "women": 15}.get(info.get("matchType", "").lower(), 0)
    when = {"live": 30, "upcoming": 10, "done": 5}[state]
    # finished more than a day ago, or starting more than two days ahead: not interesting
    now_ms = time.time() * 1000
    if state == "done" and now_ms - int(info.get("endDate", now_ms)) > 86400000:
        return -1
    if state == "upcoming" and int(info.get("startDate", now_ms)) - now_ms > 2 * 86400000:
        return -1
    return (100 if india else 0) + kind + when


def fetch():
    html = requests.get(PAGE, headers=HEADERS, timeout=10).text
    matches = [simplify(m) for m in page_matches(html)]
    matches = sorted((m for m in matches if m["_rank"] >= 0), key=lambda m: -m["_rank"])[:MAX_MATCHES]
    for m in matches:
        del m["_rank"]
    return {"updated": datetime.now().strftime("%I:%M %p").lstrip("0"), "matches": matches}


_cache = {"at": 0, "data": None}
_lock = threading.Lock()


def cricket_data():
    """The match list, refreshed at most every 30 seconds."""
    with _lock:
        if not _cache["data"] or time.time() - _cache["at"] > 30:
            _cache.update(at=time.time(), data=fetch())
        return _cache["data"]


def spoken():
    """The top match in one sentence, for the voice reply."""
    try:
        matches = cricket_data()["matches"]
    except (requests.RequestException, ValueError, KeyError) as e:
        print(f"Cricket failed: {e}")
        return "Here are the cricket scores"
    if not matches:
        return "No cricket matches right now"
    m = matches[0]
    a, b = m["teams"]
    name = f"{a['full']} versus {b['full']}"
    if m["state"] == "upcoming":
        return f"{name}. {m['status']}"
    scores = ". ".join(f"{t['full']} {spoken_score(t['score'])}" for t in (a, b) if t["score"])
    return f"{name}. {scores}. {m['status']}"


def spoken_score(score):
    """'245/3 (38.2)' -> '245 for 3 in 38.2 overs'"""
    score = re.sub(r"(\d+)/(\d+)", r"\1 for \2", score)
    return re.sub(r"\(([\d.]+)\)", r"in \1 overs", score).replace("&", "and")


if __name__ == "__main__":
    print(json.dumps(fetch(), indent=2, ensure_ascii=False))
    print(spoken())
