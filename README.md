# Ghost

*An unofficial PC port of Gauntlet 4. It plays your own cartridge dump.*

> **Work in progress.** Ghost is still being built and tested. Expect bugs, unfinished features
> and changes between versions: options, data files and graphics-pack details may change, and
> some features have only been tested in scripted runs so far (for example online play's
> restart on Windows). More is planned, including an NES Gauntlet mode. Bug reports are welcome
> under Issues.

Gauntlet 4 (Sega Mega Drive / Genesis, 1993) running natively on Windows and Linux, built from a
C translation of the original game (the
[gauntlet4-decomp](https://github.com/ReeseKaine/gauntlet4-decomp) project). On top of the original game:

- a launcher (video, audio, graphics packs, and "Build the data files" from your own ROMs);
- **GAUNTLET II**: the 117 arcade Gauntlet II mazes with their rules, narration, sound effects,
  dragon, thief and mugger, treasure rooms and attract screens;
- **GAUNTLET III**: the Atari 8-bit Gauntlet's Deeper Dungeons mazes (from the Atari disk or the
  Commodore 64 tape) plus the Atari's own mazes, with its treasure rooms and colours. (Why call
  it that? The Deeper Dungeons were the original game's real follow-up, while the two different
  games actually sold as Gauntlet III play nothing like it. The less spoken about the real
  Gauntlet III, the better.)
- arcade colours, a Start Level choice, graphics packs (replacement pictures at higher
  resolution), recorded music, and online play for up to 4 players.

**No ROM or game data is included.** You need your own copy of the Gauntlet 4 cartridge
(September 1993 version), named `gauntlet4.bin` next to `ghost.exe`. Everything else is made on
your own machine, from your own copies, by the launcher.

Check your ROM's fingerprint (any ROM manager or hash tool shows these):

| Version | Size | CRC32 | SHA-1 |
|---|---|---|---|
| September 1993 (use this one) | 1,048,576 bytes | `f9d60510` | `d28e22207121f0e2980dd409b4fb24f9fb8967ae` |
| August 1993 (not supported: the translation was made from September) | 1,048,576 bytes | `3bf46dce` | `26c26ee2bb9571d51537d9328a5fd2a91b4e9dc1` |

## Playing

**Windows:** download the ZIP from Releases, unpack it, add your ROM, and start `ghost.exe`
(the launcher). `README.txt` explains the controls, the options and online play.

**Linux (x86-64):** download the `linux-x64.tar.gz` from Releases, unpack it, add your ROM, and
run `./play.sh`. SDL3 comes with it, so nothing needs installing; it needs glibc 2.38 or newer
(Ubuntu 24.04, Fedora 39, Debian 13, current Arch, SteamOS 3.6 or newer). `README-LINUX.txt`
lists the differences from Windows.

## The extras' data files

In the launcher choose **Build the data files**. It finds what you have in the ROM folder (by
content, so the file names do not matter) and makes the matching data files next to the game:

| You have | It makes |
|---|---|
| arcade Gauntlet (`gauntlet.zip`, non-merged MAME set) | `arcade_colours.txt` |
| arcade Gauntlet II (`gaunt2.zip`, non-merged MAME set) | `gauntlet2_levels.bin`, `g2_sprites.bin`, `g2_speech.bin`, `g2_sfx.bin`, `g2_attract.bin` |
| Atari 8-bit Deeper Dungeons (`.atr`) or C64 Deeper Dungeons tape (`.tap`), optionally the Atari Gauntlet side 2 (`.atr`) or C64 Gauntlet cartridge (`.crt`) | `gauntlet3_levels.bin` |
| the Gauntlet 4 ROM | `g4_music.bin` (the music, recorded in the background) |

The older Python converters (`make_data.sh` and the `*.py` scripts) still work but are no longer
needed.

## Building

The game's translated code is not in this repository: the build makes it from **your own
Gauntlet 4 ROM** first (`gen_code.sh`, about 3 seconds; it needs Python 3 and sets up `capstone`
and `z80dis` in `~/g4venv` the first time). Put the ROM in the project folder as
`gauntlet4.bin`, or set `G4ROM` to its path.

SDL3 (window, sound, gamepads) is not in the repository either. `bash host/get_sdl.sh` fetches
SDL's official 3.4.20 release files (checked by SHA-256) into the right place: `win` for the
Windows build, `linux` for the Linux build (built from SDL's source; needs `cmake`), `all` for both.

Linux (needs `gcc`, `g++` and Python 3):

    bash host/get_sdl.sh linux       # once (skip it if your system has SDL3, libsdl3-dev)
    bash host/build.sh               # -> host/ghost

Windows, cross-built from Linux or WSL (needs `mingw-w64`: `sudo apt install mingw-w64`):

    bash host/get_sdl.sh win         # once
    bash host/build_win.sh           # -> host/win/ghost.exe + SDL3.dll

The release packages are made by `bash make_windows_release.sh VERSION` and
`bash make_linux_release.sh VERSION` (into `releases/`; the version must match `GHOST_VERSION` in
`host/g4launch.h`).

## Credits

Other people's code in this project, each with its licence kept beside it:
Musashi 68000 emulator by Karl Stenerud (MIT, `host/musashi/`) with SoftFloat;
Nuked-OPN2 YM2612 emulator by Nuke.YKT (LGPL 2.1, `host/ym/`);
ymfm YM2151 emulator by Aaron Giles (BSD 3-Clause, `host/ymfm/`; used only to record the
Gauntlet II sound effects); dr_mp3 by David Reid and QOA by Dominic Szablewski (`host/mp3/`);
stb_image and stb_image_write by Sean Barrett (public domain / MIT, `host/stb/`).
Graphics, sound and gamepads go through SDL3.

## How this was made

This project was built with the help of Claude (Anthropic's AI assistant), which wrote much of
the code and tools under my direction. I chose the features, tested every build on Windows and
reported what needed fixing. The game itself runs on the translated code from the
[decomp project](https://github.com/ReeseKaine/gauntlet4-decomp), where every translated routine is checked against the original ROM by its tests.

## Legal

Gauntlet 4, Gauntlet, Gauntlet II and Gauntlet: The Deeper Dungeons belong to their rights
holders. This project is not affiliated with them and contains no ROM or game data. The
machine translation of the game's code is not included either: the build generates it from your
own ROM. The hand-written files in `c/g4_*.c` rewrite a few of the game's routines.

## License

Ghost's own code is under the MIT License: see `LICENSE`. The other people's code it includes
keeps its own licence (see Credits); all their notices are collected in
`host/win/THIRD_PARTY_LICENSES.txt` (made by `make_licenses.py`), which goes in the release ZIP
next to `ghost.exe`. Nuked-OPN2 is under the GNU LGPL 2.1: its full source is in `host/ym/`,
and `host/build_win.sh` / `host/build.sh` rebuild the program with a changed copy of it. No
licence here grants any rights to Gauntlet 4 or the other games.
