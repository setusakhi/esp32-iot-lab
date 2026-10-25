#include "radio.h"
#include "radio_lists.h"
#include "gadget_config.h"
#include "tones.h"
#include <Audio.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// ---------------- Stream player task ----------------

enum CmdType { CMD_PLAY, CMD_PAUSE, CMD_VOLUME, CMD_QUIT };
struct RadioCmd {
  CmdType type;
  uint8_t volume;
  char    url[256];
};

static QueueHandle_t       cmdQueue = nullptr;
static SemaphoreHandle_t   doneSem = nullptr;
static volatile bool       active = false;
static volatile RadioStatus status = RADIO_IDLE;
static volatile uint8_t    currentVolume = RADIO_DEFAULT_VOLUME;

static portMUX_TYPE titleLock = portMUX_INITIALIZER_UNLOCKED;
static char         titleBuf[96] = "";
static volatile bool titleChanged = false;

static void setTitle(const char *t) {
  portENTER_CRITICAL(&titleLock);
  size_t j = 0;
  for (size_t i = 0; t[i] && j < sizeof(titleBuf) - 1; i++) {
    char c = t[i];
    if (c >= 32 && c < 127) titleBuf[j++] = c;   // the screen font is ASCII only
  }
  titleBuf[j] = 0;
  titleChanged = true;
  portEXIT_CRITICAL(&titleLock);
}

// Callbacks from the Audio library (they run inside the radio task)
void audio_showstreamtitle(const char *info) { setTitle(info); }
void audio_info(const char *info) { Serial.printf("radio: %s\n", info); }

static void radioTask(void *) {
  Audio *player = new Audio();   // installs the I2S driver
  player->setPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);
  player->setVolume(currentVolume);

  RadioCmd cmd;
  bool quit = false;
  while (!quit) {
    while (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE) {
      switch (cmd.type) {
        case CMD_PLAY:
          status = RADIO_CONNECTING;
          setTitle("");
          status = player->connecttohost(cmd.url) ? RADIO_PLAYING : RADIO_ERROR;
          break;
        case CMD_PAUSE:
          player->stopSong();
          status = RADIO_IDLE;
          break;
        case CMD_VOLUME:
          player->setVolume(cmd.volume);
          break;
        case CMD_QUIT:
          quit = true;
          break;
      }
    }
    player->loop();
    if (status == RADIO_PLAYING && !player->isRunning()) status = RADIO_ERROR;  // stream dropped
    vTaskDelay(1);
  }

  delete player;                 // stops the stream and releases the I2S driver
  status = RADIO_IDLE;
  xSemaphoreGive(doneSem);
  vTaskDelete(nullptr);
}

static void sendCmd(CmdType type, const char *url = nullptr, uint8_t vol = 0) {
  if (!active) return;
  RadioCmd c = {};
  c.type = type;
  c.volume = vol;
  if (url) strlcpy(c.url, url, sizeof(c.url));
  xQueueSend(cmdQueue, &c, pdMS_TO_TICKS(100));
}

void radioStart() {
  if (active) return;
  if (!cmdQueue) cmdQueue = xQueueCreate(4, sizeof(RadioCmd));
  if (!doneSem)  doneSem = xSemaphoreCreateBinary();
  xQueueReset(cmdQueue);
  audioEnd();                    // borrow the amplifier
  status = RADIO_IDLE;
  active = true;
  xTaskCreatePinnedToCore(radioTask, "radio", 10240, nullptr, 3, nullptr, 0);
}

void radioStop() {
  if (!active) return;
  sendCmd(CMD_QUIT);
  xSemaphoreTake(doneSem, pdMS_TO_TICKS(10000));   // a connect in progress can take a few seconds
  active = false;
  audioResume();                 // give the amplifier back to beeps and piano
}

bool radioActive() { return active; }
void radioPlay(const char *url) { sendCmd(CMD_PLAY, url); }
void radioPause() { sendCmd(CMD_PAUSE); }
void radioSetVolume(uint8_t vol) {
  currentVolume = vol > 21 ? 21 : vol;
  sendCmd(CMD_VOLUME, nullptr, currentVolume);
}
RadioStatus radioStatus() { return status; }

bool radioTakeTitle(char *out, size_t len) {
  if (!titleChanged) return false;
  portENTER_CRITICAL(&titleLock);
  strlcpy(out, titleBuf, len);
  titleChanged = false;
  portEXIT_CRITICAL(&titleLock);
  return true;
}

// ---------------- Station list ----------------

