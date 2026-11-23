#!/bin/bash
# make_windows_release.sh VERSION -- build Ghost for Windows and pack releases/Ghost-vVERSION-windows.zip
# (ghost.exe + SDL3.dll + README.txt + THIRD_PARTY_LICENSES.txt + the players' .bat files).
# Needs what host/build_win.sh needs (mingw-w64, SDL3 via host/get_sdl.sh win, your ROM for gen_code.sh).
set -e
cd "$(dirname "$0")"
V="${1:?usage: make_windows_release.sh VERSION   (e.g. 0.1.1)}"
grep -q "GHOST_VERSION \"$V\"" host/g4launch.h || { echo "host/g4launch.h says another version: set GHOST_VERSION to \"$V\" first"; exit 1; }
python3 -I make_licenses.py
bash host/build_win.sh
if strings host/win/ghost.exe | grep -q "$HOME"; then echo "ghost.exe contains a home-folder path: not packing"; exit 1; fi
N="Ghost-v$V"
mkdir -p releases
python3 - "$N" <<'EOF'
import sys, zipfile
n = sys.argv[1]; w = "host/win/"
files = ["ghost.exe", "SDL3.dll", "README.txt", "THIRD_PARTY_LICENSES.txt",
         "play.bat", "play4.bat", "play_g2.bat", "play_g2_nomusic.bat", "host.bat", "join.bat",
         "extract_music.bat", "music_to_wav.bat", "music_log.bat", "dump_tiles.bat"]
with zipfile.ZipFile(f"releases/{n}-windows.zip", "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for f in files:
        z.write(w + f, f"{n}/{f}")
EOF
ls -l "releases/$N-windows.zip"
