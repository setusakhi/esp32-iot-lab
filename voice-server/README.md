# IoT Lab voice server

Runs on your laptop and gives the ESP32 gadget short English voice commands.
The gadget records 3 seconds of audio and sends it here; [faster-whisper](https://github.com/SYSTRAN/faster-whisper)
turns it into text on your own computer (nothing goes to the internet), and `server.py` picks the command.
The answer ("Timer set for 5 minutes") is shown on the gadget and spoken through its speaker,
using the Windows voice (`VOICE_SPEAKER`, default Microsoft Zira).

Watch the server window to see exactly what was heard.

## Start
Double-click **`start-server.bat`** and keep the window open. The first start downloads the
Whisper model (small: about 480 MB).

When Windows Firewall asks about Python, allow it on **Private networks**, otherwise the
gadget cannot reach the server.

The gadget finds the server through `VOICE_SERVER_URL` in `../esp32-iot-lab/src/gadget_config.h`
(currently `http://192.168.1.8:5005`). If your laptop's WiFi address changes (`ipconfig`),
update it there and upload the firmware again.

## Commands
| Say | Does |
|---|---|
| "Play Arijit Singh", "Play Kesariya" | Spotify plays that artist or song |
| "Play songs of Lata Mangeshkar", "Play Kishore Kumar songs" | Spotify plays that artist |
| "Play", "Play music", "Resume" | Spotify continues |
| "Stop", "Pause" | Pauses Spotify (when nothing plays there: back to the gadget home screen) |
| "Next", "Previous" | Next / previous song |
| "Volume up", "Volume down" | Spotify volume +/- 15% |
| "What's playing" | Says the song name |
| "Play radio" | The gadget's FM radio (last station) |
| "Play Vividh Bharati", "Play news" | All India Radio's Vividh Bharati / Hindi news, on the gadget |
| "Play kirtan" / "Play Gurbani", "Play bhajan", "Play Tirupati", "Play Shiva", "Play Sai Baba" | Devotional stations on the gadget |
| "Timer 5 minutes", "Set a timer for 10 minutes" | Starts the timer |
| "Time", "Weather" | Shows clock / weather |
| "Calculator", "Piano", "Home" | Opens that screen |

Gadget commands are in `INTENTS` (`server.py`), station names in `STATIONS` (`server.py`, must
match `src/radio_lists.h` in the firmware), Spotify ones in `WORDS` (`spotify.py`).
Whisper is told which words to expect (`HINT_WORDS` in `server.py`): commands only, because
singer names there made Whisper insert them where nobody said them. Add favourite singers to
`KNOWN_ARTISTS` in `spotify.py` instead; it corrects slightly mis-heard names.

## Spotify (needs Spotify Premium)
The gadget is the remote: music plays on your own Spotify device (phone, laptop, speaker),
through the official Spotify Web API.

Setup (once):
1. Create an app at https://developer.spotify.com/dashboard with Redirect URI
   `http://127.0.0.1:8888/callback` and "Web API" ticked.
2. Put its Client ID and Client secret in `spotify_config.json`.
3. Run `.venv\Scripts\python spotify.py login` and approve in the browser.
4. Restart the server; it prints "Spotify: connected".

Open the Spotify app on a device once so there is somewhere to play.
`spotify_config.json` and `spotify_token.json` are private: don't share them.

## Settings (environment variables, see `start-server.bat`)
- `VOICE_MODEL`: `small` (default) or `base` (faster, much less accurate)
- `VOICE_LANGUAGE`: `en` (default), `hi` for Hindi, or empty to detect each time (twice as slow)
- `VOICE_PORT`: default `5005`
- `VOICE_SPEAKER`: Windows voice for spoken replies, default `Microsoft Zira Desktop`
  (or `Microsoft David Desktop`)
- `VOICE_SAVE`: `1` (default) saves each recording in `recordings/` with what was heard, so
  settings can be compared on your own voice; `0` turns it off

On real gadget recordings, `small` beat `base` by far (about 12 of 16 phrases vs 3), and the
hint words fixed most mis-heard names for about 0.7 s extra; `large-v3-turbo` was no better
than `small` with hints but took 15 s per phrase.

## Test without the gadget
Send any 16 kHz, 16-bit mono raw recording:
```
curl -X POST --data-binary @command.raw http://localhost:5005/listen
```
