@echo off
rem Turns g4_music.bin (made by extract_music.bat) into WAV files you can play in any
rem media player, in a folder called music_wav. Looping tunes play their loop twice.
cd /d "%~dp0"
if not exist music_wav mkdir music_wav
ghost.exe --music-wav g4_music.bin music_wav
pause
