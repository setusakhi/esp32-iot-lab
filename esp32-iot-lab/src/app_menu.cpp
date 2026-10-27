// Home screen: 8 big tiles
#include "apps.h"
#include "tones.h"
#include "net.h"
#include "gadget_config.h"

enum Icon { ICON_RADIO, ICON_CALC, ICON_CLOCK, ICON_TIMER, ICON_PIANO, ICON_WIFI, ICON_MARKET, ICON_CRICKET };

struct Tile {
  Rect        r;
  const char *label;
  const char *sublabel;
  uint16_t    color;
  Icon        icon;
  App        *app;       // nullptr = WiFi setup action
};

static Tile tiles[] = {
  {{6,   52, 114, 126}, "Radio",      "Hindi FM",     0xE267, ICON_RADIO,   &radioApp},
  {{124, 52, 114, 126}, "Market",     "Stocks & Gold", 0x1B8F, ICON_MARKET,  &marketApp},
  {{242, 52, 114, 126}, "Cricket",    "Live scores",  0x2C49, ICON_CRICKET, &cricketApp},
  {{360, 52, 114, 126}, "Clock",      "& Weather",    0xF4E2, ICON_CLOCK,   &clockApp},
  {{6,  186, 114, 126}, "Calculator", nullptr,        0x34DB, ICON_CALC,    &calcApp},
  {{124,186, 114, 126}, "Timer",      nullptr,        0x9AD6, ICON_TIMER,   &timerApp},
  {{242,186, 114, 126}, "Piano",      nullptr,        0x256C, ICON_PIANO,   &pianoApp},
  {{360,186, 114, 126}, "WiFi",       "Setup",        0x324B, ICON_WIFI,    nullptr},
};
static const int TILE_COUNT = sizeof(tiles) / sizeof(tiles[0]);
static const Rect VOICE_BTN = {130, 5, 104, 34};   // in the top bar

static int  lastMinute = -1;
static bool lastOnline = false;

static void drawIcon(Icon icon, int cx, int cy, uint16_t bg) {
  const uint16_t W = TFT_WHITE;
  switch (icon) {
    case ICON_RADIO:
      tft.drawLine(cx - 18, cy - 16, cx + 12, cy - 34, W);
      tft.drawLine(cx - 17, cy - 15, cx + 13, cy - 33, W);
      tft.fillRoundRect(cx - 32, cy - 16, 64, 38, 6, W);
      tft.fillCircle(cx - 13, cy + 3, 11, bg);
      tft.fillRect(cx + 6, cy - 7, 20, 4, bg);
      tft.fillRect(cx + 6, cy + 1, 20, 4, bg);
      tft.fillRect(cx + 6, cy + 9, 20, 4, bg);
      break;
    case ICON_CALC:
      tft.fillRoundRect(cx - 22, cy - 32, 44, 62, 5, W);
      tft.fillRect(cx - 16, cy - 26, 32, 12, bg);
      for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
          tft.fillRect(cx - 16 + c * 12, cy - 8 + r * 12, 8, 8, bg);
      break;
    case ICON_CLOCK:
      tft.fillCircle(cx + 26, cy - 22, 10, TFT_YELLOW);
      tft.fillCircle(cx - 4, cy, 28, W);
      tft.drawWideLine(cx - 4, cy, cx - 4, cy - 19, 4, bg, W);
      tft.drawWideLine(cx - 4, cy, cx + 9, cy + 4, 4, bg, W);
      break;
    case ICON_TIMER:
      tft.fillRect(cx - 24, cy - 35, 48, 5, W);
      tft.fillRect(cx - 24, cy + 30, 48, 5, W);
      tft.fillTriangle(cx - 19, cy - 30, cx + 19, cy - 30, cx, cy, W);
      tft.fillTriangle(cx - 19, cy + 30, cx + 19, cy + 30, cx, cy, W);
      break;
    case ICON_PIANO:
      for (int i = 0; i < 5; i++) tft.fillRect(cx - 35 + i * 14, cy - 26, 12, 52, W);
      tft.fillRect(cx - 35 + 8,  cy - 26, 8, 30, TFT_BLACK);
      tft.fillRect(cx - 21 + 8,  cy - 26, 8, 30, TFT_BLACK);
      tft.fillRect(cx + 7 + 8,   cy - 26, 8, 30, TFT_BLACK);
      break;
    case ICON_WIFI:
      tft.drawArc(cx, cy + 22, 44, 37, 135, 225, W, bg, false);
      tft.drawArc(cx, cy + 22, 30, 23, 135, 225, W, bg, false);
      tft.drawArc(cx, cy + 22, 16, 9, 135, 225, W, bg, false);
      tft.fillCircle(cx, cy + 22, 5, W);
      break;
    case ICON_MARKET:                                  // bars with a rising arrow
      tft.fillRect(cx - 30, cy + 6, 12, 24, W);
      tft.fillRect(cx - 12, cy - 6, 12, 36, W);
      tft.fillRect(cx + 6,  cy - 18, 12, 48, W);
      tft.drawWideLine(cx - 32, cy - 6, cx - 4, cy - 26, 4, TFT_GREEN, bg);
      tft.drawWideLine(cx - 4, cy - 26, cx + 22, cy - 34, 4, TFT_GREEN, bg);
      tft.fillTriangle(cx + 30, cy - 37, cx + 18, cy - 42, cx + 22, cy - 28, TFT_GREEN);
      break;
    case ICON_CRICKET:                                 // bat and ball
      tft.drawWideLine(cx - 22, cy + 26, cx + 12, cy - 22, 12, W, bg);
      tft.drawWideLine(cx + 14, cy - 25, cx + 22, cy - 36, 5, 0xC618, bg);   // handle
      tft.fillCircle(cx + 22, cy + 20, 9, TFT_RED);
      tft.drawFastHLine(cx + 15, cy + 20, 14, TFT_WHITE);
      break;
  }
}

