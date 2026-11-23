@echo off
rem Records Gauntlet 4's music from your own ROM into g4_music.bin (about 5 minutes).
rem The game then plays these clean recordings instead of the Mega Drive music.
cd /d "%~dp0"
set ROM=gauntlet4.bin
ghost.exe "%ROM%" --frames 0 --extract-music g4_music.bin
pause
