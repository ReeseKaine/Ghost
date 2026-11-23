#!/bin/bash
# build.sh -- build Ghost for Linux. Output: host/ghost (with an SDL window if SDL3 is found; Ubuntu 24.04 has no SDL3
# package, so host/linux/sdl/ -- SDL3 built from its source, see make_linux_release.sh -- is used when present).
# The program looks for libSDL3.so.0 in its own folder first (rpath $ORIGIN), then the system's.
set -e
cd "$(dirname "$0")/.."
bash gen_code.sh || exit 1         # the translated game code (c/gen) is made from your own ROM first
[ -f host/linux/sdl/lib/pkgconfig/sdl3.pc ] && export PKG_CONFIG_PATH="$PWD/host/linux/sdl/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
# the YM2151 (ymfm, C++) for the Gauntlet II sound-effect recorder: compiled apart, linked in
YMO=$(mktemp -d)
g++ -std=c++14 -O2 -c -o $YMO/ymfm_opm.o host/ymfm/ymfm_opm.cpp
g++ -std=c++14 -O2 -c -o $YMO/ym2151_wrap.o host/ym2151_wrap.cpp
SRC="host/g4host.c host/g4audio.c host/g4net.c host/g4mods.c host/g4music.c host/g4extract.c host/g4launch.c host/g4data.c host/g2run.c host/g2pics.c host/g2levels.c host/g2attract.c host/g1colours.c host/g4pack.c host/g2sfx.c host/musashi/m68kcpu.c host/musashi/m68kops.c host/musashi/m68kdasm.c host/musashi/softfloat/softfloat.c host/ym/ym3438.c c/g4_bits.c c/g4_password.c c/g4_password_b.c c/g4_dispatch.c c/g4_main.c c/g4_level.c c/g4_levelend.c c/gen/*.c c/gen_z80/g4_z80.c"
if pkg-config --exists sdl3 2>/dev/null; then            # with SDL3 installed: a window, sound, gamepads
    gcc -std=c99 -O2 -DG4_TICK -DG4_SDL $(pkg-config --define-prefix --cflags sdl3) -o host/ghost $SRC $YMO/*.o $(pkg-config --define-prefix --libs-only-L sdl3) -lSDL3 -Wl,-rpath,'$ORIGIN:$ORIGIN/linux/sdl/lib' -static-libgcc -Wl,-Bstatic -lstdc++ -Wl,-Bdynamic -lm
else
    echo "note: SDL3 not found -- building without a window or sound (tests only). For the real game run:"
    echo "      bash host/get_sdl.sh linux    and build again"
    gcc -std=c99 -O2 -DG4_TICK -o host/ghost $SRC $YMO/*.o -lstdc++ -lm
fi
rm -rf $YMO
echo "built host/ghost"
