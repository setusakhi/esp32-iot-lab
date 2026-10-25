// ============================================================
//  ESP32 IoT Lab Gadget
//  Home menu with: Internet Radio, Calculator, Clock & Weather,
//  Timer, Piano, WiFi Setup and Voice commands.
//  Hardware: ESP32 DevKit V1, 3.5" SPI touch TFT, MAX98357A amp,
//            MAX9814 microphone, TTP223 touch button, rotary encoder
// ============================================================
#include <Arduino.h>
#include <Preferences.h>
#include "gadget_config.h"
#include "ui.h"
#include "apps.h"
#include "tones.h"
#include "net.h"
#include "mic.h"
#include "input.h"

static App *current = nullptr;
static App *pending = nullptr;

void goToApp(App *app) { pending = app; }
App *currentApp() { return current; }

static void switchIfPending() {
  if (!pending) return;
  App *next = pending;
  pending = nullptr;
  if (current && current->exit) current->exit();
  current = next;
  tft.fillScreen(COL_BG);
  if (current->enter) current->enter();
}

// ---------------- Touch calibration ----------------
// Saved in flash, so it normally runs only once. It is checked with test taps
// before saving, and runs again by itself if touches stop landing on the screen.

static void waitRelease() {
  while (tft.getTouchRawZ() > 300) delay(20);
  delay(150);
}

// Ask the user to tap a target; true if the tap lands close to it.
static bool verifyTap(int16_t tx, int16_t ty) {
  tft.fillScreen(COL_BG);
  tft.drawCircle(tx, ty, 20, TFT_YELLOW);
  tft.drawCircle(tx, ty, 21, TFT_YELLOW);
  tft.drawFastHLine(tx - 30, ty, 60, TFT_WHITE);
  tft.drawFastVLine(tx, ty - 30, 60, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(0);
  tft.drawString("Check: tap the centre of the circle", SCREEN_W / 2, ty > 160 ? 60 : 270, 4);

  uint32_t until = millis() + 15000;
  uint16_t x, y;
  while (millis() < until) {
    if (tft.getTouch(&x, &y)) {
      Serial.printf("Test tap at %u,%u (target %d,%d)\n", x, y, tx, ty);
      tft.fillCircle(x, y, 5, TFT_GREEN);
      waitRelease();
      return abs((int)x - tx) < 45 && abs((int)y - ty) < 45;
    }
    delay(10);
  }
  Serial.println("Test tap: no valid touch");
  return false;
}

static void runCalibration() {
  uint16_t cal[5];
  for (int attempt = 1;; attempt++) {
    showMessage("Touch calibration", "Tap the TIP of each corner arrow with a pen cap");
    waitRelease();
    delay(2000);
    tft.fillScreen(COL_BG);
    tft.calibrateTouch(cal, TFT_MAGENTA, TFT_BLACK, 15);
    tft.setTouch(cal);
    Serial.printf("Touch calibration (try %d): %u %u %u %u %u\n",
                  attempt, cal[0], cal[1], cal[2], cal[3], cal[4]);
    waitRelease();
    if (verifyTap(SCREEN_W / 2, SCREEN_H / 2) && verifyTap(90, 70) && verifyTap(390, 250)) {
      Preferences prefs;
      prefs.begin("touch", false);
      prefs.putBytes("cal", cal, sizeof(cal));
      prefs.end();
      showMessage("Touch calibrated!", "Saved");
      delay(800);
      return;
    }
    showMessage("Not accurate yet", "Let's try once more - tap exactly on the arrow tips");
    delay(2000);
  }
}

static void setupTouch() {
  uint16_t cal[5];
  Preferences prefs;
  prefs.begin("touch", true);
  bool haveCal = prefs.isKey("cal") && prefs.getBytesLength("cal") == sizeof(cal);
  if (haveCal) prefs.getBytes("cal", cal, sizeof(cal));
  prefs.end();

  if (!haveCal) { runCalibration(); return; }

  tft.setTouch(cal);
  Serial.printf("Touch calibration: %u %u %u %u %u\n", cal[0], cal[1], cal[2], cal[3], cal[4]);
  showMessage("IoT Lab", "Touch screen now to recalibrate");
  uint32_t until = millis() + 3000;
  while (millis() < until) {
    if (tft.getTouchRawZ() > 500) { runCalibration(); return; }
    delay(20);
  }
}

// ---------------- Touch button and knob ----------------
// Touch tap: talk (or silence the alarm). Long touch: stop and go home.
// Knob: volume (radio on its screen, else beeps and spoken replies);
// press: pause/play on the radio screen, else back to home.

static uint32_t saveVolumeAt = 0;

static void handleControls() {
  inputService();

  TouchPress press = touchTake();
  if (press != PRESS_NONE) Serial.printf("Touch button: %s\n", press == PRESS_TAP ? "tap" : "long press");
  if (press == PRESS_TAP) {
    if (timerAlarmActive()) timerAlarmStop();
    else voiceListenNow();
  } else if (press == PRESS_LONG) {
    if (timerAlarmActive()) timerAlarmStop();
    audioStop();
    goToApp(&menuApp);         // leaving the radio screen stops the radio
  }

  int steps = knobTakeSteps();
  if (steps) Serial.printf("Knob: %+d\n", steps);
  if (steps && current == &radioApp) {
    radioAdjustVolume(steps);
  } else if (steps) {
    audioSetVolume(constrain((int)audioGetVolume() + steps, 0, 10));
    clickSound();              // a tick at the new volume
    saveVolumeAt = millis() + 2000;
  }

  if (knobTakePress()) {
    Serial.println("Knob: pressed");
    if (current == &radioApp) radioTogglePause();
    else if (current != &menuApp) { clickSound(); goToApp(&menuApp); }
  }

  if (saveVolumeAt && millis() > saveVolumeAt) {
    saveVolumeAt = 0;
    Preferences prefs;
    prefs.begin("settings", false);
    prefs.putUChar("toneVol", audioGetVolume());
    prefs.end();
  }
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);          // landscape 480 x 320
  audioBegin();
  {
    Preferences prefs;
    prefs.begin("settings", true);
    audioSetVolume(prefs.getUChar("toneVol", DEFAULT_VOLUME));
    prefs.end();
  }
  micBegin();
  voiceBegin();                // before WiFi and radio take memory
  inputBegin();
  pinMode(0, INPUT_PULLUP);    // BOOT button: push-to-talk on the Voice screen
  setupTouch();
  showMessage("Connecting to WiFi...", "Please wait");
  netBegin();
  goToApp(&menuApp);
  switchIfPending();
  audioPlayNote(523, 120);     // start-up chime
  audioPlayNote(784, 180);
  Serial.printf("IoT Lab ready, free memory %u bytes\n", ESP.getFreeHeap());
}