static void drawTile(const Tile &t) {
  tft.fillRoundRect(t.r.x, t.r.y, t.r.w, t.r.h, 12, t.color);
  int cx = t.r.x + t.r.w / 2;
  drawIcon(t.icon, cx, t.r.y + 46, t.color);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(0);
  uint8_t font = tft.textWidth(t.label, 4) <= t.r.w - 8 ? 4 : 2;   // "Calculator" needs the small font
  if (t.sublabel) {
    tft.drawString(t.label, cx, t.r.y + 94, font);
    tft.drawString(t.sublabel, cx, t.r.y + 114, 2);
  } else {
    tft.drawString(t.label, cx, t.r.y + 100, font);
  }
}

static void drawStatus(bool force) {
  struct tm now;
  bool haveTime = netGetTime(&now);
  bool online = netOnline();
  int minute = haveTime ? now.tm_hour * 60 + now.tm_min : -2;
  if (!force && minute == lastMinute && online == lastOnline) return;
  lastMinute = minute;
  lastOnline = online;

  tft.fillRect(250, 0, SCREEN_W - 250, TOPBAR_H, COL_TOPBAR);
  tft.setTextDatum(MR_DATUM);
  tft.setTextPadding(0);
  if (haveTime) {
    char buf[12];
    if (CLOCK_24H) strftime(buf, sizeof(buf), "%H:%M", &now);
    else           strftime(buf, sizeof(buf), "%I:%M %p", &now);
    tft.setTextColor(TFT_WHITE);
    tft.drawString(buf, 470, TOPBAR_H / 2, 4);
  }
  tft.setTextColor(online ? TFT_GREEN : TFT_ORANGE);
  tft.drawString(online ? "Online" : "Offline", haveTime ? 335 : 470, TOPBAR_H / 2, 2);
}

static void menuEnter() {
  tft.fillScreen(COL_BG);
  tft.fillRect(0, 0, SCREEN_W, TOPBAR_H, COL_TOPBAR);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(ML_DATUM);
  tft.drawString("IoT Lab", 12, TOPBAR_H / 2, 4);
  drawButton(VOICE_BTN, "Voice", 0xE8E4, TFT_WHITE, 4);
  for (int i = 0; i < TILE_COUNT; i++) drawTile(tiles[i]);
  drawStatus(true);
}

static void menuUpdate() { drawStatus(false); }

static void menuTouch(TouchEvent ev, int16_t x, int16_t y) {
  if (ev != TOUCH_DOWN) return;
  if (VOICE_BTN.contains(x, y)) { clickSound(); goToApp(&voiceApp); return; }
  for (int i = 0; i < TILE_COUNT; i++) {
    if (!tiles[i].r.contains(x, y)) continue;
    clickSound();
    if (tiles[i].app) {
      goToApp(tiles[i].app);
    } else {
      netSetupPortal();     // blocks until saved or timed out
      menuEnter();
    }
    return;
  }
}

App menuApp = {menuEnter, nullptr, menuUpdate, menuTouch};
