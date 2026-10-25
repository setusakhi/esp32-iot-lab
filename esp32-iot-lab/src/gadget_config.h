#pragma once
// ============================================================
//  Settings you may want to change
//  (Display and touch pins are in platformio.ini)
// ============================================================

// ---- Audio amplifier (MAX98357A) ----
#define I2S_BCLK_PIN   26
#define I2S_LRC_PIN    25
#define I2S_DOUT_PIN   22
#define AUDIO_SAMPLE_RATE  22050
#define DEFAULT_VOLUME     5        // beeps and piano: 0..10

// ---- Microphone (INMP441, uses the second I2S port) ----
// VDD to 3V3 (not 5V), GND to GND, L/R to GND
#define MIC_SCK_PIN    32
#define MIC_WS_PIN     33
#define MIC_SD_PIN     34
#define MIC_SAMPLE_RATE  16000

// ---- Analog microphone (MAX9814), used by the Voice screen and env:mictest-analog ----
// VDD to 3V3, GND to GND, OUT to GPIO 35. GAIN and AR not connected.
#define MIC_ADC_PIN      35
#define MIC_ADC_CHANNEL  ADC1_CHANNEL_7     // ADC1 channel of GPIO 35

// ---- Voice commands (voice-server/server.py on your laptop) ----
// Your laptop's WiFi address (ipconfig) and the server port
#define VOICE_SERVER_URL   "http://192.168.1.8:5005"
#define VOICE_RECORD_SECS  3

// ---- WiFi setup ----
// With no saved WiFi, the gadget creates this hotspot.
// Connect your phone to it and choose your WiFi network.
#define WIFI_SETUP_AP_NAME  "IoT-Lab-Setup"
#define WIFI_SETUP_TIMEOUT  120     // seconds, then continue offline

// ---- Clock ----
#define TIMEZONE   "IST-5:30"       // India Standard Time
#define CLOCK_24H  false            // true = 24-hour clock

// ---- Weather (Open-Meteo, no API key needed) ----
#define WEATHER_CITY  "Jaipur"
#define WEATHER_LAT   26.91
#define WEATHER_LON   75.79
#define WEATHER_REFRESH_MIN  15

// ---- Internet radio ----
#define RADIO_LANGUAGE        "hindi"   // station list comes from radio-browser.info
#define RADIO_MAX_STATIONS    24
#define RADIO_DEFAULT_VOLUME  8         // 0..21
