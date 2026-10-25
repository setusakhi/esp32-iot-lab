# IoT Lab voice server

Runs on your laptop and gives the ESP32 gadget short English voice commands.
The gadget records 3 seconds of audio (8-bit mu-law, to save its memory) and sends it here;
[faster-whisper](https://github.com/SYSTRAN/faster-whisper) turns it into text on your own
computer (no audio leaves it), and `server.py` picks the command. The answer is shown on the
gadget and spoken through its speaker, using the Windows voice (`VOICE_SPEAKER`, default Zira).

Watch the server window to see exactly what was heard and how long each step took.

## Start
Double-click **`start-server.bat`** and keep the window open. The first start downloads the
Whisper model (small: about 480 MB).

When Windows Firewall asks about Python, allow it on **Private networks**, otherwise the
gadget cannot reach the server.

The gadget finds the server through `VOICE_SERVER_URL` in `../esp32-iot-lab/src/gadget_config.h`
(currently `http://192.168.1.8:5005`). If your laptop's WiFi address changes (`ipconfig`),
update it there and upload the firmware again.

## Commands
| Say | Gadget does |
|---|---|
| "Play radio", "Play" | Radio, last station |
| "Play Vividh Bharati", "Play news" | All India Radio's Vividh Bharati / Hindi news |
| "Play kirtan" / "Play Gurbani", "Play bhajan", "Play Tirupati", "Play Shiva", "Play Sai Baba" | Devotional stations |
| "Next station", "Previous station" | Changes the radio station |
| "Volume up", "Volume down" | Radio volume (or beeps and replies when the radio is off) |
| "Stop" | Stops and goes to the home screen |
| "What time is it", "What's the date" | Says the time / date and shows the clock |
| "What's the weather" | Says the weather (Open-Meteo, `VOICE_CITY`) and shows it |
| "What is 25 times 4", "15 percent of 200", "Square root of 81" | Says the answer |
| "Timer 5 minutes", "Stop the timer", "Add 2 minutes" | Timer |
| "Calculator", "Piano", "Home" | Opens that screen |
| "Hello", "Thank you", "Who are you" | A friendly reply |

The rules are in `understand()` in `server.py`; station names in `STATIONS` must match
`src/radio_lists.h` in the firmware. Whisper is told which words to expect (`HINT_WORDS`):
commands only, because names there made Whisper insert them where nobody said them.

## Settings (environment variables, see `start-server.bat`)
- `VOICE_MODEL`: `small` (default) or `base` (faster, much less accurate)
- `VOICE_LANGUAGE`: `en` (default), `hi` for Hindi, or empty to detect each time (twice as slow)
- `VOICE_CITY`, `VOICE_LAT`, `VOICE_LON`: place for the weather (default Jaipur)
- `VOICE_PORT`: default `5005`
- `VOICE_SPEAKER`: Windows voice for replies, default `Microsoft Zira Desktop`
  (or `Microsoft David Desktop`)
- `VOICE_SAVE`: `1` (default) saves each recording in `recordings/` with what was heard, so
  settings can be compared on your own voice; `0` turns it off

On real gadget recordings, `small` beat `base` by far (about 12 of 16 phrases vs 3);
`large-v3-turbo` was no better than `small` with hint words but took 15 s per phrase.
Whisper takes about 4 s per command on a laptop CPU.

## Test without the gadget
Send any 16 kHz, 16-bit mono raw recording:
```
curl -X POST --data-binary @command.raw http://localhost:5005/listen
```
