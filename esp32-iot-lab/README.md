# ESP32 IoT Lab Gadget

A touchscreen gadget with a home menu:

| Tile | What it does |
|---|---|
| Radio | Hindi internet radio stations, station list loaded live from radio-browser.info |
| Calculator | + − × ÷, %, +/−, DEL, shows the calculation line |
| Clock & Weather | Big clock synced from the internet, date, live weather (Open-Meteo) |
| Timer | Countdown with presets; rings on any screen, tap anywhere to stop |
| Piano | 8-key Sa Re Ga Ma piano, demo tune, siren |
| WiFi Setup | Connect the gadget to a WiFi network from your phone |

Works offline too: Calculator, Timer and Piano don't need internet.

## Wiring
Same as the touch piano project, nothing new:

- **Display:** VCC 3V3, GND, CS 15, RESET 4, D/C 2, SDI 23, SCK 18, LED 3V3, SDO not connected,
  T_CLK 18, T_CS 21, T_DIN 23, T_DO 19
- **Amplifier MAX98357A:** VIN 5V, GND, BCLK 26, LRC 25, DIN 22
- **Microphone MAX9814 (analog):** VDD 3V3, GND, OUT 35; GAIN and AR not connected
- **Touch button TTP223:** VCC 3V3, GND, OUT 27. Tap = talk from any screen, long press = stop and go home
- **Rotary encoder KY-040:** + 3V3 (not 5V), GND, CLK 32, DT 33, SW 14. Turn = volume, press =
  pause/play on the radio screen, else home (set `ENC_REVERSE` in `gadget_config.h` if it turns the wrong way)
- *(Microphone INMP441, only for env:mictest: VDD 3V3, GND, SCK 32, WS 33, SD 34, L/R to GND;
  it shares 32/33 with the encoder, so unplug the encoder for that test)*

## Upload
1. Open this folder in VS Code (File → Open Folder → `esp32-iot-lab`).
2. Wait for PlatformIO to download the libraries (first time only, a few minutes).
3. Choose **env:esp32dev** in the bottom bar, click **→ Upload** (hold BOOT if it says "Connecting...").
4. Open the Serial Monitor to see status messages.

## First start
1. **Touch calibration:** touch the arrows in each corner (only once; it's saved).
2. **WiFi setup:** the screen shows instructions.
   - On your phone, connect to WiFi **IoT-Lab-Setup**.
   - A setup page opens (if not, open **192.168.4.1** in the browser).
   - Tap **Configure WiFi**, pick your network, enter the password, **Save**.
   - The gadget remembers it. Use the **WiFi Setup** tile later to change networks.
3. If a saved WiFi is not available (for example at a school), the gadget starts offline
   without waiting. Use the WiFi Setup tile or your phone's hotspot there.

## Settings (`src/gadget_config.h`)
- `WEATHER_CITY`, `WEATHER_LAT`, `WEATHER_LON`: your city for weather (set to Jaipur)
- `CLOCK_24H`: `true` for 24-hour clock
- `RADIO_LANGUAGE`: `"hindi"` (try `"punjabi"`, `"marathi"`, `"english"` ...)
- `RADIO_DEFAULT_VOLUME`, `DEFAULT_VOLUME`

## Notes
- The radio list comes from **radio-browser.info**, a free community directory. Stations are
  run by others and some may be offline at times; the list skips broken ones and the screen
  says so if a station doesn't play. Big commercial FM brands often don't offer free streams.
- Radio and beeps share one amplifier: while the radio screen is open, beeps are muted.
  If the timer finishes during radio, the radio stops and the alarm rings.
- Weather data: Open-Meteo is free for personal and non-commercial use. For a product you
  sell, check their commercial terms.
- The app area uses the "huge_app" partition (3 MB) because the radio decoders are large.
  Over-the-air updates would need a different layout later.

## Project structure
```
src/gadget_config.h       settings
src/main.cpp       start-up, touch handling, screen switching
src/ui.*           shared drawing helpers (buttons, top bar)
src/net.*          WiFi (WiFiManager) and internet time
src/tones.*        tone engine for beeps, piano, alarms
src/radio.*        internet radio engine + station list
src/app_menu.cpp   home screen
src/app_radio.cpp  radio screen
src/app_calc.cpp   calculator
src/app_clock.cpp  clock & weather
src/app_timer.cpp  timer
src/app_piano.cpp  piano
src/hwtest.cpp     hardware test (env:hwtest)
src/mictest.cpp    microphone test (env:mictest)
```
Each screen is an "app" with four functions (enter, exit, update, touch), so new apps are easy to add.

## Microphone test
Choose **env:mictest** and click **→ Upload**. The screen shows a live sound-level bar.
Tap **RECORD 3s** (or press the **BOOT** button) to record 3 seconds; it plays back through the speaker.
If it says "No signal", check the mic wiring and the solder joints.
For the MAX9814 analog mic, use **env:mictest-analog** instead.
Afterwards choose **env:esp32dev** and upload again to get the gadget back.

## Troubleshooting
- **Radio says "Could not load stations":** check WiFi; tap the screen to retry.
- **A station shows "not working":** pick another; stations change over time.
- **Radio crackles or stops:** weak WiFi. Move closer to the router.
- **Clock shows "Getting time...":** needs internet; it syncs within a few seconds.
- **Touch is off:** touch the screen during the start-up "IoT Lab" message to recalibrate.
