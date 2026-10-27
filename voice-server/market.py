"""
Market data for the gadget's Market screen: stocks, gold / silver / currencies, and
mutual fund NAVs. The watchlist is in market.json.

  Stocks, gold, silver, currencies: Yahoo Finance (unofficial; NSE prices are about 15 minutes late)
  Mutual fund NAVs: mfapi.in (AMFI data, once a day)

Find a fund's scheme code:  .venv\\Scripts\\python.exe market.py search parag parikh
"""
import json
import os
import re
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime

import requests

CONFIG_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "market.json")
HEADERS = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                         "(KHTML, like Gecko) Chrome/130 Safari/537.36"}
TROY_OUNCE_GRAMS = 31.1035
GOLD, SILVER, DOLLAR = "GC=F", "SI=F", "USDINR=X"


def load_config():
    with open(CONFIG_FILE, encoding="utf-8") as f:
        return json.load(f)


def indian_number(x, decimals=2):
    """1234567.8 -> '12,34,567.80' (lakh / crore grouping)."""
    whole, _, frac = f"{abs(x):.{decimals}f}".partition(".")
    if len(whole) > 3:
        head, tail = whole[:-3], whole[-3:]
        groups = []
        while len(head) > 2:
            groups.insert(0, head[-2:])
            head = head[:-2]
        whole = ",".join(([head] if head else []) + groups) + "," + tail
    return ("-" if x < 0 else "") + whole + (f".{frac}" if frac else "")


def percent(now, before):
    return round((now / before - 1) * 100, 2) if now and before else None


def yahoo_quote(symbol):
    """(price, previous close) from Yahoo Finance, or (None, None)."""
    try:
        r = requests.get(f"https://query1.finance.yahoo.com/v8/finance/chart/{symbol}",
                         params={"range": "1d", "interval": "15m"}, headers=HEADERS, timeout=8)
        meta = r.json()["chart"]["result"][0]["meta"]
        return meta["regularMarketPrice"], meta.get("chartPreviousClose") or meta.get("previousClose")
    except (requests.RequestException, KeyError, IndexError, TypeError, ValueError) as e:
        print(f"Market: {symbol} failed: {e}")
        return None, None


def fund_nav(code):
    """(NAV, previous NAV, date like '24-10-2026') from mfapi.in, or Nones."""
    try:
        data = requests.get(f"https://api.mfapi.in/mf/{code}", headers=HEADERS, timeout=8).json()["data"]
        return float(data[0]["nav"]), float(data[1]["nav"]) if len(data) > 1 else None, data[0]["date"]
    except (requests.RequestException, KeyError, IndexError, TypeError, ValueError) as e:
        print(f"Market: fund {code} failed: {e}")
        return None, None, None


def row(label, price, before, decimals=2, highlight=False):
    r = {"name": label, "price": indian_number(price, decimals) if price else "--",
         "chg": percent(price, before), "value": price}       # value: for the spoken reply
    if highlight:
        r["hl"] = 1
    return r


def market_open(now=None):
    """NSE hours, Monday to Friday 9:15 to 15:30 (exchange holidays are not known here)."""
    now = now or datetime.now()
    return now.weekday() < 5 and (9, 15) <= (now.hour, now.minute) < (15, 30)


