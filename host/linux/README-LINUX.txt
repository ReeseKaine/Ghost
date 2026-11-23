GHOST ON LINUX
==============

Everything in README.txt applies on Linux too. Read "ghost" where it says ghost.exe and
"./ghost <options>" where it shows a .bat file. This file covers only what is different.

WHAT YOU NEED
  ghost            the game
  libSDL3.so.0     window, sound and gamepad library (keep it next to ghost; ghost looks
                   for it in its own folder first)
  play.sh          starts the launcher from this folder (same as double-clicking ghost)
  a ROM            your own copy of the Gauntlet 4 cartridge (September 1993 version),
                   in this folder, named gauntlet4.bin

  A 64-bit (x86-64) Linux from about 2024 on: glibc 2.38 or newer (Ubuntu 24.04, Fedora
  39, Debian 13, current Arch, SteamOS 3.6 and newer), with X11 or Wayland, and
  PulseAudio, PipeWire or ALSA for sound. Nothing needs installing.

STARTING
  In a terminal, in this folder:
      ./play.sh
  or start the game directly with options, like the .bat files do:
      ./ghost gauntlet4.bin --window --frames 0                    (play.bat)
      ./ghost gauntlet4.bin --window --frames 0 --teamplayer       (play4.bat)
      ./ghost gauntlet4.bin --window --frames 0 --level-set gauntlet2   (play_g2.bat)
  If your file manager won't run it: right-click ghost and play.sh -> Properties ->
  allow running as a program, or in a terminal:  chmod +x ghost play.sh

  The launcher's "Build the data files" works the same way as on Windows.

ONLINE
  The firewall question Windows asks does not appear; if your computer runs a firewall
  (ufw, firewalld), allow TCP port 47100 to host a game.

BUILDING IT YOURSELF
  The source code is at https://github.com/ReeseKaine/Ghost (host/build.sh).
