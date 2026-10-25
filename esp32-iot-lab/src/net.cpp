#include "net.h"
#include "gadget_config.h"
#include "ui.h"
#include <WiFi.h>
#include <WiFiManager.h>

static void drawSetupInstructions(WiFiManager *) {
  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(0);
  tft.setTextColor(TFT_YELLOW);
  tft.drawString("WiFi Setup", SCREEN_W / 2, 40, 4);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("1. On your phone, connect to WiFi:", SCREEN_W / 2, 95, 2);
  tft.setTextColor(TFT_CYAN);
  tft.drawString(WIFI_SETUP_AP_NAME, SCREEN_W / 2, 125, 4);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("2. A setup page opens (or visit 192.168.4.1)", SCREEN_W / 2, 170, 2);
  tft.drawString("3. Tap 'Configure WiFi', pick your network,", SCREEN_W / 2, 195, 2);
  tft.drawString("   enter the password and Save", SCREEN_W / 2, 215, 2);
  tft.setTextColor(COL_DIM);
  tft.drawString("Continues offline after 2 minutes", SCREEN_W / 2, 280, 2);
  drawButton(BACK_BTN, "< Back", COL_KEY, TFT_WHITE, 2);
}

static void startClockSync() {
  WiFi.setSleep(false);          // power saving makes downloads and radio streams drop out
  configTzTime(TIMEZONE, "pool.ntp.org", "time.google.com", "time.nist.gov");
}

// Runs the setup hotspot until WiFi is saved, the timeout passes,
// or the user taps "< Back". True when connected.
static bool runPortal(WiFiManager &wm) {
  uint32_t until = millis() + WIFI_SETUP_TIMEOUT * 1000UL;
  while (millis() < until) {
    if (wm.process()) return true;                 // saved and connected
    uint16_t x, y;
    if (tft.getTouch(&x, &y) && BACK_BTN.contains(x, y)) {
      clickSound();
      Serial.println("WiFi setup cancelled");
      break;
    }
    delay(10);
  }
  wm.stopConfigPortal();
  return false;
}

static void connected() {
  Serial.printf("WiFi connected to \"%s\", IP %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  startClockSync();
}

void netBegin() {
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.setDebugOutput(false);
  wm.setAPCallback(drawSetupInstructions);
  wm.setConnectTimeout(15);
  wm.setConfigPortalBlocking(false);   // so the Back button keeps working
  // If WiFi was saved before but isn't reachable now (e.g. at a school),
  // don't open the setup hotspot; just continue offline.
  if (wm.getWiFiIsSaved()) wm.setEnableConfigPortal(false);

  if (wm.autoConnect(WIFI_SETUP_AP_NAME) || (wm.getConfigPortalActive() && runPortal(wm))) {
    connected();
  } else {
    Serial.println("WiFi not connected, running offline");
  }
}

void netSetupPortal() {
  WiFiManager wm;
  wm.setDebugOutput(false);
  wm.setConfigPortalBlocking(false);
  drawSetupInstructions(&wm);
  wm.startConfigPortal(WIFI_SETUP_AP_NAME);
  if (runPortal(wm)) connected();
}

bool netOnline() { return WiFi.status() == WL_CONNECTED; }

bool netGetTime(struct tm *out) {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;   // not synced yet
  localtime_r(&now, out);
  return true;
}
