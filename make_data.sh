#!/bin/bash
# make_data.sh -- make the extra data files from your own ROMs, in one go:
#     gauntlet2_levels.bin   the Gauntlet II level set           (needs gauntlet2.zip)
#     g2_sprites.bin         the Gauntlet II dragon picture      (needs gauntlet2.zip)
#     arcade_colours.txt     arcade colours for both level sets  (needs gauntlet.zip)
#     g2_speech.bin          the Gauntlet II narration           (needs gauntlet2.zip)
# usage: bash make_data.sh <gauntlet4.bin> <gauntlet.zip> <gauntlet2.zip> [output folder]
# Needs gcc and a Python with unicorn installed (set PY to use another Python, e.g. a venv's).
# The output folder defaults to host/win, next to ghost.exe.
set -e
[ $# -ge 3 ] || { echo "usage: bash make_data.sh <gauntlet4.bin> <gauntlet.zip> <gauntlet2.zip> [output folder]"; exit 1; }
ROM=$(realpath "$1"); G1=$(realpath "$2"); G2=$(realpath "$3")
cd "$(dirname "$0")"
OUT=$(realpath -m "${4:-host/win}")
PY=${PY:-python3}
mkdir -p "$OUT" levels
echo "1/9 building the level decoder library"
gcc -std=c99 -O1 -w -shared -fPIC -o c/libg4.so c/g4_*.c c/gen/*.c c/test_shim.c
echo "2/9 reading the Gauntlet 4 levels";   $PY -I extract_levels.py "$ROM" c/libg4.so levels >/dev/null
echo "3/9 reading the Gauntlet mazes";      $PY -I import_arcade.py "$G1" "$ROM" c/libg4.so levels/arcade_g1.json >/dev/null
echo "4/9 reading the Gauntlet II mazes";   $PY -I import_gauntlet2.py "$G2" levels/arcade_g2.json >/dev/null
echo "5/9 converting the Gauntlet II mazes"; $PY -I g2_convert.py "$ROM" c/libg4.so "$OUT/gauntlet2_levels.bin" >/dev/null
echo "6/9 arcade colours"
$PY -I arcade_colours.py "$G1" levels/arcade_g1.json levels/levels.json levels/arcade_colours.json "$OUT/arcade_colours.txt" >/dev/null
$PY -I g2_arcade_colours.py "$G2" levels/arcade_g2.json "$OUT/gauntlet2_levels.bin" "$OUT/arcade_colours.txt" >/dev/null
echo "7/9 the dragon picture";              $PY -I g2_sprites.py "$G2" levels/arcade_g2.json "$OUT/g2_sprites.bin" >/dev/null
echo "8/9 the narration";                  $PY -I g2_speech.py "$G2" --bin "$OUT/g2_speech.bin" >/dev/null
echo "9/9 the attract screens";            $PY -I g2_attract.py render_gauntlet2.py "$G2" levels/arcade_g2.json capture "$OUT/g2_attract.bin" >/dev/null
echo "done: gauntlet2_levels.bin, g2_sprites.bin, arcade_colours.txt, g2_speech.bin and g2_attract.bin are in $OUT"
