#pragma once
#include <Arduino.h>

// Physical controls next to the screen (pins in gadget_config.h):
//  - TTP223 touch button: tap = talk to the gadget, long press = stop and go home
//  - rotary encoder: turn = volume, press = pause/play (radio) or home

enum TouchPress { PRESS_NONE, PRESS_TAP, PRESS_LONG };

void       inputBegin();          // call once in setup()
void       inputService();        // call every loop
TouchPress touchTake();           // a tap or long press since the last call
int        knobTakeSteps();       // clicks turned since the last call (+ = clockwise)
bool       knobTakePress();       // knob pressed since the last call
