// ============================================================
//  Microphone test
//    env "mictest"        = INMP441 digital mic (I2S)
//    env "mictest-analog" = MAX9814 analog mic (ADC, MIC_ANALOG=1)
//  - Shows a live sound-level bar
//  - Tap RECORD (or press the BOOT button) to record 3 seconds,
//    then it plays the recording back through the MAX98357A
//  - INMP441: reads both I2S channels and uses the one that has
//    sound, so it works whichever way the L/R pin is wired
// ============================================================
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include <driver/i2s.h>
#include <driver/adc.h>
#include "gadget_config.h"

#ifndef MIC_ANALOG
#define MIC_ANALOG 0
#endif
#if MIC_ANALOG
#define MIC_NAME "MAX9814"
#else
#define MIC_NAME "INMP441"
#endif

TFT_eSPI tft;

#define MIC_PORT      I2S_NUM_1
#define SPK_PORT      I2S_NUM_0
#define BOOT_BTN      0
#define RECORD_SECS   3
#define FRAMES        256          // stereo frames read at a time (16 ms)

static const int RECORD_LEN = MIC_SAMPLE_RATE * RECORD_SECS;
static int16_t  *recording = nullptr;
static bool      haveTouch = false;

// Bar and button positions
static const int BAR_X = 20, BAR_Y = 110, BAR_W = 440, BAR_H = 40;
static const int BTN_X = 140, BTN_Y = 230, BTN_W = 200, BTN_H = 60;

// ---------------- Mic and speaker setup ----------------

#if MIC_ANALOG
static void micBegin() {
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(MIC_ADC_CHANNEL, ADC_ATTEN_DB_12);   // 0..~3.1 V
  Serial.printf("Analog mic on GPIO %d, resting value %d (expect about 1500)\n",
                MIC_ADC_PIN, adc1_get_raw(MIC_ADC_CHANNEL));
}
#else
static void micBegin() {
  i2s_config_t cfg = {};
  cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate          = MIC_SAMPLE_RATE;
  cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT;   // INMP441 sends 24 bits in a 32-bit slot
  cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;  // read both, pick the live one
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count        = 8;
  cfg.dma_buf_len          = FRAMES;

  i2s_pin_config_t pins = {};
  pins.mck_io_num   = I2S_PIN_NO_CHANGE;
  pins.bck_io_num   = MIC_SCK_PIN;
  pins.ws_io_num    = MIC_WS_PIN;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num  = MIC_SD_PIN;

  esp_err_t e1 = i2s_driver_install(MIC_PORT, &cfg, 0, nullptr);
  esp_err_t e2 = i2s_set_pin(MIC_PORT, &pins);
  Serial.printf("Mic I2S driver: %s, pins: %s (SCK=%d WS=%d SD=%d)\n",
                esp_err_to_name(e1), esp_err_to_name(e2),
                MIC_SCK_PIN, MIC_WS_PIN, MIC_SD_PIN);
}
#endif

static void speakerBegin() {
  i2s_config_t cfg = {};
  cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate          = MIC_SAMPLE_RATE;
  cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count        = 8;
  cfg.dma_buf_len          = FRAMES;
  cfg.tx_desc_auto_clear   = true;   // silence when nothing is playing

  i2s_pin_config_t pins = {};
  pins.mck_io_num   = I2S_PIN_NO_CHANGE;
  pins.bck_io_num   = I2S_BCLK_PIN;
  pins.ws_io_num    = I2S_LRC_PIN;
  pins.data_out_num = I2S_DOUT_PIN;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;

  i2s_driver_install(SPK_PORT, &cfg, 0, nullptr);
  i2s_set_pin(SPK_PORT, &pins);
  i2s_zero_dma_buffer(SPK_PORT);
}

// ---------------- Reading the mic ----------------

static int  liveChannel = 0;           // 0 = left, 1 = right
static float hpPrevIn = 0, hpPrevOut = 0;

static int16_t removeDc(float x) {
  float y = x - hpPrevIn + 0.995f * hpPrevOut;
  hpPrevIn = x;
  hpPrevOut = y;
  return (int16_t)constrain(y, -32767.0f, 32767.0f);
}

