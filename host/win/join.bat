@echo off
rem Join an online game.
cd /d "%~dp0"
set ROM=gauntlet4.bin
set /p ADDR=Host address (name or IP)? 
ghost.exe "%ROM%" --window --join %ADDR%:47100
pause
