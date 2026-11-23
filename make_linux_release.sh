#!/bin/bash
# make_linux_release.sh VERSION -- build Ghost for Linux and pack releases/Ghost-vVERSION-linux-x64.tar.gz
# (ghost + libSDL3.so.0 + README.txt + README-LINUX.txt + THIRD_PARTY_LICENSES.txt + play.sh).
#
# Needs SDL3 in host/linux/sdl/: bash host/get_sdl.sh linux (SDL's source release, built with cmake;
# Ubuntu 24.04 has no SDL3 package).
# SDL loads X11/Wayland/ALSA/PulseAudio itself at run time, so the release needs none of them installed
# to start. The program needs glibc 2.38+ (SDL's own requirement on this build machine).
set -e
cd "$(dirname "$0")"
V="${1:?usage: make_linux_release.sh VERSION   (e.g. 0.1.0)}"
[ -f host/linux/sdl/lib/libSDL3.so.0 ] || { echo "SDL3 missing in host/linux/sdl: run  bash host/get_sdl.sh linux"; exit 1; }
python3 -I make_licenses.py
bash host/build.sh
if strings host/ghost | grep -q "$HOME"; then echo "ghost contains a home-folder path: not packing"; exit 1; fi
N="Ghost-v$V"
T=$(mktemp -d)
mkdir "$T/$N"
cp host/ghost host/linux/play.sh host/linux/README-LINUX.txt "$T/$N/"
cp -L host/linux/sdl/lib/libSDL3.so.0 "$T/$N/"
tr -d '\r' < host/win/README.txt > "$T/$N/README.txt"
tr -d '\r' < host/win/THIRD_PARTY_LICENSES.txt > "$T/$N/THIRD_PARTY_LICENSES.txt"
strip "$T/$N/ghost"
chmod 755 "$T/$N/ghost" "$T/$N/play.sh"
chmod 644 "$T/$N/libSDL3.so.0" "$T/$N/"*.txt
mkdir -p releases
tar -C "$T" --owner=0 --group=0 --numeric-owner -czf "releases/$N-linux-x64.tar.gz" "$N"
rm -rf "$T"
ls -l "releases/$N-linux-x64.tar.gz"
