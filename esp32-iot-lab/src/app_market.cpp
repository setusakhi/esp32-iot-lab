// Market: stocks, gold & currencies, and mutual fund NAVs in three tabs.
// The laptop voice server fetches the prices (voice-server/market.py, watchlist in
// market.json) and sends a small JSON over plain HTTP, so no secure connection is needed here.
#include "apps.h"
#include "net.h"
#include "gadget_config.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

static const int MAX_PAGES = 3, MAX_ROWS = 6, ROW_Y = 50, ROW_H = 40;
static const Rect LIST_AREA = {0, ROW_Y, SCREEN_W, MAX_ROWS * ROW_H};
static const Rect TAB_RECT[MAX_PAGES] = {{200, 6, 82, 32}, {286, 6, 104, 32}, {394, 6, 80, 32}};

struct Row  { char name[24]; char price[16]; float chg; bool hasChg, highlight; };
struct Page { char title[12]; char note[52]; Row rows[MAX_ROWS]; uint8_t count; };

static Page     pages[MAX_PAGES];
static uint8_t  pageCount = 0;
static uint8_t  page = 0;
static char     status[20] = "", updated[12] = "";
static bool     valid = false;
static uint32_t lastFetch = 0;

static bool fetchMarket() {
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(15000);              // the server may need a few seconds for fresh prices
  if (!http.begin(VOICE_SERVER_URL "/market")) return false;
  int code = http.GET();
  Serial.printf("market: HTTP %d\n", code);
  if (code != 200) { http.end(); return false; }

  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, http.getStream());
  http.end();
  if (e) { Serial.printf("market: bad JSON (%s)\n", e.c_str()); return false; }

  strlcpy(status, doc["status"] | "", sizeof(status));
  strlcpy(updated, doc["updated"] | "", sizeof(updated));
  pageCount = 0;
  for (JsonObject p : doc["pages"].as<JsonArray>()) {
    if (pageCount >= MAX_PAGES) break;
    Page &pg = pages[pageCount++];
    strlcpy(pg.title, p["title"] | "", sizeof(pg.title));
    strlcpy(pg.note, p["note"] | "", sizeof(pg.note));
    pg.count = 0;
    for (JsonObject r : p["rows"].as<JsonArray>()) {
      if (pg.count >= MAX_ROWS) break;
      Row &row = pg.rows[pg.count++];
      strlcpy(row.name, r["name"] | "", sizeof(row.name));
      strlcpy(row.price, r["price"] | "--", sizeof(row.price));
      row.hasChg = !r["chg"].isNull();
      row.chg = r["chg"] | 0.0f;
      row.highlight = r["hl"] | 0;
    }
  }
  if (page >= pageCount) page = 0;
  valid = pageCount > 0;
  return valid;
}

static void drawTabs() {
  for (int i = 0; i < pageCount; i++)
    drawButton(TAB_RECT[i], pages[i].title, i == page ? 0x256C : COL_KEY, TFT_WHITE, 2);
}

static void drawArrow(int x, int y, bool up, uint16_t col) {
  if (up) tft.fillTriangle(x - 7, y + 5, x + 7, y + 5, x, y - 6, col);
  else    tft.fillTriangle(x - 7, y - 5, x + 7, y - 5, x, y + 6, col);
}

static void drawRow(const Row &r, int y) {
  if (r.highlight) tft.fillRoundRect(4, y + 2, SCREEN_W - 8, ROW_H - 4, 8, COL_PANEL);
  else tft.drawFastHLine(10, y + ROW_H - 1, SCREEN_W - 20, COL_KEY);
  int cy = y + ROW_H / 2;
  tft.setTextPadding(0);
  tft.setTextColor(r.highlight ? TFT_YELLOW : TFT_WHITE);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(r.name, 14, cy, 4);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(r.price, 350, cy, 4);
  if (!r.hasChg) return;
  bool up = r.chg >= 0;
  uint16_t col = up ? TFT_GREEN : TFT_RED;
  char pct[12];
  snprintf(pct, sizeof(pct), "%.2f%%", fabsf(r.chg));
  tft.setTextColor(col);
  tft.drawString(pct, 468, cy, 4);
  drawArrow(468 - tft.textWidth(pct, 4) - 12, cy, up, col);
}

static void drawFooter() {
  tft.fillRect(0, 292, SCREEN_W, SCREEN_H - 292, COL_BG);
  tft.setTextPadding(0);
  tft.setTextColor(COL_DIM);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(pages[page].note, 10, 306, 2);
  char right[40];
  snprintf(right, sizeof(right), "%s  %s", status, updated);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(strstr(status, "open") ? TFT_GREEN : COL_DIM);
  tft.drawString(right, 470, 306, 2);
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

static void drawPage() {
  tft.fillRect(0, 48, SCREEN_W, SCREEN_H - 48, COL_BG);
  const Page &pg = pages[page];
  if (pg.count == 0) {
    tft.setTextColor(COL_DIM);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Nothing here yet", SCREEN_W / 2, 150, 4);
  }
  for (int i = 0; i < pg.count; i++) drawRow(pg.rows[i], ROW_Y + i * ROW_H);
  drawFooter();
}

static void drawAll() {
  if (!valid) {
    if (netOnline()) centerMessage("Prices not available", TFT_ORANGE, "Is the voice server running on the laptop?");
    else centerMessage("Offline - use WiFi Setup first", TFT_ORANGE);
    return;
  }
  drawTabs();
  drawPage();
}

static void refresh() {
  if (netOnline()) fetchMarket();
  lastFetch = millis();
}

static void marketEnter() {
  drawTopBar("Market");
  if (!valid) centerMessage("Loading prices...", TFT_YELLOW);
  else drawAll();                      // old prices at once, fresh ones a moment later
  refresh();
  drawAll();
}

static void marketRedraw() {
  drawTopBar("Market");
  drawAll();
}

static void marketUpdate() {
  uint32_t interval = valid ? MARKET_REFRESH_SEC * 1000UL : 30000UL;   // retry sooner after a failure
  if (millis() - lastFetch > interval) {
    refresh();
    drawAll();
  }
}

static void showPage(int p) {
  if (!valid || p == page) return;
  page = p;
  drawTabs();
  drawPage();
}

// Knob: next / previous tab
void marketKnob(int steps) {
  if (!valid || pageCount == 0) return;
  showPage(((page + steps) % pageCount + pageCount) % pageCount);
}

static void marketTouch(TouchEvent ev, int16_t x, int16_t y) {
  if (ev != TOUCH_DOWN) return;
  if (BACK_BTN.contains(x, y)) { clickSound(); goToApp(&menuApp); return; }
  for (int i = 0; i < pageCount; i++)
    if (TAB_RECT[i].contains(x, y)) { clickSound(); showPage(i); return; }
  if (LIST_AREA.contains(x, y) || !valid) {   // tap the list: refresh now
    clickSound();
    refresh();
    drawAll();
  }
}

App marketApp = {marketEnter, nullptr, marketUpdate, marketTouch, marketRedraw};
