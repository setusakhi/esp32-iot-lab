#include "mic.h"
#include "gadget_config.h"
#include <driver/adc.h>

void micBegin() {
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(MIC_ADC_CHANNEL, ADC_ATTEN_DB_12);   // 0..~3.1 V
}

void micRecord(int16_t *buf, int count) {
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
    buf[i] = (int16_t)constrain(y, -32767.0f, 32767.0f);
  }
}