def build():
    cfg = load_config()
    symbols = {s["symbol"] for s in cfg["stocks"] + cfg["currencies"]} | {GOLD, SILVER, DOLLAR}
    with ThreadPoolExecutor(max_workers=8) as pool:
        quotes = dict(zip(symbols, pool.map(yahoo_quote, symbols)))
        navs = list(pool.map(lambda f: fund_nav(f["code"]), cfg["funds"]))

    stocks = [row(s["label"], *quotes[s["symbol"]], highlight=s.get("highlight", False))
              for s in cfg["stocks"]]

    # Gold and silver trade in dollars per troy ounce: convert to rupees per 10 g / per kg.
    # That is the world price: Indian shops add import duty and GST on top.
    (gold, gold_before), (silver, silver_before), (usd, usd_before) = \
        quotes[GOLD], quotes[SILVER], quotes[DOLLAR]
    metals = []
    for label, price, before, grams in (("Gold 10 g", gold, gold_before, 10),
                                        ("Silver 1 kg", silver, silver_before, 1000)):
        now_rs = price * usd * grams / TROY_OUNCE_GRAMS if price and usd else None
        before_rs = before * usd_before * grams / TROY_OUNCE_GRAMS if before and usd_before else None
        metals.append(row(label, now_rs, before_rs, decimals=0))
    currencies = [row(c["label"], *quotes[c["symbol"]]) for c in cfg["currencies"]]

    funds = [row(f["label"], nav, before) for f, (nav, before, _) in zip(cfg["funds"], navs)]
    nav_dates = sorted({d for _, _, d in navs if d})

    now = datetime.now()
    return {
        "status": "Market open" if market_open(now) else "Market closed",
        "updated": now.strftime("%I:%M %p").lstrip("0"),
        "pages": [
            {"title": "Stocks", "note": "NSE, about 15 min late", "rows": stocks},
            {"title": "Gold & FX", "note": "World price in Rs, no duty/GST",
             "rows": metals + currencies},
            {"title": "MF NAV",
             "note": (f"NAV of {nav_dates[-1]}" if nav_dates else "Add funds in voice-server/market.json"),
             "rows": funds},
        ],
    }


_cache = {"at": 0, "data": None}
_lock = threading.Lock()


def market_data():
    """Everything for the Market screen, refreshed at most every minute (10 when closed)."""
    with _lock:
        max_age = 60 if market_open() else 600
        if not _cache["data"] or time.time() - _cache["at"] > max_age:
            _cache.update(at=time.time(), data=build())
        return _cache["data"]


def _keyword(label):
    """The word that names a row when spoken: 'Tata Motors PV' -> 'tata', 'USD / INR' -> 'usd'."""
    return label.split()[0].lower()


SPOKEN_NAMES = {"usd": "dollar", "eur": "euro", "gbp": "pound"}


def mentioned(text):
    """True if the text names something on the Market screen ('reliance', 'gold', 'dollar')."""
    cfg = load_config()
    words = {_keyword(x["label"]) for x in cfg["stocks"] + cfg["currencies"]} | {"gold", "silver"}
    words |= {SPOKEN_NAMES.get(w, w) for w in words}
    return any(re.search(rf"\b{re.escape(w)}\b", text) for w in words)


def spoken(text=""):
    """'NIFTY 50 is at 22,753, up 0.68 percent' for the row the text names, else the first stock."""
    try:
        rows = [r for p in market_data()["pages"] for r in p["rows"]]
    except (requests.RequestException, OSError, ValueError, KeyError) as e:
        print(f"Market failed: {e}")
        return "Here is the market"
    if not rows:
        return "Here is the market"
    pick = rows[0]
    for r in rows:
        key = _keyword(r["name"])
        if re.search(rf"\b({re.escape(key)}|{SPOKEN_NAMES.get(key, key)})\b", text):
            pick = r
            break
    if pick["value"] is None:
        return f"{pick['name']} is not available right now"
    key = _keyword(pick["name"])
    name = SPOKEN_NAMES.get(key, pick["name"]).replace(" 10 g", ", 10 grams,").replace(" 1 kg", ", 1 kilo,")
    value = f"{pick['value']:,.0f}" if pick["value"] >= 1000 else f"{pick['value']:.2f}"
    unit = "" if pick.get("hl") else " rupees"              # the highlighted row is the index
    answer = f"{name[0].upper()}{name[1:]} is at {value}{unit}"
    if pick["chg"]:
        answer += f", {'up' if pick['chg'] > 0 else 'down'} {abs(pick['chg'])} percent"
    elif pick["chg"] == 0:
        answer += ", unchanged"
    return answer


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "search":
        query = " ".join(sys.argv[2:])
        for s in requests.get("https://api.mfapi.in/mf/search", params={"q": query},
                              headers=HEADERS, timeout=10).json():
            print(f"{s['schemeCode']:>8}  {s['schemeName']}")
    else:
        print(json.dumps(build(), indent=2, ensure_ascii=False))
        print(spoken())