// Reads one block, fills out[] with FRAMES mono 16-bit samples
// (DC offset removed) and returns how many samples were read.
#if MIC_ANALOG
static int micRead(int16_t *out) {
  // Sample the ADC at MIC_SAMPLE_RATE, timed with micros()
  static uint32_t next = micros();
  const uint32_t period = 1000000UL / MIC_SAMPLE_RATE;
  int minRaw = 4095, maxRaw = 0;
  for (int i = 0; i < FRAMES; i++) {
    while ((int32_t)(micros() - next) < 0) {}
    next += period;
    int raw = adc1_get_raw(MIC_ADC_CHANNEL);
    minRaw = min(minRaw, raw);
    maxRaw = max(maxRaw, raw);
    out[i] = removeDc(raw * 16.0f);    // 12-bit ADC to 16-bit range
  }
  if ((int32_t)(micros() - next) > 100000) next = micros();   // catch up after a pause

  // Diagnostics: raw ADC range, twice a second
  static uint32_t lastRaw = 0;
  if (millis() - lastRaw > 500) {
    lastRaw = millis();
    Serial.printf("raw ADC min=%d max=%d\n", minRaw, maxRaw);
  }
  return FRAMES;
}
#else
static int micRead(int16_t *out) {
  static int32_t raw[FRAMES * 2];
  size_t bytes = 0;
  i2s_read(MIC_PORT, raw, sizeof(raw), &bytes, portMAX_DELAY);
  int frames = bytes / 8;

  // Pick the channel that carries sound (the other one reads 0)
  uint64_t energy[2] = {0, 0};
  for (int i = 0; i < frames; i++) {
    energy[0] += abs(raw[2 * i] >> 16);
    energy[1] += abs(raw[2 * i + 1] >> 16);
  }
  if (energy[0] != energy[1]) liveChannel = energy[1] > energy[0] ? 1 : 0;

  // Diagnostics: raw words from the mic, twice a second
  static uint32_t lastRaw = 0;
  if (millis() - lastRaw > 500 && frames >= 2) {
    lastRaw = millis();
    Serial.printf("raw L=%08lx R=%08lx  L=%08lx R=%08lx\n",
                  (unsigned long)raw[0], (unsigned long)raw[1],
                  (unsigned long)raw[2], (unsigned long)raw[3]);
  }

  for (int i = 0; i < frames; i++)
    out[i] = removeDc((float)(raw[2 * i + liveChannel] >> 14));   // 24-bit data, a little gain
  return frames;
}
#endif

// ---------------- Screen ----------------

static void drawStatus(const char *text, uint16_t color = TFT_WHITE) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextPadding(440);
  tft.drawString(text, 20, 180, 4);
  tft.setTextPadding(0);
}

static void drawRecordButton(uint16_t bg, const char *label) {
  tft.fillRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 10, bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, bg);
  tft.drawString(label, BTN_X + BTN_W / 2, BTN_Y + BTN_H / 2, 4);
}

static void drawLevel(float db, float peakDb) {
  // -60 dB (quiet room) .. 0 dB (very loud)
  int w    = constrain((int)((db + 60.0f) / 60.0f * BAR_W), 0, BAR_W);
  int peak = constrain((int)((peakDb + 60.0f) / 60.0f * BAR_W), 0, BAR_W - 3);
  uint16_t col = db > -10 ? TFT_RED : db > -25 ? TFT_YELLOW : TFT_GREEN;

  tft.fillRect(BAR_X, BAR_Y, w, BAR_H, col);
  tft.fillRect(BAR_X + w, BAR_Y, BAR_W - w, BAR_H, 0x18E3);
  tft.fillRect(BAR_X + peak, BAR_Y, 3, BAR_H, TFT_WHITE);

  char line[32];
  snprintf(line, sizeof(line), "Level: %5.1f dB  (%s)   ", db, MIC_ANALOG ? "A" : liveChannel ? "R" : "L");
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(line, 20, 70, 4);
}

static void loadTouchCalibration() {
  uint16_t cal[5];
  Preferences prefs;
  prefs.begin("touch", true);
  haveTouch = prefs.isKey("cal") && prefs.getBytesLength("cal") == sizeof(cal);
  if (haveTouch) prefs.getBytes("cal", cal, sizeof(cal));
  prefs.end();
  if (haveTouch) tft.setTouch(cal);
  Serial.println(haveTouch ? "Touch calibration loaded" : "No touch calibration, use the BOOT button");
}