void loop() {
  static bool    down = false;
  static bool    swallow = false;  // a tap that only silenced the alarm
  static uint8_t releaseCount = 0;
  static int16_t lastX = 0, lastY = 0;

  uint16_t x, y;
  bool touched = tft.getTouch(&x, &y);

  // Presses the touch chip feels but that don't map onto the screen mean the
  // calibration is wrong. After a few of those, recalibrate automatically.
  static uint32_t lastRawReport = 0;
  static uint8_t  badPresses = 0;
  if (!touched && tft.getTouchRawZ() > 1000 && millis() - lastRawReport > 500) {
    lastRawReport = millis();
    uint16_t rx, ry;
    tft.getTouchRaw(&rx, &ry);
    Serial.printf("Pressure seen but no valid touch (raw %u,%u z=%u)\n", rx, ry, tft.getTouchRawZ());
    if (++badPresses >= 5) {
      badPresses = 0;
      Serial.println("Touch looks miscalibrated - recalibrating");
      runCalibration();
      tft.fillScreen(COL_BG);
      if (current->enter) current->enter();
      return;
    }
  }

  if (touched) {
    badPresses = 0;
    releaseCount = 0;
    lastX = x; lastY = y;
    if (!down) {
      down = true;
      uint16_t rx, ry;
      tft.getTouchRaw(&rx, &ry);
      Serial.printf("Touch at x=%u y=%u  (raw %u,%u  z=%u)\n", x, y, rx, ry, tft.getTouchRawZ());
      if (timerAlarmActive()) { timerAlarmStop(); swallow = true; }
      else current->touch(TOUCH_DOWN, x, y);
    } else if (!swallow) {
      current->touch(TOUCH_MOVE, x, y);
    }
  } else if (down && ++releaseCount >= 3) {   // ignore tiny gaps in the touch reading
    down = false;
    if (!swallow) current->touch(TOUCH_UP, lastX, lastY);
    swallow = false;
  }

  handleControls();
  timerService();
  if (current->update) current->update();
  switchIfPending();
  delay(5);
}
