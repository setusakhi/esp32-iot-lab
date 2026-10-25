#include "mic.h"
#include "gadget_config.h"
#include <driver/adc.h>

void micBegin() {
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(MIC_ADC_CHANNEL, ADC_ATTEN_DB_12);   // 0..~3.1 V
}

// 16-bit sample -> 8-bit mu-law (ITU-T G.711)
static uint8_t toUlaw(int sample) {
  const int BIAS = 0x84, CLIP = 32635;
  int sign = 0;
  if (sample < 0) { sample = -sample; sign = 0x80; }
  if (sample > CLIP) sample = CLIP;
  sample += BIAS;
  int exponent = 7;
  for (int mask = 0x4000; !(sample & mask) && exponent > 0; mask >>= 1) exponent--;
  int mantissa = (sample >> (exponent + 3)) & 0x0F;
  return ~(sign | (exponent << 4) | mantissa);
}

void micRecord(uint8_t *ulaw, int count) {
  const uint32_t period = 1000000UL / MIC_SAMPLE_RATE;
  float prevIn = adc1_get_raw(MIC_ADC_CHANNEL) * 8.0f, prevOut = 0;
  uint32_t next = micros();
  for (int i = 0; i < count; i++) {
    while ((int32_t)(micros() - next) < 0) {}
    next += period;
    // x8, not x16: at x16 normal speech clipped (measured on saved recordings)
    float x = adc1_get_raw(MIC_ADC_CHANNEL) * 8.0f;
    float y = x - prevIn + 0.995f * prevOut;           // remove DC offset
    prevIn = x;
    prevOut = y;
    ulaw[i] = toUlaw((int)constrain(y, -32767.0f, 32767.0f));
  }
}
