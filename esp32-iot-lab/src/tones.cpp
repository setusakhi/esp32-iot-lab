#include "tones.h"
#include "gadget_config.h"
#include <driver/i2s.h>

struct Note {
  uint16_t freq;
  uint16_t ms;
};

static QueueHandle_t     noteQueue = nullptr;
static volatile bool     abortNote = false;
static volatile bool     paused = false;
static volatile uint8_t  volume = DEFAULT_VOLUME;
static volatile uint32_t bytesSent = 0;

static const int CHUNK = 256;   // samples written to I2S at a time

// Background task: takes notes from the queue and turns them into sound.
static void audioTask(void *) {
  static int16_t buf[CHUNK * 2];   // stereo: left + right
  float phase = 0;
  Note n;

  for (;;) {
    if (xQueueReceive(noteQueue, &n, portMAX_DELAY) != pdTRUE) continue;
    if (paused) continue;
    abortNote = false;

    const uint32_t total   = (uint32_t)AUDIO_SAMPLE_RATE * n.ms / 1000;
    const uint32_t attack  = AUDIO_SAMPLE_RATE / 200;  // 5 ms fade-in (avoids clicks)
    const uint32_t release = AUDIO_SAMPLE_RATE / 40;   // 25 ms fade-out
    const float step = 2.0f * PI * n.freq / AUDIO_SAMPLE_RATE;
    const float amp  = 9000.0f * volume / 10.0f;       // keep headroom, the amp is loud

    uint32_t done = 0;
    while (done < total && !abortNote && !paused) {
      uint32_t count = min((uint32_t)CHUNK, total - done);
      for (uint32_t k = 0; k < count; k++) {
        uint32_t i = done + k;
        int16_t s = 0;
        if (n.freq > 0) {
          float env = 1.0f;
          if (i < attack)                      env = (float)i / attack;
          else if (total > release && i > total - release) env = (float)(total - i) / release;
          s = (int16_t)(sinf(phase) * amp * env);
          phase += step;
          if (phase > 2.0f * PI) phase -= 2.0f * PI;
        }
        buf[2 * k] = s;       // left
        buf[2 * k + 1] = s;   // right (MAX98357 mixes L+R by default)
      }
      size_t written = 0;
      i2s_write(I2S_NUM_0, buf, count * 4, &written, portMAX_DELAY);
      bytesSent += written;
      done += count;
    }
  }
}

static void i2sInstall() {
  i2s_config_t cfg = {};
  cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate          = AUDIO_SAMPLE_RATE;
  cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count        = 8;
  cfg.dma_buf_len          = CHUNK;
  cfg.tx_desc_auto_clear   = true;   // output silence when nothing is playing

  i2s_pin_config_t pins = {};
  pins.mck_io_num   = I2S_PIN_NO_CHANGE;
  pins.bck_io_num   = I2S_BCLK_PIN;
  pins.ws_io_num    = I2S_LRC_PIN;
  pins.data_out_num = I2S_DOUT_PIN;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;

  esp_err_t e1 = i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
  esp_err_t e2 = i2s_set_pin(I2S_NUM_0, &pins);
  Serial.printf("I2S driver: %s, pins: %s (BCLK=%d LRC=%d DIN=%d)\n",
                esp_err_to_name(e1), esp_err_to_name(e2),
                I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

void audioBegin() {
  i2sInstall();
  noteQueue = xQueueCreate(32, sizeof(Note));
  xTaskCreatePinnedToCore(audioTask, "tones", 4096, nullptr, 2, nullptr, 0);
}

void audioPlayNote(uint16_t freqHz, uint16_t ms) {
  if (paused || !noteQueue) return;
  Note n = {freqHz, ms};
  xQueueSend(noteQueue, &n, 0);
}

void audioStop() {
  if (noteQueue) xQueueReset(noteQueue);
  abortNote = true;
}

void audioEnd() {
  if (paused) return;
  paused = true;
  audioStop();
  delay(60);                       // let the tone task finish its last chunk
  i2s_driver_uninstall(I2S_NUM_0);
}

void audioResume() {
  if (!paused) return;
  i2sInstall();
  paused = false;
}

void audioPlayPcm(Stream &in, size_t bytes) {
  if (paused) return;
  audioStop();
  delay(40);                       // let the tone task finish its last chunk

  static int16_t mono[CHUNK];
  static int16_t stereo[CHUNK * 2];
  const float gain = volume / 6.0f;   // speech is quieter than our tones
  while (bytes >= 2) {
    size_t got = in.readBytes((uint8_t *)mono, min(sizeof(mono), bytes & ~(size_t)1));
    if (got < 2) break;            // stream ended early
    int n = got / 2;
    for (int i = 0; i < n; i++) {
      int16_t s = (int16_t)constrain(mono[i] * gain, -32767.0f, 32767.0f);
      stereo[2 * i] = s;
      stereo[2 * i + 1] = s;
    }
    size_t written = 0;
    i2s_write(I2S_NUM_0, stereo, n * 4, &written, portMAX_DELAY);
    bytesSent += written;
    bytes -= got;
  }
}

void audioSetVolume(uint8_t level) { volume = level > 10 ? 10 : level; }
uint8_t audioGetVolume() { return volume; }
uint32_t audioBytesSent() { return bytesSent; }
