@echo off
cd /d "%~dp0"
set ROM=gauntlet4.bin
ghost.exe "%ROM%" --window --frames 0
pause
