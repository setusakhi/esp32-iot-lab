// Internet radio with three tabs: Hindi stations (live list from radio-browser.info),
// AIR & News, and Bhakti (fixed lists in radio_lists.h)
#include "apps.h"
#include "radio.h"
#include "radio_lists.h"
#include "net.h"
#include "gadget_config.h"
#include <Preferences.h>

static const int PER_PAGE = 6;
static const Rect NOW_BOX  = {6, 218, 468, 44};
static const Rect PREV_BTN = {6, 268, 56, 46};
static const Rect NEXT_BTN = {66, 268, 56, 46};
static const Rect STOP_BTN = {128, 268, 110, 46};
static const Rect VOL_DOWN = {258, 268, 60, 46};
static const Rect VOL_UP   = {414, 268, 60, 46};

enum { TAB_HINDI, TAB_AIR, TAB_BHAKTI, TAB_COUNT };
static const char *TAB_LABEL[TAB_COUNT] = {"Hindi", "AIR & News", "Bhakti"};
static const Rect TAB_RECT[TAB_COUNT] = {{186, 6, 80, 32}, {270, 6, 116, 32}, {390, 6, 84, 32}};

static std::vector<Station> lists[TAB_COUNT];
static int  tab = TAB_HINDI;
static int  page = 0;
static int  playingTab = -1, playing = -1;   // station now playing (any tab)
static uint8_t volume = RADIO_DEFAULT_VOLUME;
static char songTitle[96] = "";
static RadioStatus shownStatus = RADIO_IDLE;
static bool ready = false;          // radio running and the current tab's list loaded
static bool autoPlay = false;       // start the last station on enter (voice command)
static String requested;            // station asked for by voice ("Vividh Bharati")

void radioAutoPlay() { autoPlay = true; }
void radioPlayStation(const char *name) { requested = name; }

static std::vector<Station> &current() { return lists[tab]; }

template <size_t N>
static void fillFixed(std::vector<Station> &out, const FixedStation (&src)[N]) {
  if (!out.empty()) return;
  for (const FixedStation &s : src) out.push_back({s.name, s.url, 0});
}

static void saveSettings() {
  Preferences p;
  p.begin("settings", false);
  p.putUChar("radioVol", volume);
  p.putUChar("radioTab", playing >= 0 ? playingTab : tab);   // reopen where the music is
  if (playing >= 0) p.putString("radioLast", lists[playingTab][playing].name);
  p.end();
}

static void playStation(int t, int i) {
  playingTab = t;
  playing = i;
  songTitle[0] = 0;
  Serial.printf("Play %s : %s\n", lists[t][i].name.c_str(), lists[t][i].url.c_str());
  radioPlay(lists[t][i].url.c_str());
  saveSettings();
}

static Rect stationRect(int slot) {
  return {(int16_t)(6 + (slot % 2) * 237), (int16_t)(50 + (slot / 2) * 56), 231, 50};
}

static int pageCount() { return (current().size() + PER_PAGE - 1) / PER_PAGE; }

static void drawTabs() {
  for (int t = 0; t < TAB_COUNT; t++)
    drawButton(TAB_RECT[t], TAB_LABEL[t], t == tab ? 0x256C : COL_KEY, TFT_WHITE, 2);
}

static void drawStations() {
  tft.fillRect(0, 48, SCREEN_W, 168, COL_BG);
  for (int slot = 0; slot < PER_PAGE; slot++) {
    int i = page * PER_PAGE + slot;
    if (i >= (int)current().size()) break;
    bool on = (tab == playingTab && i == playing);
    drawButton(stationRect(slot), current()[i].name.c_str(), on ? 0x256C : 0x3186, TFT_WHITE, 2);
  }
}

static void drawNowPlaying() {
  tft.fillRoundRect(NOW_BOX.x, NOW_BOX.y, NOW_BOX.w, NOW_BOX.h, 8, COL_PANEL);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  RadioStatus st = radioStatus();
  shownStatus = st;
  String name = playing >= 0 ? lists[playingTab][playing].name : String("");
  if (playing < 0) st = RADIO_IDLE;
  String line1, line2;
  uint16_t col = TFT_WHITE;
  switch (st) {
    case RADIO_IDLE:       line1 = "Tap a station to play"; col = COL_DIM; break;
    case RADIO_CONNECTING: line1 = "Connecting to " + name + "..."; col = TFT_YELLOW; break;
    case RADIO_PLAYING:    line1 = "Playing: " + name; line2 = songTitle; col = TFT_GREEN; break;
    case RADIO_ERROR:      line1 = "This station is not working, try another"; col = TFT_ORANGE; break;
  }
  tft.setTextColor(col);
  tft.drawString(line1, 14, 223, 2);
  if (line2.length()) {
    if (line2.length() > 55) line2 = line2.substring(0, 55);
    tft.setTextColor(TFT_LIGHTGREY);
    tft.drawString(line2, 14, 242, 2);
  }
}

static void drawVolume() {
  tft.fillRect(320, 268, 92, 46, COL_BG);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(String(volume), 366, 291, 4);
}

