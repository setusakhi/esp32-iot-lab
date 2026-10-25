// ============================================================
//  Hardware test (environment "hwtest")
//  - Shows raw touch readings on screen and Serial Monitor
//  - Beeps every 2 seconds to test the MAX98357 amplifier
//  - Erases saved touch calibration, so the piano app
//    asks for a fresh calibration next time
// ============================================================
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include "gadget_config.h"
#include "tones.h"

TFT_eSPI tft;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== HARDWARE TEST ===");

  Preferences prefs;
  prefs.begin("touch", false);
  prefs.clear();               // forget old (possibly bad) calibration
  prefs.end();
  Serial.println("Touch calibration erased");

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("HARDWARE TEST", 10, 10, 4);
  tft.drawString("Press the screen and watch Z, X, Y change", 10, 50, 2);
  tft.drawString("You should hear a beep every 2 seconds", 10, 70, 2);

  audioBegin();
  audioSetVolume(6);
}

void loop() {
  static uint32_t lastBeep = 0;
  static uint32_t lastRead = 0;

  if (millis() - lastBeep > 2000) {
    lastBeep = millis();
    audioPlayNote(440, 300);
    Serial.printf("BEEP (440 Hz) - audio bytes sent so far: %lu\n", (unsigned long)audioBytesSent());
    tft.fillCircle(450, 25, 12, TFT_GREEN);
    delay(300);
    tft.fillCircle(450, 25, 12, TFT_BLACK);
  }

  if (millis() - lastRead > 200) {
    lastRead = millis();
    uint16_t x = 0, y = 0;
    uint16_t z = tft.getTouchRawZ();
    tft.getTouchRaw(&x, &y);

    char line[64];
    snprintf(line, sizeof(line), "Z=%4u   X=%4u   Y=%4u   ", z, x, y);
    tft.drawString(line, 10, 130, 4);
    tft.drawString(z > 500 ? "TOUCHED        " : "not touched    ", 10, 180, 4);
    Serial.println(line);
  }
}
