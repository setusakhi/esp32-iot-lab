// Voice commands: tap the mic, speak, and the laptop voice server
// (Whisper, see voice-server/) tells us what to do.
#include "apps.h"
#include "mic.h"
#include "net.h"
#include "tones.h"
#include "radio.h"
#include "gadget_config.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

static const int  MIC_X = SCREEN_W / 2, MIC_Y = 130, MIC_R = 52;
static const Rect MIC_BTN = {MIC_X - MIC_R, MIC_Y - MIC_R, MIC_R * 2, MIC_R * 2};
static const int  BOOT_BTN = 0;

static uint8_t *recording = nullptr;    // mu-law, 1 byte per sample
static int      recordLen = 0;
static App     *nextApp = nullptr;      // where the command takes us
static uint32_t nextAt = 0;

// Touch button: listen from any screen, then go back there (and resume the radio)
static bool listenSoon = false;
static App *returnApp = nullptr;
static bool resumeRadio = false;

void voiceListenNow() {
  listenSoon = true;
  App *from = currentApp();
  if (from == &voiceApp) return;
  returnApp = (from == &menuApp) ? nullptr : from;
  resumeRadio = (from == &radioApp && radioActive() && radioStatus() == RADIO_PLAYING);
  goToApp(&voiceApp);                  // leaving the radio stops it, so the mic hears only you
}

static void drawMic(uint16_t color) {
  tft.fillCircle(MIC_X, MIC_Y, MIC_R, color);
  tft.fillRoundRect(MIC_X - 13, MIC_Y - 32, 26, 42, 13, TFT_WHITE);
  // U-shaped holder; drawArc angles start at 6 o'clock and run clockwise
  tft.drawArc(MIC_X, MIC_Y - 4, 24, 20, 270, 360, TFT_WHITE, color, false);
  tft.drawArc(MIC_X, MIC_Y - 4, 24, 20, 0, 90, TFT_WHITE, color, false);
  tft.fillRect(MIC_X - 2, MIC_Y + 20, 4, 12, TFT_WHITE);
  tft.fillRect(MIC_X - 14, MIC_Y + 30, 28, 4, TFT_WHITE);
}

static void drawLines(const char *line1, uint16_t col1, const char *line2 = "") {
  tft.fillRect(0, 196, SCREEN_W, 64, COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(0);
  tft.setTextColor(col1);
  tft.drawString(line1, SCREEN_W / 2, 212, 4);
  tft.setTextColor(COL_DIM);
  tft.drawString(line2, SCREEN_W / 2, 244, 2);
}

static bool isAscii(const char *s) {
  for (; *s; s++) if ((uint8_t)*s > 126) return false;
  return true;
}

static void voiceEnter() {
  drawTopBar("Voice");
  drawMic(0x34DB);
  drawLines("Tap the mic and speak", TFT_WHITE, "or touch the talk button");
  tft.setTextColor(COL_DIM);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Try: \"Play Vividh Bharati\"  \"Next station\"  \"What time is it\"  \"25 times 4\"",
                 SCREEN_W / 2, 296, 2);
  nextApp = nullptr;

  // Reserved only while this screen is open: the radio needs that memory for its
  // decoders (kept permanently, every station failed to start). In mu-law, 3 s is
  // 48 KB, which still fits after the radio has played (a 16-bit 96 KB one did not).
  for (int secs = VOICE_RECORD_SECS; secs >= 2 && !recording; secs--) {
    recordLen = MIC_SAMPLE_RATE * secs;
    recording = (uint8_t *)malloc(recordLen);
  }
  Serial.printf("Voice: buffer %s, %d samples, free heap %u\n",
                recording ? "OK" : "FAILED", recordLen, ESP.getFreeHeap());
}

static void voiceExit() {
  free(recording);
  recording = nullptr;
  listenSoon = false;
  returnApp = nullptr;
  resumeRadio = false;
}

// Plays the spoken reply (path from the server, e.g. "/say/3") through the speaker
static void speak(const char *path) {
  if (!*path) return;
  HTTPClient http;
  http.begin(String(VOICE_SERVER_URL) + path);
  http.setTimeout(5000);
  if (http.GET() == 200 && http.getSize() > 0) audioPlayPcm(*http.getStreamPtr(), http.getSize());
  http.end();
}

// "Volume up/down" changes the radio when we go back to it, else the beeps and replies
static void changeVolume(int amount, bool forRadio) {
  if (forRadio) { radioRequestVolume(amount); return; }
  audioSetVolume(constrain((int)audioGetVolume() + (amount > 0 ? 2 : -2), 0, 10));
  Preferences prefs;
  prefs.begin("settings", false);
  prefs.putUChar("toneVol", audioGetVolume());
  prefs.end();
}