static bool recordPressed() {
  if (digitalRead(BOOT_BTN) == LOW) return true;
  uint16_t x, y;
  return haveTouch && tft.getTouch(&x, &y) &&
         x >= BTN_X && x < BTN_X + BTN_W && y >= BTN_Y && y < BTN_Y + BTN_H;
}

// ---------------- Record and play back ----------------

static void recordAndPlay() {
  drawRecordButton(TFT_RED, "Recording...");
  drawStatus("Speak now!", TFT_YELLOW);

  int16_t block[FRAMES];
  int filled = 0, peak = 1;
  while (filled < RECORD_LEN) {
    int n = min(micRead(block), RECORD_LEN - filled);
    for (int i = 0; i < n; i++) {
      recording[filled + i] = block[i];
      peak = max(peak, abs((int)block[i]));
    }
    filled += n;
  }

  // Make the playback loud enough, but not more than 8x
  float gain = min(20000.0f / peak, 8.0f);
  Serial.printf("Recorded %d samples, peak %d, playback gain %.1fx\n", filled, peak, gain);

  drawRecordButton(0x03E0, "Playing...");
  drawStatus("Listen to your recording", TFT_GREEN);

  int16_t out[FRAMES * 2];
  for (int pos = 0; pos < filled; pos += FRAMES) {
    int n = min(FRAMES, filled - pos);
    for (int i = 0; i < n; i++) {
      int16_t s = (int16_t)constrain(recording[pos + i] * gain, -32767.0f, 32767.0f);
      out[2 * i] = s;
      out[2 * i + 1] = s;
    }
    size_t written;
    i2s_write(SPK_PORT, out, n * 4, &written, portMAX_DELAY);
  }

  delay(200);
  while (recordPressed()) delay(20);   // wait for release
  drawRecordButton(0x001F, "RECORD 3s");
  drawStatus(haveTouch ? "Tap RECORD or press BOOT" : "Press BOOT to record");
}

// ---------------- Main ----------------

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== MIC TEST (" MIC_NAME ") ===");
  pinMode(BOOT_BTN, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("MIC TEST  (" MIC_NAME ")", 20, 15, 4);
  tft.drawString("Speak or clap and watch the bar move", 20, 45, 2);
  loadTouchCalibration();

  recording = (int16_t *)malloc(RECORD_LEN * sizeof(int16_t));
  Serial.printf("Recording buffer: %s (free heap %u)\n",
                recording ? "OK" : "FAILED", ESP.getFreeHeap());

  micBegin();
  speakerBegin();

  drawRecordButton(0x001F, "RECORD 3s");
  drawStatus(haveTouch ? "Tap RECORD or press BOOT" : "Press BOOT to record");
}

void loop() {
  static int16_t  block[FRAMES];
  static float    peakDb = -60;
  static uint32_t lastDraw = 0, lastPrint = 0, silentSince = millis();
  static double   sumSq = 0;
  static int      count = 0;

  int n = micRead(block);
  bool allZero = true;
  for (int i = 0; i < n; i++) {
    sumSq += (double)block[i] * block[i];
    if (block[i] != 0) allZero = false;
  }
  count += n;
  if (!allZero) silentSince = millis();

  if (millis() - lastDraw > 60 && count > 0) {
    lastDraw = millis();
    float rms = sqrt(sumSq / count);
    float db = 20.0f * log10f(max(rms, 1.0f) / 32767.0f);
    peakDb = max(db, peakDb - 1.0f);    // peak marker falls slowly
    drawLevel(db, peakDb);
    if (millis() - lastPrint > 500) {
      lastPrint = millis();
      Serial.printf("level %.1f dB, channel %s\n", db, MIC_ANALOG ? "A" : liveChannel ? "R" : "L");
    }
    sumSq = 0;
    count = 0;
  }

  // Only zeros for 3 seconds usually means a wiring problem
  static bool warned = false;
  if (millis() - silentSince > 3000 && !warned) {
    warned = true;
    drawStatus("No signal - check wiring!", TFT_RED);
    Serial.println("Mic gives only zeros - check wiring and solder joints");
  }
  if (!allZero && warned) {
    warned = false;
    drawStatus(haveTouch ? "Tap RECORD or press BOOT" : "Press BOOT to record");
  }

  if (recording && recordPressed()) recordAndPlay();
}
