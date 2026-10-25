#pragma once
#include <Arduino.h>

// Analog microphone (MAX9814) on an ADC pin, see gadget_config.h.
// Recording blocks until done; samples are 16-bit mono at MIC_SAMPLE_RATE
// with the DC offset removed, ready to send to the voice server.

void micBegin();                          // call once in setup()
void micRecord(int16_t *buf, int count);  // records count samples
