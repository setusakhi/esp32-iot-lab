#include "screensaver.h"
#include "apps.h"
#include "net.h"
#include "gadget_config.h"

static bool active = false;

// ---- Twinkling stars ----
static const int STAR_COUNT = 60;
static int16_t starX[STAR_COUNT], starY[STAR_COUNT];
static uint8_t starLevel[STAR_COUNT];

// ---- Clock (moves to a new spot every minute, so no part of the screen stays lit) ----
static const int CLOCK_W = 330, CLOCK_H = 150;
static int16_t clockX = 75, clockY = 50;
static int lastMinute = -1, lastSecond = -1;

// ---- Radio bars ----
static const int BARS = 16, BAR_W = 20, BAR_GAP = 8, BAR_BASE = 312, BAR_MAX = 46;
static const int BARS_X = (SCREEN_W - BARS * (BAR_W + BAR_GAP) + BAR_GAP) / 2;
static float barLevel[BARS], barTarget[BARS];
static String shownStation, shownSong;
static uint32_t nextFrame = 0;

static uint16_t starColor(uint8_t level) {
  return tft.color565(level, level, min(255, level + 40));
}

static bool inClockArea(int x, int y) {
  return x >= clockX - 4 && x < clockX + CLOCK_W + 4 && y >= clockY - 4 && y < clockY + CLOCK_H + 4;
}

static bool inRadioArea(int y) { return y >= 230; }

static void placeStar(int i) {
  do {
    starX[i] = random(SCREEN_W);
    starY[i] = random(SCREEN_H);
  } while (inClockArea(starX[i], starY[i]) || inRadioArea(starY[i]));
  starLevel[i] = random(40, 200);
}

static void drawClock(bool full) {
  struct tm now;
  if (!netGetTime(&now)) return;
  if (!full && now.tm_sec == lastSecond) return;
  lastSecond = now.tm_sec;

  if (now.tm_min != lastMinute || full) {      // new minute: move and redraw everything
    tft.fillRect(clockX - 4, clockY - 4, CLOCK_W + 8, CLOCK_H + 8, TFT_BLACK);
    lastMinute = now.tm_min;
    if (!full) {
      clockX = random(10, SCREEN_W - CLOCK_W - 10);
      clockY = random(10, 230 - CLOCK_H - 10);
    }
    for (int i = 0; i < STAR_COUNT; i++)      // stars must not sit under the new clock
      if (inClockArea(starX[i], starY[i])) placeStar(i);

    char hhmm[8], date[32];
    if (CLOCK_24H) strftime(hhmm, sizeof(hhmm), "%H:%M", &now);
    else           strftime(hhmm, sizeof(hhmm), "%I:%M", &now);
    strftime(date, sizeof(date), "%A, %d %B", &now);
    tft.setTextDatum(TC_DATUM);
    tft.setTextPadding(0);
    tft.setTextColor(0x7DFF);                 // soft sky blue
    tft.setTextSize(2);
    tft.drawString(hhmm[0] == '0' && !CLOCK_24H ? hhmm + 1 : hhmm, clockX + CLOCK_W / 2, clockY, 7);
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY);
    tft.drawString(date, clockX + CLOCK_W / 2, clockY + 112, 4);
  }

  // Seconds: a thin bar that fills up under the time
  int w = (CLOCK_W - 40) * now.tm_sec / 59;
  tft.fillRect(clockX + 20, clockY + 102, w, 3, 0x2B5F);
  tft.fillRect(clockX + 20 + w, clockY + 102, CLOCK_W - 40 - w, 3, 0x10A2);
}

static void drawRadioInfo() {
  String station = radioStationName();
  String song = radioSongTitle();
  if (station == shownStation && song == shownSong) return;
  shownStation = station;
  shownSong = song;
  tft.fillRect(0, 232, SCREEN_W, 30, TFT_BLACK);
  if (!station.length()) {                    // radio off: clear the bars too
    tft.fillRect(0, BAR_BASE - BAR_MAX - 2, SCREEN_W, BAR_MAX + 4, TFT_BLACK);
    return;
  }
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(0);
  tft.setTextColor(TFT_GREEN);
  tft.drawString(station, SCREEN_W / 2, 232, 2);
  if (song.length()) {
    if (song.length() > 60) song = song.substring(0, 60);
    tft.setTextColor(COL_DIM);
    tft.drawString(song, SCREEN_W / 2, 248, 1);
  }
}

static void drawBars() {
  for (int b = 0; b < BARS; b++) {
    if (random(100) < 18) barTarget[b] = random(6, BAR_MAX);   // music-like jumps
    barLevel[b] += (barTarget[b] - barLevel[b]) * 0.35f;
    barTarget[b] *= 0.93f;                                     // and slow falls
    int h = (int)barLevel[b];
    int x = BARS_X + b * (BAR_W + BAR_GAP);
    tft.fillRect(x, BAR_BASE - BAR_MAX, BAR_W, BAR_MAX - h, TFT_BLACK);
    // green at the bottom, yellow in the middle, red on top
    for (int y = 0; y < h; y += 4) {
      uint16_t col = y < BAR_MAX * 0.6f ? TFT_GREEN : y < BAR_MAX * 0.85f ? TFT_YELLOW : TFT_RED;
      tft.fillRect(x, BAR_BASE - y - 3, BAR_W, 3, col);
    }
  }
}

static void twinkle() {
  for (int n = 0; n < 4; n++) {
    int i = random(STAR_COUNT);
    starLevel[i] = constrain(starLevel[i] + random(-60, 61), 20, 255);
    tft.drawPixel(starX[i], starY[i], starColor(starLevel[i]));
    if (starLevel[i] > 180) {                 // bright stars get a small cross
      tft.drawPixel(starX[i] - 1, starY[i], starColor(starLevel[i] / 2));
      tft.drawPixel(starX[i] + 1, starY[i], starColor(starLevel[i] / 2));
    }
  }
}

void saverStart() {
  if (active) return;
  active = true;
  tft.fillScreen(TFT_BLACK);
  for (int i = 0; i < STAR_COUNT; i++) {
    placeStar(i);
    tft.drawPixel(starX[i], starY[i], starColor(starLevel[i]));
  }
  for (int b = 0; b < BARS; b++) barLevel[b] = barTarget[b] = 0;
  shownStation = shownSong = "\x01";          // force the radio line to draw
  lastMinute = -1;
  drawClock(true);
  Serial.println("Screensaver on");
}

void saverStop() {
  if (!active) return;
  active = false;
  tft.fillScreen(COL_BG);
  Serial.println("Screensaver off");
}

bool saverActive() { return active; }

void saverUpdate() {
  if (!active || millis() < nextFrame) return;
  nextFrame = millis() + 60;                  // about 16 frames a second
  drawClock(false);
  drawRadioInfo();
  if (shownStation.length()) drawBars();
  twinkle();
}
