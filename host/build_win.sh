#!/bin/bash
# build_win.sh -- build the native Gauntlet 4 host as a Windows program (gamepads via XInput /
# DirectInput need a Windows build: WSL does not see controllers). Needs the MinGW-w64 cross
# compiler (sudo apt install mingw-w64); SDL3 comes from host/win/sdl (the official SDL3 MinGW
# development package, SDL3-devel-3.4.20-mingw.tar.gz). Output: host/win/ghost.exe + SDL3.dll
# Icon: if host/win/ghost.ico exists (sizes up to 256x256), it becomes the program's icon (in
# Explorer, the window and the taskbar). Without it the program builds with no icon.
set -e
cd "$(dirname "$0")/.."
SDL=host/win/sdl/SDL3-3.4.20/x86_64-w64-mingw32
if [ ! -f $SDL/lib/libSDL3.dll.a ]; then
    echo "SDL3 for Windows is missing (it is not kept in the repository). Get it with:"
    echo "    bash host/get_sdl.sh win"
    echo "(downloads SDL's official SDL3-devel-3.4.20-mingw.tar.gz and unpacks it into host/win/sdl/)"
    exit 1
fi
command -v x86_64-w64-mingw32-gcc >/dev/null || { echo "The MinGW-w64 cross compiler is missing: sudo apt install mingw-w64"; exit 1; }
bash gen_code.sh || exit 1         # the translated game code (c/gen) is made from your own ROM first
# the YM2151 (ymfm, C++) for the Gauntlet II sound-effect recorder: compiled apart, linked in
YMO=$(mktemp -d)
x86_64-w64-mingw32-g++ -std=c++14 -O2 -c -o $YMO/ymfm_opm.o host/ymfm/ymfm_opm.cpp
x86_64-w64-mingw32-g++ -std=c++14 -O2 -c -o $YMO/ym2151_wrap.o host/ym2151_wrap.cpp
SRC="host/g4host.c host/g4audio.c host/g4net.c host/g4mods.c host/g4music.c host/g4extract.c host/g4launch.c host/g4data.c host/g2run.c host/g2pics.c host/g2levels.c host/g2attract.c host/g1colours.c host/g4pack.c host/g2sfx.c host/musashi/m68kcpu.c host/musashi/m68kops.c host/musashi/m68kdasm.c host/musashi/softfloat/softfloat.c host/ym/ym3438.c c/g4_bits.c c/g4_password.c c/g4_password_b.c c/g4_dispatch.c c/g4_main.c c/g4_level.c c/g4_levelend.c c/gen/*.c c/gen_z80/g4_z80.c"
RES=""
if [ -f host/win/ghost.ico ]; then
    RES=$(mktemp --suffix=.o)
    (cd host/win && echo '1 ICON "ghost.ico"' | x86_64-w64-mingw32-windres -O coff -o "$RES")
    echo "with icon host/win/ghost.ico"
fi
x86_64-w64-mingw32-gcc -std=c99 -O2 -DG4_TICK -DG4_SDL -I$SDL/include \
    -o host/win/ghost.exe $SRC $YMO/*.o $RES -L$SDL/lib -lSDL3 -lws2_32 -lm -static-libgcc -Wl,-Bstatic -lstdc++ -lwinpthread -Wl,-Bdynamic
[ -n "$RES" ] && rm -f "$RES"
rm -rf $YMO
cp $SDL/bin/SDL3.dll host/win/
echo "built host/win/ghost.exe"
