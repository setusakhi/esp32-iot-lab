// Cricket: live scores, three matches per page (India's matches first).
// The laptop voice server reads them from Cricbuzz (voice-server/cricket.py).
#include "apps.h"
#include "net.h"
#include "gadget_config.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

static const int MAX_MATCHES = 6, PER_PAGE = 3, CARD_Y = 50, CARD_H = 86, CARD_GAP = 4;
static const Rect LIST_AREA = {0, CARD_Y, SCREEN_W, SCREEN_H - CARD_Y};

enum MatchState { LIVE, UPCOMING, DONE };

struct Team  { char name[8]; char score[24]; };
struct Match { char title[66]; char status[66]; Team teams[2]; MatchState state; };

static Match    matches[MAX_MATCHES];
static uint8_t  matchCount = 0;
static uint8_t  page = 0;
static char     updated[12] = "";
static bool     valid = false;
static uint32_t lastFetch = 0;

static int pageCount() { return (matchCount + PER_PAGE - 1) / PER_PAGE; }

static bool fetchCricket() {
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(15000);
  if (!http.begin(VOICE_SERVER_URL "/cricket")) return false;
  int code = http.GET();
  Serial.printf("cricket: HTTP %d\n", code);
  if (code != 200) { http.end(); return false; }

  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, http.getStream());
  http.end();
  if (e) { Serial.printf("cricket: bad JSON (%s)\n", e.c_str()); return false; }

  strlcpy(updated, doc["updated"] | "", sizeof(updated));
  matchCount = 0;
  for (JsonObject m : doc["matches"].as<JsonArray>()) {
    if (matchCount >= MAX_MATCHES) break;
    Match &mt = matches[matchCount++];
    strlcpy(mt.title, m["title"] | "", sizeof(mt.title));
    strlcpy(mt.status, m["status"] | "", sizeof(mt.status));
    const char *st = m["state"] | "live";
    mt.state = !strcmp(st, "upcoming") ? UPCOMING : !strcmp(st, "done") ? DONE : LIVE;
    for (int t = 0; t < 2; t++) {
      strlcpy(mt.teams[t].name, m["teams"][t]["name"] | "?", sizeof(mt.teams[t].name));
      strlcpy(mt.teams[t].score, m["teams"][t]["score"] | "", sizeof(mt.teams[t].score));
    }
  }
  if (page >= pageCount()) page = 0;
  valid = true;
  return true;
}

static void drawCard(const Match &m, int y) {
  tft.fillRoundRect(4, y, SCREEN_W - 8, CARD_H, 10, COL_PANEL);
  tft.setTextPadding(0);

  // badge, then the match title next to it
  const char *badge = m.state == LIVE ? "LIVE" : m.state == UPCOMING ? "NEXT" : "RESULT";
  uint16_t badgeCol = m.state == LIVE ? TFT_RED : m.state == UPCOMING ? 0x34DB : COL_KEY;
  int bw = tft.textWidth(badge, 2) + 12;
  tft.fillRoundRect(12, y + 5, bw, 18, 4, badgeCol);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(badge, 12 + bw / 2, y + 14, 2);
  tft.setTextColor(COL_DIM);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(m.title, 20 + bw, y + 14, 2);

  // the two teams: short name and score, side by side
  for (int t = 0; t < 2; t++) {
    int x = 16 + t * 232;
    tft.setTextColor(TFT_YELLOW);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(m.teams[t].name, x, y + 42, 4);
    tft.setTextColor(TFT_WHITE);
    tft.drawString(m.teams[t].score, x + tft.textWidth(m.teams[t].name, 4) + 10, y + 42, 4);
  }

  tft.setTextColor(m.state == LIVE ? TFT_GREEN : TFT_LIGHTGREY);
  tft.drawString(m.status, 16, y + 70, 2);
}

static void centerMessage(const char *line1, uint16_t col, const char *line2 = nullptr) {
  tft.fillRect(0, 48, SCREEN_W, SCREEN_H - 48, COL_BG);
  tft.setTextPadding(0);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(col);
  tft.drawString(line1, SCREEN_W / 2, line2 ? 160 : 170, 4);
  if (line2) {
    tft.setTextColor(COL_DIM);
    tft.drawString(line2, SCREEN_W / 2, 195, 2);
  }
}

static void drawUpdated() {
  tft.fillRect(250, 0, SCREEN_W - 250, TOPBAR_H, COL_TOPBAR);
  if (!valid) return;
  char line[32];
  if (pageCount() > 1) snprintf(line, sizeof(line), "%s   %d/%d", updated, page + 1, pageCount());
  else strlcpy(line, updated, sizeof(line));
  tft.setTextPadding(0);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(line, 470, TOPBAR_H / 2, 2);
}

static void drawAll() {
  drawUpdated();
  if (!valid) {
    if (netOnline()) centerMessage("Scores not available", TFT_ORANGE, "Is the voice server running on the laptop?");
    else centerMessage("Offline - use WiFi Setup first", TFT_ORANGE);
    return;
  }
  if (matchCount == 0) { centerMessage("No matches right now", COL_DIM); return; }
  tft.fillRect(0, 48, SCREEN_W, SCREEN_H - 48, COL_BG);
  for (int i = 0; i < PER_PAGE; i++) {
    int n = page * PER_PAGE + i;
    if (n >= matchCount) break;
    drawCard(matches[n], CARD_Y + i * (CARD_H + CARD_GAP));
  }
}

static void refresh() {
  if (netOnline()) fetchCricket();     // on failure the last scores stay up
  lastFetch = millis();
}

static void cricketEnter() {
  drawTopBar("Cricket");
  if (!valid) centerMessage("Loading scores...", TFT_YELLOW);
  else drawAll();                      // old scores at once, fresh ones a moment later
  refresh();
  drawAll();
}

static void cricketRedraw() {
  drawTopBar("Cricket");
  drawAll();
}

static void cricketUpdate() {
  uint32_t interval = valid ? CRICKET_REFRESH_SEC * 1000UL : 30000UL;
  if (millis() - lastFetch > interval) {
    refresh();
    drawAll();
  }
}

// Knob: next / previous page of matches
void cricketKnob(int steps) {
  if (!valid || pageCount() < 2) return;
  page = ((page + steps) % pageCount() + pageCount()) % pageCount();
  drawAll();
}

static void cricketTouch(TouchEvent ev, int16_t x, int16_t y) {
  if (ev != TOUCH_DOWN) return;
  if (BACK_BTN.contains(x, y)) { clickSound(); goToApp(&menuApp); return; }
  if (LIST_AREA.contains(x, y)) {      // tap: next page, or refresh when there is one page
    clickSound();
    if (valid && pageCount() > 1) cricketKnob(1);
    else { refresh(); drawAll(); }
  }
}

App cricketApp = {cricketEnter, nullptr, cricketUpdate, cricketTouch, cricketRedraw};
