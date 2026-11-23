@echo off
rem Plays as usual and writes music_log.txt: every music track the game starts, by number.
rem Name your MP3s after these numbers (e.g. track 0A -> music\g4_0A.mp3).
cd /d "%~dp0"
set ROM=gauntlet4.bin
set G4MUSICLOG=1
ghost.exe "%ROM%" --window --frames 0 2> music_log.txt
findstr /c:"music request" music_log.txt
pause
