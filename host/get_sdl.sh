#!/bin/bash
# get_sdl.sh [win|linux|all] -- fetch SDL3 3.4.20 (window, sound and gamepad library) for building
# Ghost. SDL is not kept in the repository; this downloads SDL's own official release files from
# https://github.com/libsdl-org/SDL/releases/tag/release-3.4.20 and checks their SHA-256 first.
#   win    host/win/sdl/SDL3-3.4.20/  the MinGW development package (for host/build_win.sh)
#   linux  host/linux/sdl/            SDL3 built from its source release (for host/build.sh: Ubuntu
#                                     24.04 has no SDL3 package; needs cmake and the usual X11,
#                                     Wayland, ALSA and PulseAudio -dev packages)
# Default: win. Needs curl and tar.
set -e
cd "$(dirname "$0")/.."
WHAT="${1:-win}"
V=3.4.20
URL="https://github.com/libsdl-org/SDL/releases/download/release-$V"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

fetch() {   # fetch FILE SHA256 -> $TMP/FILE, checked
    echo "downloading $1 ..."
    curl -fSL --retry 3 -o "$TMP/$1" "$URL/$1"
    echo "$2  $TMP/$1" | sha256sum -c --quiet - || { echo "$1: checksum does not match -- not used"; exit 1; }
}

if [ "$WHAT" = win ] || [ "$WHAT" = all ]; then
    if [ -f host/win/sdl/SDL3-$V/x86_64-w64-mingw32/lib/libSDL3.dll.a ]; then
        echo "Windows SDL3 already in host/win/sdl/SDL3-$V"
    else
        fetch SDL3-devel-$V-mingw.tar.gz d2d3bf3fb97a90b22e908a306d7362631ef1bab229d988fe6669595dc29cd9ed
        mkdir -p host/win/sdl
        tar -xzf "$TMP/SDL3-devel-$V-mingw.tar.gz" -C host/win/sdl
        echo "Windows SDL3 ready in host/win/sdl/SDL3-$V"
    fi
fi

if [ "$WHAT" = linux ] || [ "$WHAT" = all ]; then
    if [ -f host/linux/sdl/lib/libSDL3.so.0 ]; then
        echo "Linux SDL3 already in host/linux/sdl"
    else
        command -v cmake >/dev/null || { echo "cmake is needed: sudo apt install cmake"; exit 1; }
        fetch SDL3-$V.tar.gz f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f
        tar -xzf "$TMP/SDL3-$V.tar.gz" -C "$TMP"
        cmake -S "$TMP/SDL3-$V" -B "$TMP/build" -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX="$PWD/host/linux/sdl" -DSDL_SHARED=ON -DSDL_STATIC=OFF \
            -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_X11_XTEST=OFF > "$TMP/cmake.log" 2>&1 \
            || { tail -20 "$TMP/cmake.log"; echo "SDL3 configure failed (missing -dev packages?)"; exit 1; }
        cmake --build "$TMP/build" -j"$(nproc)" > "$TMP/build.log" 2>&1 || { tail -20 "$TMP/build.log"; exit 1; }
        cmake --install "$TMP/build" > /dev/null
        rm -f host/linux/sdl/lib/libSDL3_test.a
        echo "Linux SDL3 ready in host/linux/sdl"
    fi
fi
