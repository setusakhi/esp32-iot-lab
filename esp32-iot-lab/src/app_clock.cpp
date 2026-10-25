// Clock & Weather: big clock, date, and live weather from Open-Meteo
#include "apps.h"
#include "net.h"
#include "gadget_config.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

struct Weather {
  bool  valid = false;
  float temp, tmax, tmin, wind;
  int   humidity, code;
  char  updated[8];
};

static Weather  wx;
static uint32_t lastFetch = 0;
static int      lastSecond = -1;
static int      lastDay = -1;
static bool     lastSynced = false;

static const char *weatherText(int c) {
  if (c == 0) return "Clear sky";
  if (c == 1) return "Mainly clear";
  if (c == 2) return "Partly cloudy";
  if (c == 3) return "Cloudy";
  if (c == 45 || c == 48) return "Fog";
  if (c >= 51 && c <= 57) return "Drizzle";
  if (c >= 61 && c <= 67) return "Rain";
  if (c >= 71 && c <= 77) return "Snow";
  if (c >= 80 && c <= 82) return "Rain showers";
  if (c == 85 || c == 86) return "Snow showers";
  if (c >= 95) return "Thunderstorm";
  return "Unknown";
}

static void drawCloud(int cx, int cy, uint16_t col) {
  tft.fillCircle(cx - 14, cy + 2, 12, col);
  tft.fillCircle(cx + 2, cy - 6, 16, col);
  tft.fillCircle(cx + 18, cy + 3, 11, col);
  tft.fillRect(cx - 14, cy + 4, 32, 11, col);
}

static void drawWeatherIcon(int code, int cx, int cy) {
  tft.fillRect(cx - 40, cy - 40, 80, 80, COL_PANEL);
  if (code <= 1) {                                   // sun
    tft.fillCircle(cx, cy, 18, TFT_YELLOW);
    for (int a = 0; a < 360; a += 45) {
      float r = a * DEG_TO_RAD;
      tft.drawWideLine(cx + cos(r) * 24, cy + sin(r) * 24, cx + cos(r) * 32, cy + sin(r) * 32,
                       3, TFT_YELLOW, COL_PANEL);
    }
  } else if (code == 2) {                            // sun behind cloud
    tft.fillCircle(cx + 10, cy - 10, 15, TFT_YELLOW);
    drawCloud(cx - 4, cy + 6, TFT_LIGHTGREY);
  } else if (code == 3 || code == 45 || code == 48) {
    drawCloud(cx, cy, TFT_LIGHTGREY);
  } else if (code >= 95) {                           // thunder
    drawCloud(cx, cy - 8, TFT_DARKGREY);
    tft.fillTriangle(cx - 2, cy + 6, cx + 8, cy + 6, cx - 6, cy + 30, TFT_YELLOW);
  } else {                                           // rain / snow
    drawCloud(cx, cy - 8, TFT_LIGHTGREY);
    uint16_t drop = (code >= 71 && code <= 86 && !(code >= 80 && code <= 82)) ? TFT_WHITE : TFT_CYAN;
    for (int i = -1; i <= 1; i++) tft.drawWideLine(cx + i * 12, cy + 14, cx + i * 12 - 4, cy + 26, 3, drop, COL_PANEL);
  }
}

static bool fetchWeather() {
  char url[256];
  snprintf(url, sizeof(url),
           "http://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
           "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m"
           "&daily=temperature_2m_max,temperature_2m_min&forecast_days=1&timezone=auto",
           WEATHER_LAT, WEATHER_LON);
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(8000);
  if (!http.begin(url)) return false;
  int code = http.GET();
  Serial.printf("weather: HTTP %d\n", code);
  if (code != 200) { http.end(); return false; }

  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, http.getStream());
  http.end();
  if (e) return false;

  wx.temp     = doc["current"]["temperature_2m"] | 0.0f;
  wx.humidity = doc["current"]["relative_humidity_2m"] | 0;
  wx.code     = doc["current"]["weather_code"] | 0;
  wx.wind     = doc["current"]["wind_speed_10m"] | 0.0f;
  wx.tmax     = doc["daily"]["temperature_2m_max"][0] | 0.0f;
  wx.tmin     = doc["daily"]["temperature_2m_min"][0] | 0.0f;
  struct tm now;
  if (netGetTime(&now)) strftime(wx.updated, sizeof(wx.updated), CLOCK_24H ? "%H:%M" : "%I:%M", &now);
  else strcpy(wx.updated, "--");
  wx.valid = true;
  return true;
}

