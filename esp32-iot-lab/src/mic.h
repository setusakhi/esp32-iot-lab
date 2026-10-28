#pragma once
#include <Arduino.h>

// Analog microphone (MAX9814) on an ADC pin, see gadget_config.h.
// Recording blocks until done; samples are mono at MIC_SAMPLE_RATE with the
// DC offset removed, stored as 8-bit mu-law (the telephone voice format):
// half the memory of 16-bit, so the radio still has room for its secure
// connection. The voice server turns it back into 16-bit before Whisper.

void micBegin();                          // call once in setup()
void micRecord(uint8_t *ulaw, int count); // records count samples

// Records up to maxCount samples but stops about 0.8 s after you stop talking,
// so short commands stay quick and long questions are not cut off.
// Returns how many samples were recorded.
int micRecordSpeech(uint8_t *ulaw, int maxCount);
