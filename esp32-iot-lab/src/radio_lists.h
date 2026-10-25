#pragma once
// Fixed station lists for the radio tabs "AIR & News" and "Bhakti".
// (The "Hindi" tab loads its list live from radio-browser.info.)
// All checked as working in October 2026. AIR streams are HLS playlists of AAC in
// MPEG-TS segments, which the audio library plays. If a station stops working,
// find a new address at radio-browser.info and replace it here.
// Names must match the voice server's STATIONS list (voice-server/server.py).

struct FixedStation {
  const char *name;
  const char *url;
};

static const FixedStation AIR_NEWS_STATIONS[] = {
  {"Vividh Bharati", "https://air.pc.cdn.bitgravity.com/air/live/pbaudio001/playlist.m3u8"},
  {"AIR Alwar",      "https://air.pc.cdn.bitgravity.com/air/live/pbaudio153/playlist.m3u8"},
  {"Jaipur Radio",   "https://streamasiacdn.atc-labs.com/jaipurradio.aac"},
  {"Hindi News",     "https://replaynewshi.ice.infomaniak.ch/replaynewshi-128.mp3"},
};

// Used for the "Hindi" tab only when radio-browser.info can't be reached and no list
// was saved yet (its servers, or the internet provider, sometimes block it).
static const FixedStation DEFAULT_HINDI_STATIONS[] = {
  {"Hindi Gold Radio",      "https://azuracast.vibesounds.in:8010/radio.mp3"},
  {"Mirchi Top 20",         "https://drive.uber.radio/uber/bollywoodnow/icecast.audio"},
  {"AR Radio",              "https://free.rcast.net/200660"},
  {"Lata Mangeshkar Radio", "https://stream.zeno.fm/87xam8pf7tzuv"},
};

static const FixedStation BHAKTI_STATIONS[] = {
  {"Golden Temple Kirtan", "https://live.sgpc.net:8443/;stream.mp3"},
  {"Gurbani Kirtan",       "https://gurbanikirtan.radioca.st/start.mp3"},
  {"SVBC TTD Tirupati",    "https://radio.mslivecdn.com:6278/stream"},
  {"Shaiva Lahari",        "https://24293.live.streamtheworld.com/SP_R2925215_SC"},
  {"Mahakatha Radio",      "https://mahakatharadio.com/api/station/live.mp3"},
  {"Radio Sai",            "http://stream.radiosai.net:8002/"},
};