static void drawWeather() {
  tft.fillRoundRect(6, 198, 468, 116, 10, COL_PANEL);
  tft.setTextPadding(0);
  if (!wx.valid) {
    tft.setTextColor(COL_DIM);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(netOnline() ? "Weather not available, retrying soon" : "Offline - connect WiFi for weather",
                   SCREEN_W / 2, 256, 2);
    return;
  }
  // temperature
  char t[8];
  snprintf(t, sizeof(t), "%d", (int)roundf(wx.temp));
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(t, 24, 212, 6);
  int tx = 24 + tft.textWidth(t, 6) + 4;
  tft.drawCircle(tx + 5, 218, 5, TFT_WHITE);
  tft.drawCircle(tx + 5, 218, 4, TFT_WHITE);
  tft.drawString("C", tx + 13, 212, 4);

  tft.setTextColor(COL_DIM);
  tft.drawString(WEATHER_CITY, 24, 268, 2);
  char up[24];
  snprintf(up, sizeof(up), "Updated %s", wx.updated);
  tft.drawString(up, 24, 288, 2);

  // details
  tft.setTextColor(TFT_WHITE);
  tft.drawString(weatherText(wx.code), 160, 214, 4);
  char line[48];
  snprintf(line, sizeof(line), "Humidity %d%%    Wind %d km/h", wx.humidity, (int)roundf(wx.wind));
  tft.setTextColor(TFT_LIGHTGREY);
  tft.drawString(line, 160, 252, 2);
  snprintf(line, sizeof(line), "High %dC    Low %dC", (int)roundf(wx.tmax), (int)roundf(wx.tmin));
  tft.drawString(line, 160, 276, 2);

  drawWeatherIcon(wx.code, 420, 254);
}

static void drawClock(bool force) {
  struct tm now;
  bool synced = netGetTime(&now);
  if (!synced) {
    if (force || lastSynced) {
      tft.fillRect(0, TOPBAR_H, SCREEN_W, 150, COL_BG);
      tft.setTextColor(COL_DIM);
      tft.setTextDatum(MC_DATUM);
      tft.setTextPadding(0);
      tft.drawString(netOnline() ? "Getting time from internet..." : "Offline - connect WiFi for time",
                     SCREEN_W / 2, 120, 4);
    }
    lastSynced = false;
    return;
  }
  if (!lastSynced) { tft.fillRect(0, TOPBAR_H, SCREEN_W, 150, COL_BG); force = true; }
  lastSynced = true;
  if (!force && now.tm_sec == lastSecond) return;
  lastSecond = now.tm_sec;

  char hm[8], sec[4], ampm[4];
  int hour = now.tm_hour;
  if (CLOCK_24H) snprintf(hm, sizeof(hm), "%02d:%02d", hour, now.tm_min);
  else           snprintf(hm, sizeof(hm), "%d:%02d", (hour % 12 == 0) ? 12 : hour % 12, now.tm_min);
  snprintf(sec, sizeof(sec), "%02d", now.tm_sec);
  strcpy(ampm, hour < 12 ? "AM" : "PM");

  // big time, seven-segment font at double size
  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN, COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(tft.textWidth("88:88", 7));
  tft.drawString(hm, 205, 108, 7);
  tft.setTextSize(1);

  tft.setTextColor(TFT_WHITE, COL_BG);
  tft.setTextPadding(tft.textWidth("88", 4) + 4);
  if (!CLOCK_24H) tft.drawString(ampm, 425, 82, 4);
  tft.drawString(sec, 425, 130, 4);

  if (force || now.tm_mday != lastDay) {
    lastDay = now.tm_mday;
    char date[40];
    strftime(date, sizeof(date), "%A, %d %B %Y", &now);
    tft.setTextPadding(SCREEN_W - 20);
    tft.setTextColor(TFT_WHITE, COL_BG);
    tft.drawString(date, SCREEN_W / 2, 178, 4);
  }
  tft.setTextPadding(0);
}

static void clockEnter() {
  drawTopBar("Clock & Weather");
  lastSecond = -1; lastDay = -1; lastSynced = false;
  drawClock(true);
  if (netOnline() && (!wx.valid || millis() - lastFetch > WEATHER_REFRESH_MIN * 60000UL)) {
    tft.fillRoundRect(6, 198, 468, 116, 10, COL_PANEL);
    tft.setTextColor(COL_DIM);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Loading weather...", SCREEN_W / 2, 256, 2);
    fetchWeather();
    lastFetch = millis();
  }
  drawWeather();
}

static void clockUpdate() {
  drawClock(false);
  uint32_t interval = wx.valid ? WEATHER_REFRESH_MIN * 60000UL : 60000UL;  // retry sooner after a failure
  if (netOnline() && millis() - lastFetch > interval) {
    fetchWeather();
    lastFetch = millis();
    drawWeather();
  }
}

static void clockTouch(TouchEvent ev, int16_t x, int16_t y) {
  if (ev == TOUCH_DOWN && BACK_BTN.contains(x, y)) { clickSound(); goToApp(&menuApp); }
}

App clockApp = {clockEnter, nullptr, clockUpdate, clockTouch};
