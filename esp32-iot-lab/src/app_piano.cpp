// Touch piano: Sa Re Ga Ma Pa Dha Ni Sa, with a demo tune and siren
#include "apps.h"
#include "tones.h"

static const Rect VOL_DOWN = {300, 4, 50, 36};
static const Rect VOL_UP   = {420, 4, 54, 36};
static const Rect TUNE_BTN = {6, 50, 230, 54};
static const Rect SIREN_BTN = {244, 50, 230, 54};

static const int KEY_COUNT = 8;
static const int KEY_Y = 110;
static const int KEY_H = SCREEN_H - KEY_Y;
static const int KEY_W = SCREEN_W / KEY_COUNT;

static const uint16_t KEY_FREQ[KEY_COUNT]   = {262, 294, 330, 349, 392, 440, 494, 523};
static const char    *KEY_SARGAM[KEY_COUNT] = {"Sa", "Re", "Ga", "Ma", "Pa", "Dha", "Ni", "Sa'"};
static const char    *KEY_NOTE[KEY_COUNT]   = {"C", "D", "E", "F", "G", "A", "B", "C"};
static const uint16_t KEY_GLOW[KEY_COUNT]   = {TFT_RED, TFT_ORANGE, TFT_YELLOW, TFT_GREEN,
                                               TFT_CYAN, TFT_BLUE, TFT_VIOLET, TFT_MAGENTA};

// Twinkle Twinkle Little Star (traditional)
struct Step { int8_t key; uint16_t ms; };
static const Step TUNE[] = {
  {0,400},{0,400},{4,400},{4,400},{5,400},{5,400},{4,800},
  {3,400},{3,400},{2,400},{2,400},{1,400},{1,400},{0,800},
};
static const int TUNE_LEN = sizeof(TUNE) / sizeof(TUNE[0]);

static int      tunePos = -1;
static uint32_t tuneNextAt = 0;
static int      litKey = -1;
static int      heldKey = -1;

static void drawKey(int i, bool pressed) {
  int x = i * KEY_W;
  tft.fillRect(x + 1, KEY_Y, KEY_W - 2, KEY_H, pressed ? KEY_GLOW[i] : TFT_WHITE);
  tft.drawRect(x, KEY_Y, KEY_W, KEY_H, TFT_BLACK);
  tft.setTextColor(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(0);
  tft.drawString(KEY_SARGAM[i], x + KEY_W / 2, SCREEN_H - 60, 4);
  tft.drawString(KEY_NOTE[i],   x + KEY_W / 2, SCREEN_H - 25, 2);
}

static void lightKey(int i) {
  if (litKey == i) return;
  if (litKey >= 0) drawKey(litKey, false);
  if (i >= 0) drawKey(i, true);
  litKey = i;
}

static void drawVolume() {
  tft.fillRect(352, 4, 66, 36, COL_TOPBAR);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(String(audioGetVolume()), 385, 23, 4);
}

static void stopTune() { tunePos = -1; lightKey(-1); }

static void pianoEnter() {
  drawTopBar("Piano");
  drawButton(VOL_DOWN, "-", COL_KEY);
  drawButton(VOL_UP, "+", COL_KEY);
  drawVolume();
  drawButton(TUNE_BTN, "Play Tune", TFT_BLUE);
  drawButton(SIREN_BTN, "Siren", TFT_RED);
  litKey = heldKey = -1;
  for (int i = 0; i < KEY_COUNT; i++) drawKey(i, false);
}

static void pianoExit() { tunePos = -1; audioStop(); }

static void pianoUpdate() {
  if (tunePos < 0 || millis() < tuneNextAt) return;
  if (tunePos >= TUNE_LEN) { stopTune(); return; }
  const Step &s = TUNE[tunePos];
  lightKey(s.key);
  audioPlayNote(KEY_FREQ[s.key], s.ms * 9 / 10);
  tuneNextAt = millis() + s.ms;
  tunePos++;
}

static void pianoTouch(TouchEvent ev, int16_t x, int16_t y) {
  if (ev == TOUCH_UP) {
    if (heldKey >= 0) { lightKey(-1); heldKey = -1; }
    return;
  }
  int k = (y >= KEY_Y) ? x / KEY_W : -1;
  if (k >= KEY_COUNT) k = KEY_COUNT - 1;
  if (k >= 0) {
    if (k != heldKey) {               // new key, or sliding across keys
      stopTune();
      audioStop();
      audioPlayNote(KEY_FREQ[k], 400);
      lightKey(k);
      heldKey = k;
    }
    return;
  }
  if (ev != TOUCH_DOWN) return;
  if (BACK_BTN.contains(x, y)) { clickSound(); goToApp(&menuApp); }
  else if (TUNE_BTN.contains(x, y)) { audioStop(); tunePos = 0; tuneNextAt = millis(); }
  else if (SIREN_BTN.contains(x, y)) {
    stopTune(); audioStop();
    for (int i = 0; i < 4; i++) { audioPlayNote(700, 280); audioPlayNote(1000, 280); }
  }
  else if (VOL_UP.contains(x, y))   { audioSetVolume(audioGetVolume() + 1); drawVolume(); audioPlayNote(880, 80); }
  else if (VOL_DOWN.contains(x, y)) { if (audioGetVolume() > 0) audioSetVolume(audioGetVolume() - 1); drawVolume(); audioPlayNote(880, 80); }
}

App pianoApp = {pianoEnter, pianoExit, pianoUpdate, pianoTouch};
