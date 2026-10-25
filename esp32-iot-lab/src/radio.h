#pragma once
#include <Arduino.h>
#include <vector>

// Internet radio engine. Runs the stream decoder in its own task,
// so drawing on the screen never interrupts the music.

enum RadioStatus { RADIO_IDLE, RADIO_CONNECTING, RADIO_PLAYING, RADIO_ERROR };

void        radioStart();                 // borrow the amplifier from the tone engine
void        radioStop();                  // stop everything and give the amplifier back
bool        radioActive();
void        radioPlay(const char *url);
void        radioPause();                 // stop the stream, stay in radio mode
void        radioSetVolume(uint8_t vol);  // 0..21
RadioStatus radioStatus();
bool        radioTakeTitle(char *out, size_t len);  // true when the song title changed

// Station list from radio-browser.info (a free, community station directory)
struct Station {
  String name;
  String url;
  int    bitrate;
};
bool stationsFetch(std::vector<Station> &out);