static void drawControls() {
  drawButton(PREV_BTN, "<", COL_KEY);
  drawButton(NEXT_BTN, ">", COL_KEY);
  drawButton(STOP_BTN, "Stop", TFT_RED);
  drawButton(VOL_DOWN, "-", COL_KEY);
  drawButton(VOL_UP, "+", COL_KEY);
  drawVolume();
}

static void centerMessage(const char *msg, uint16_t col) {
  tft.fillRect(0, 48, SCREEN_W, SCREEN_H - 48, COL_BG);
  tft.setTextColor(col);
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(0);
  tft.drawString(msg, SCREEN_W / 2, 170, 4);
}

// Makes sure the current tab's list is there (the Hindi one comes from the internet)
static bool loadTab() {
  fillFixed(lists[TAB_AIR], AIR_NEWS_STATIONS);
  fillFixed(lists[TAB_BHAKTI], BHAKTI_STATIONS);
  if (tab == TAB_HINDI && lists[TAB_HINDI].empty()) {
    centerMessage("Loading Hindi stations...", TFT_YELLOW);
    if (!stationsFetch(lists[TAB_HINDI])) {
      centerMessage("Could not load stations. Tap to retry", TFT_ORANGE);
      return false;
    }
  }
  return true;
}

static void drawAll() {
  tft.fillRect(0, 48, SCREEN_W, SCREEN_H - 48, COL_BG);
  drawStations();
  drawNowPlaying();
  drawControls();
}

// Finds a station by name in the loaded lists and switches to its tab
static bool findStation(const String &name, int &t, int &i) {
  for (t = 0; t < TAB_COUNT; t++)
    for (i = 0; i < (int)lists[t].size(); i++)
      if (lists[t][i].name.equalsIgnoreCase(name)) return true;
  return false;
}

static void radioEnter() {
  drawTopBar("Radio");
  ready = false;
  Preferences p;
  p.begin("settings", true);
  volume = p.getUChar("radioVol", RADIO_DEFAULT_VOLUME);
  tab = p.getUChar("radioTab", TAB_HINDI) % TAB_COUNT;
  String last = p.getString("radioLast", "");
  p.end();
  drawTabs();

  if (!netOnline()) { centerMessage("Offline - use WiFi Setup first", TFT_ORANGE); return; }

  // A voice request names the station; "play radio" means the last one played
  String want = requested.length() ? requested : (autoPlay ? last : String());
  int t, i;
  fillFixed(lists[TAB_AIR], AIR_NEWS_STATIONS);
  fillFixed(lists[TAB_BHAKTI], BHAKTI_STATIONS);
  if (want.length() && findStation(want, t, i)) tab = t;
  drawTabs();
  if (!loadTab()) return;

  playing = playingTab = -1;
  songTitle[0] = 0;
  radioStart();
  radioSetVolume(volume);
  if (want.length() || autoPlay) {
    if (!findStation(want, t, i)) { t = tab; i = 0; }   // not found: first station of the tab
    if (!lists[t].empty()) {
      tab = t;
      page = i / PER_PAGE;
      playStation(t, i);
      drawTabs();
    }
  }
  requested = "";
  autoPlay = false;
  if (page >= pageCount()) page = 0;
  drawAll();
  ready = true;
}

static void radioExit() {
  if (radioActive()) {
    centerMessage("Stopping radio...", COL_DIM);
    radioStop();
  }
  saveSettings();
}

static void radioUpdate() {
  if (!ready) return;
  bool titleNew = radioTakeTitle(songTitle, sizeof(songTitle));
  if (titleNew || radioStatus() != shownStatus) drawNowPlaying();
}

static void radioTouch(TouchEvent ev, int16_t x, int16_t y) {
  if (ev != TOUCH_DOWN) return;
  if (BACK_BTN.contains(x, y)) { goToApp(&menuApp); return; }
  for (int t = 0; t < TAB_COUNT; t++) {
    if (TAB_RECT[t].contains(x, y) && t != tab) {
      clickSound();
      tab = t;
      page = 0;
      drawTabs();
      if (!radioActive()) { radioEnter(); return; }      // first load failed earlier
      ready = loadTab();
      if (ready) drawAll();
      return;
    }
  }
  if (!ready) {                       // retry loading
    if (netOnline()) { if (radioActive()) { ready = loadTab(); if (ready) drawAll(); } else radioEnter(); }
    return;
  }
  for (int slot = 0; slot < PER_PAGE; slot++) {
    int i = page * PER_PAGE + slot;
    if (i < (int)current().size() && stationRect(slot).contains(x, y)) {
      playStation(tab, i);
      drawStations();
      drawNowPlaying();
      return;
    }
  }
  if (PREV_BTN.contains(x, y) && pageCount() > 1) { page = (page + pageCount() - 1) % pageCount(); drawStations(); }
  else if (NEXT_BTN.contains(x, y) && pageCount() > 1) { page = (page + 1) % pageCount(); drawStations(); }
  else if (STOP_BTN.contains(x, y)) { radioPause(); playing = playingTab = -1; drawStations(); drawNowPlaying(); }
  else if (VOL_UP.contains(x, y) && volume < 21) { radioSetVolume(++volume); drawVolume(); }
  else if (VOL_DOWN.contains(x, y) && volume > 0) { radioSetVolume(--volume); drawVolume(); }
}

App radioApp = {radioEnter, radioExit, radioUpdate, radioTouch};
