#pragma once
#include <time.h>

void netBegin();                    // connect to saved WiFi (or open setup hotspot) + start clock sync
void netSetupPortal();              // open the WiFi setup hotspot on demand
bool netOnline();
bool netGetTime(struct tm *out);    // false until the clock is synced from the internet
