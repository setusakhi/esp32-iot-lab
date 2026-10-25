#pragma once
#include <Arduino.h>

// Simple tone engine for the MAX98357A (beeps, piano, alarms).
// Sound plays in a background task, so the screen never freezes.
// The internet radio needs the same amplifier, so it calls audioEnd()
// to borrow it and audioResume() to give it back.

void     audioBegin();                               // call once in setup()
void     audioPlayNote(uint16_t freqHz, uint16_t ms); // freqHz = 0 means silence (rest)
void     audioStop();                                // stop current sound and clear the queue
void     audioSetVolume(uint8_t level);              // 0..10
uint8_t  audioGetVolume();
void     audioEnd();                                 // release the amplifier (for the radio)
void     audioResume();                              // take the amplifier back
// Plays raw 16-bit mono PCM at AUDIO_SAMPLE_RATE straight from a stream
// (spoken voice replies). Blocks until done; uses the tone volume.
void     audioPlayPcm(Stream &in, size_t bytes);
uint32_t audioBytesSent();                           // diagnostics: total bytes sent to I2S
