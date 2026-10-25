#pragma once
#include "ui.h"

// Every screen is an "app" with the same four functions.
// Adding a new app = write these four functions and add a tile to the menu.
struct App {
  void (*enter)();                                  // draw the screen
  void (*exit)();                                   // clean up when leaving
  void (*update)();                                 // called continuously
  void (*touch)(TouchEvent ev, int16_t x, int16_t y);
};

extern App menuApp, radioApp, calcApp, clockApp, timerApp, pianoApp, voiceApp;

void goToApp(App *app);     // switch screen (happens after the current loop)
App *currentApp();

// The timer keeps running and rings on any screen.
void timerService();
bool timerAlarmActive();
void timerAlarmStop();
void timerStartSeconds(uint32_t secs);   // set and start (used by voice commands)

// Voice command "radio": the radio screen starts the last station by itself.
void radioAutoPlay();
// Voice command "play Vividh Bharati": the radio screen opens that station's tab and plays it.
void radioPlayStation(const char *name);