// Sends the recording to the laptop and acts on the answer
static void sendAndAct() {
  drawMic(TFT_ORANGE);
  drawLines("Thinking...", TFT_YELLOW);

  HTTPClient http;
  http.begin(VOICE_SERVER_URL "/listen");
  http.addHeader("Content-Type", "audio/basic");   // 8-bit mu-law
  http.setTimeout(20000);
  int code = http.POST(recording, recordLen);
  String body = code == 200 ? http.getString() : String();
  http.end();

  drawMic(0x34DB);
  if (code != 200) {
    Serial.printf("Voice server error: %d\n", code);
    drawLines("Voice server not reachable", TFT_ORANGE, "Is server.py running on the laptop?");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body)) { drawLines("Bad answer from server", TFT_ORANGE); return; }
  const char *text   = doc["text"] | "";
  const char *action = doc["action"] | "unknown";
  const char *reply  = doc["reply"] | "";
  int seconds        = doc["seconds"] | 0;
  Serial.printf("Voice: \"%s\" -> %s %d\n", text, action, seconds);

  // The screen font has no Hindi letters, so show Hindi text only on the Serial Monitor
  String heard = isAscii(text) ? String("\"") + text + "\"" : String("(heard in Hindi)");
  if (heard.length() > 60) heard = heard.substring(0, 57) + "...";
  bool understood = strcmp(action, "unknown") != 0;
  drawLines(reply, understood ? TFT_GREEN : TFT_ORANGE, heard.c_str());
  bool forRadio = resumeRadio || returnApp == &radioApp;
  if (!strcmp(action, "volume_up") || !strcmp(action, "volume_down"))
    changeVolume(doc["amount"] | 0, forRadio);       // before speaking, so the reply sounds at the new level
  speak(doc["say"] | "");

  if (!strcmp(action, "radio"))       { radioAutoPlay(); nextApp = &radioApp; }
  else if (!strcmp(action, "station")) { radioPlayStation(doc["station"] | ""); nextApp = &radioApp; }
  else if (!strcmp(action, "radio_next")) { radioRequestStep(1); nextApp = &radioApp; }
  else if (!strcmp(action, "radio_prev")) { radioRequestStep(-1); nextApp = &radioApp; }
  else if (!strcmp(action, "time") || !strcmp(action, "weather")) nextApp = &clockApp;
  else if (!strcmp(action, "timer"))  { if (seconds > 0) timerStartSeconds(seconds); nextApp = &timerApp; }
  else if (!strcmp(action, "timer_add")) { timerAddSeconds(seconds); nextApp = &timerApp; }
  else if (!strcmp(action, "timer_cancel")) timerCancel();
  else if (!strcmp(action, "calculator")) nextApp = &calcApp;
  else if (!strcmp(action, "piano"))  nextApp = &pianoApp;
  else if (!strcmp(action, "menu") || !strcmp(action, "stop")) nextApp = &menuApp;
  // "answer" (sums, hello...), volume, timer stop: stay, or go back to where we were
  if (!nextApp && returnApp) {
    if (resumeRadio) radioAutoPlay();
    nextApp = returnApp;
  }
  nextAt = millis() + 500;             // a moment to read the answer
}

static void listen() {
  if (!recording) { drawLines("Not enough memory to record", TFT_RED); return; }
  if (!netOnline()) { drawLines("Offline - use WiFi Setup first", TFT_ORANGE); return; }

  audioPlayNote(880, 90);              // "speak now" beep
  delay(200);                          // so the beep is not recorded
  drawMic(TFT_RED);
  drawLines("Listening...", TFT_RED, "Speak now");
  micRecord(recording, recordLen);
  audioPlayNote(660, 60);
  sendAndAct();
}

static void voiceUpdate() {
  if (nextApp && millis() >= nextAt) { goToApp(nextApp); nextApp = nullptr; return; }
  if (listenSoon && !nextApp) { listenSoon = false; listen(); return; }
  if (digitalRead(BOOT_BTN) == LOW && !nextApp) {
    while (digitalRead(BOOT_BTN) == LOW) delay(10);
    listen();
  }
}

static void voiceTouch(TouchEvent ev, int16_t x, int16_t y) {
  if (ev != TOUCH_DOWN) return;
  if (BACK_BTN.contains(x, y)) { clickSound(); goToApp(&menuApp); return; }
  if (MIC_BTN.contains(x, y) && !nextApp) listen();
}

App voiceApp = {voiceEnter, voiceExit, voiceUpdate, voiceTouch};
