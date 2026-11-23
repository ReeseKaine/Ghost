@echo off
rem For graphics-pack makers: play as usual; every tile, sprite tile and Gauntlet II picture
rem shown is saved once into the folder tiles_dump with the name a pack must use.
rem Redraw them bigger (the original size x your scale), put them in packs\<your pack>.
rem These pictures come from your own ROM: for your own use, not for sharing.
cd /d "%~dp0"
set ROM=gauntlet4.bin
if not exist tiles_dump mkdir tiles_dump
ghost.exe "%ROM%" --window --frames 0 --dump-tiles tiles_dump
pause
