// Countdown timer. Keeps running on every screen and rings when done.
#include "apps.h"
#include "tones.h"
#include "radio.h"

static const uint32_t MAX_MS = 99UL * 60000UL + 59000UL;

static uint32_t setMs = 60000;      // last chosen time
static uint32_t remainMs = 60000;   // time left while paused
static uint32_t endAt = 0;
static bool     running = false;
static bool     alarmOn = false;
static uint32_t alarmStart = 0, nextBeep = 0;

static int      shownSecs = -1;
static uint16_t shownColor = 0;
static bool     shownRunning = false;

static const Rect PRESET[4] = {{6, 158, 114, 46}, {124, 158, 114, 46}, {242, 158, 114, 46}, {360, 158, 114, 46}};
static const uint32_t PRESET_MS[4] = {60000, 180000, 300000, 600000};
static const char *PRESET_LABEL[4] = {"1 min", "3 min", "5 min", "10 min"};
static const Rect ADJUST[4] = {{6, 210, 114, 46}, {124, 210, 114, 46}, {242, 210, 114, 46}, {360, 210, 114, 46}};
static const int32_t ADJUST_MS[4] = {-60000, 60000, -10000, 10000};
static const char *ADJUST_LABEL[4] = {"-1 min", "+1 min", "-10 sec", "+10 sec"};
static const Rect START_BTN = {6, 262, 300, 52};
static const Rect RESET_BTN = {316, 262, 158, 52};

static uint32_t timeLeft() {
  if (!running) return remainMs;
  int32_t left = (int32_t)(endAt - millis());
  return left > 0 ? left : 0;
}

// ---- runs every loop, on every screen ----
void timerService() {
  if (running && timeLeft() == 0) {
    running = false;
    remainMs = 0;
    alarmOn = true;
    alarmStart = millis();
    nextBeep = 0;
    if (radioActive()) goToApp(&timerApp);   // stop the radio so the alarm can be heard
  }
  if (alarmOn) {
    if (millis() - alarmStart > 60000) { timerAlarmStop(); return; }  // give up after 1 minute
    if (!radioActive() && millis() >= nextBeep) {
      for (int i = 0; i < 3; i++) { audioPlayNote(1760, 120); audioPlayNote(0, 80); }
      nextBeep = millis() + 1300;
    }
  }
}

bool timerAlarmActive() { return alarmOn; }

void timerAlarmStop() {
  alarmOn = false;
  audioStop();
  remainMs = setMs;
  shownSecs = -1;           // force redraw if the timer screen is open
}

void timerStartSeconds(uint32_t secs) {
  if (alarmOn) timerAlarmStop();
  setMs = remainMs = min((uint32_t)(secs * 1000UL), MAX_MS);
  endAt = millis() + remainMs;
  running = true;
}

// ---- screen ----
static void drawStartButton() {
  drawButton(START_BTN, running ? "Pause" : "Start", running ? TFT_ORANGE : 0x256C);
  shownRunning = running;
}

static void drawTime(bool force) {
  uint32_t ms = timeLeft();
  int secs = (ms + 999) / 1000;
  uint16_t color = TFT_WHITE;
  if (alarmOn) color = ((millis() / 400) % 2) ? TFT_RED : TFT_YELLOW;
  else if (running) color = TFT_GREEN;
  if (!force && secs == shownSecs && color == shownColor) return;
  shownSecs = secs;
  shownColor = color;

  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", secs / 60, secs % 60);
  tft.setTextSize(2);
  tft.setTextColor(color, COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(tft.textWidth("88:88", 7));
  tft.drawString(buf, SCREEN_W / 2, 100, 7);
  tft.setTextSize(1);
  tft.setTextPadding(0);
}

static void timerEnter() {
  drawTopBar("Timer");
  for (int i = 0; i < 4; i++) drawButton(PRESET[i], PRESET_LABEL[i], 0x9AD6);
  for (int i = 0; i < 4; i++) drawButton(ADJUST[i], ADJUST_LABEL[i], COL_KEY);
  drawStartButton();
  drawButton(RESET_BTN, "Reset", 0x6B4D);
  drawTime(true);
}

static void timerUpdate() {
  drawTime(false);
  if (shownRunning != running) drawStartButton();
}

static void timerTouch(TouchEvent ev, int16_t x, int16_t y) {
  if (ev != TOUCH_DOWN) return;
  if (BACK_BTN.contains(x, y)) { clickSound(); goToApp(&menuApp); return; }

  if (START_BTN.contains(x, y)) {
    clickSound();
    if (running) { remainMs = timeLeft(); running = false; }
    else if (remainMs > 0) { endAt = millis() + remainMs; running = true; }
  } else if (RESET_BTN.contains(x, y)) {
    clickSound();
    running = false;
    remainMs = setMs;
  } else if (!running) {
    for (int i = 0; i < 4; i++) {
      if (PRESET[i].contains(x, y)) { clickSound(); setMs = remainMs = PRESET_MS[i]; }
      if (ADJUST[i].contains(x, y)) {
        clickSound();
        int32_t v = (int32_t)remainMs + ADJUST_MS[i];
        if (v < 0) v = 0;
        if (v > (int32_t)MAX_MS) v = MAX_MS;
        setMs = remainMs = v;
      }
    }
  }
  drawStartButton();
  drawTime(true);
}

App timerApp = {timerEnter, nullptr, timerUpdate, timerTouch};
