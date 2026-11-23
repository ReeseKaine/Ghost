@echo off
rem Host an online game: you are player 1. Others join with your address and port 47100.
cd /d "%~dp0"
set ROM=gauntlet4.bin
set /p PLAYERS=How many players in total (2-4)? 
ghost.exe "%ROM%" --window --host 47100 --players %PLAYERS%
pause
