@echo off
rem Arcade and Record mode with the arcade Gauntlet II mazes (gauntlet2_levels.bin)
cd /d "%~dp0"
set ROM=gauntlet4.bin
ghost.exe "%ROM%" --window --frames 0 --level-set gauntlet2
pause
