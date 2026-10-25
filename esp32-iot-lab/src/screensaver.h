#pragma once
#include <Arduino.h>

// Screensaver: after SCREENSAVER_MINUTES without input, a night-sky screen with a
// big clock, the date and (when the radio plays) the station and dancing bars.
// It is drawn on top of the current screen, so the radio keeps playing.
// main.cpp starts and stops it; any touch, the touch button or the knob wakes it.

void saverStart();
void saverStop();
bool saverActive();
void saverUpdate();          // call every loop while active
