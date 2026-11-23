@echo off
rem Like play_g2.bat, but without music: sound effects, Gauntlet 4 voices and the Gauntlet II
rem narration only (for listening to them). Add --nomusic to any launcher for the same.
cd /d "%~dp0"
set ROM=gauntlet4.bin
ghost.exe "%ROM%" --window --frames 0 --level-set gauntlet2 --nomusic
pause
