@echo off
rem Starts the IoT Lab voice server. Keep this window open while using voice commands.
cd /d "%~dp0"
set HF_HUB_DISABLE_SYMLINKS_WARNING=1
set PYTHONIOENCODING=utf-8
set PYTHONUNBUFFERED=1
rem For faster (but less accurate) answers, remove "rem" from the next line:
rem set VOICE_MODEL=base
.venv\Scripts\python.exe server.py
pause
