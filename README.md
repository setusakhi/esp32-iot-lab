# ESP32 IoT Lab

A touchscreen ESP32 gadget with internet radio (Hindi, All India Radio, devotional),
clock & weather, timer, calculator, piano and voice commands.

- [`esp32-iot-lab/`](esp32-iot-lab/README.md): the gadget firmware (PlatformIO, ESP32 DevKit V1)
- [`voice-server/`](voice-server/README.md): runs on a laptop; turns speech into commands
  with Whisper (offline), speaks replies, and controls Spotify

For Spotify, copy `voice-server/spotify_config.example.json` to `spotify_config.json`
and fill in your own app keys (that file is not committed).