static String cleanName(const String &in) {
  String s;
  bool lastSpace = true;
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c < 32 || c >= 127) continue;          // drop non-ASCII (e.g. Devanagari)
    if (c == ' ' && lastSpace) continue;
    s += c;
    lastSpace = (c == ' ');
  }
  s.trim();
  if (s.length() > 28) s = s.substring(0, 28);
  return s;
}

// The last list that loaded is kept in flash ("name<TAB>bitrate<TAB>url" lines), so the
// Hindi tab still works when radio-browser.info is down (in October 2026 five of its six
// mirrors refused connections for a while).
static void saveStations(const std::vector<Station> &list) {
  String blob;
  for (const Station &s : list) blob += s.name + "\t" + String(s.bitrate) + "\t" + s.url + "\n";
  Preferences p;
  p.begin("stations", false);
  p.putBytes("hindi", blob.c_str(), blob.length());
  p.end();
}

static bool loadSavedStations(std::vector<Station> &out) {
  Preferences p;
  p.begin("stations", true);
  size_t len = p.getBytesLength("hindi");
  String blob;
  if (len) {
    std::vector<char> buf(len + 1, 0);
    p.getBytes("hindi", buf.data(), len);
    blob = buf.data();
  }
  p.end();
  out.clear();
  int start = 0;
  while (start < (int)blob.length()) {
    int end = blob.indexOf('\n', start);
    if (end < 0) end = blob.length();
    String line = blob.substring(start, end);
    int t1 = line.indexOf('\t'), t2 = line.indexOf('\t', t1 + 1);
    if (t1 > 0 && t2 > t1) out.push_back({line.substring(0, t1), line.substring(t2 + 1), line.substring(t1 + 1, t2).toInt()});
    start = end + 1;
  }
  if (out.empty()) {                         // nothing saved yet: the built-in few
    for (const FixedStation &s : DEFAULT_HINDI_STATIONS) out.push_back({s.name, s.url, 0});
    Serial.printf("Using the built-in station list (%d stations)\n", (int)out.size());
  } else {
    Serial.printf("Using the saved station list (%d stations)\n", (int)out.size());
  }
  return !out.empty();
}

bool stationsFetch(std::vector<Station> &out) {
  static const char *hosts[] = {
    "de2.api.radio-browser.info",
    "fi1.api.radio-browser.info",
    "all.api.radio-browser.info",
    "de1.api.radio-browser.info",
    "nl1.api.radio-browser.info",
    "at1.api.radio-browser.info",
  };

  JsonDocument filter;
  filter[0]["name"] = true;
  filter[0]["url_resolved"] = true;
  filter[0]["codec"] = true;
  filter[0]["bitrate"] = true;
  filter[0]["hls"] = true;

  for (const char *host : hosts) {
    String url = String("https://") + host +
                 "/json/stations/search?language=" RADIO_LANGUAGE
                 "&hidebroken=true&order=clickcount&reverse=true&limit=60";
    WiFiClientSecure client;
    client.setInsecure();          // public station list, no private data
    HTTPClient http;
    http.useHTTP10(true);          // simple stream for the JSON parser
    http.setConnectTimeout(4000);  // a dead mirror shouldn't hold us up long
    http.setTimeout(10000);
    http.setUserAgent("IoTLab-Radio/1.0");
    if (!http.begin(client, url)) continue;

    int code = http.GET();
    Serial.printf("stations from %s: HTTP %d\n", host, code);
    if (code != 200) { http.end(); continue; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(),
                                               DeserializationOption::Filter(filter));
    http.end();
    if (err) { Serial.printf("JSON error: %s\n", err.c_str()); continue; }

    out.clear();
    for (JsonObject s : doc.as<JsonArray>()) {
      if ((s["hls"] | 0) == 1) continue;                 // skip HLS playlists
      String codec = s["codec"] | "";
      codec.toUpperCase();
      if (codec != "MP3" && !codec.startsWith("AAC")) continue;
      int br = s["bitrate"] | 0;
      if (br > 160) continue;                            // keep it light for WiFi
      String u = s["url_resolved"] | "";
      if (u.length() == 0 || u.length() > 250) continue;
      String name = cleanName(s["name"] | "");
      if (name.length() == 0) continue;
      bool dup = false;
      for (auto &st : out) if (st.name.equalsIgnoreCase(name)) { dup = true; break; }
      if (dup) continue;
      out.push_back({name, u, br});
      if (out.size() >= RADIO_MAX_STATIONS) break;
    }
    if (!out.empty()) { saveStations(out); return true; }
  }
  return loadSavedStations(out);   // directory unreachable: use the last list we had
}
