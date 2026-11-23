"""
g2_sprites.py -- pictures from arcade Gauntlet II for the host to draw (g2_sprites.bin).

Motion-object pictures are tiles from the graphics ROMs (4 planes of 0x18000 bytes, stored
inverted), laid out row by row: tile = picture + row * width + column (checked on the dragon);
colours from the motion-object palette ($910200 + 32 * colour, IRGB words) as the game loads it
for a maze (set-up 0x44AC2 + palette loader 0x43486, run in Unicorn).

Pictures written (id: what, picture, size in tiles, colour, maze whose palette is used):
  1: the dragon at rest -- 0x2740, 4x4, colour 8, maze 7 (its set-up word 0xA740, 0x5496E)
  2-9: the dragon awake, head high to low (pairs: head back / out) -- 0x2100-0x2170
  10: the dragon half-curled -- 0x2750

File: "G2SP", u16 count, then per picture: u16 id, u8 width, u8 height (pixels), width*height*4
bytes RGBA (alpha 0 = transparent).

Usage: python3 -I g2_sprites.py <gauntlet2.zip> <arcade_g2.json> <out g2_sprites.bin>
"""
import json, struct, sys
src = open(__file__.replace("g2_sprites.py", "g2_arcade_colours.py")).read()
src = src[:src.index("mazes = json.load")]
zip_path, mazes_path, out_path = sys.argv[1:4]
sys.argv = [sys.argv[0], zip_path, mazes_path, ""]
g = {"__name__": "g2_sprites_lib"}
exec(compile(src, "g2_arcade_colours.py", "exec"), g)
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_MEM_UNMAPPED, UcError
from unicorn.m68k_const import UC_M68K_REG_A7, UC_M68K_REG_SR, UC_CPU_M68K_M68020

def maze_palette(m):
    prog = g["prog"]
    img = bytearray(prog); img[0x38000:0x3A000] = prog[0x38000 + m["bank"] * 0x2000:0x38000 + (m["bank"] + 1) * 0x2000]
    uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN); uc.ctl_set_cpu_model(UC_CPU_M68K_M68020)
    uc.mem_map(0, 0x80000); uc.mem_write(0, bytes(img)); uc.mem_write(0x7FF00, b"\x4e\x72\x27\x00")
    uc.mem_map(0x800000, 0x10000); uc.mem_map(0x900000, 0x40000)
    def unmapped(uc, access, addr, size, value, ud):
        try: uc.mem_map((addr & 0xFFFFFF) & ~0xFFF, 0x1000)
        except UcError: return False
        return True
    uc.hook_add(UC_HOOK_MEM_UNMAPPED, unmapped)
    def run(entry, args):
        uc.mem_write(0x904E00, struct.pack(">I" + "I" * len(args), 0x7FF00, *args))
        uc.reg_write(UC_M68K_REG_SR, 0x2700); uc.reg_write(UC_M68K_REG_A7, 0x904E00)
        try: uc.emu_start(entry, 0x7FF00, count=20_000_000)
        except UcError: pass
    uc.mem_write(0x904004, b"\x00\x0c")
    run(0x44AC2, [m["window"]])
    pa, pb = struct.unpack(">HH", uc.mem_read(0x904B58, 4))
    run(0x43486, [pa, pb])
    g["last_bank"] = struct.unpack(">H", uc.mem_read(0x905F6E, 2))[0] & 3   # playfield tile bank
    return uc.mem_read(0x910000, 0x800)

def post_picture(k, pal):
    """Force-field post k (0-15: which sides have a beam, bits 1 up, 2 right, 4 down, 8 left; G2
    0x53398 sets them, 0x5E536 draws table 0x5BA70[k] -- 4 playfield tile words + 0x4000) as 16x16
    RGBA. Playfield tile = (bank * 0x1000 + (word & 0xFFF)) ^ 0x800, colour 0x28 + bits 12-14."""
    tile, irgb, prog = g["tile"], g["irgb"], g["prog"]
    p = struct.unpack(">I", prog[0x5BA70 + 4 * k:0x5BA74 + 4 * k])[0]
    out = bytearray(16 * 16 * 4)
    for j, (cx, cy) in enumerate(((0, 0), (1, 0), (0, 1), (1, 1))):
        w = (struct.unpack(">H", prog[p + 2 * j:p + 2 * j + 2])[0] + 0x4000) & 0xFFFF
        t = tile((g["last_bank"] * 0x1000 + (w & 0xFFF)) ^ 0x800); c = 0x28 + ((w >> 12) & 7); flip = w >> 15
        for y in range(8):
            for x in range(8):
                v = t[y * 8 + (7 - x if flip else x)]
                r, gg, b = irgb(struct.unpack(">H", pal[2 * (c * 16 + v):2 * (c * 16 + v) + 2])[0])
                o = ((cy * 8 + y) * 16 + cx * 8 + x) * 4
                out[o:o + 4] = bytes((r, gg, b, 255))
    return 16, 16, bytes(out)

def picture(code, wt, ht, colour, pal):
    tile, irgb = g["tile"], g["irgb"]
    w, h = wt * 8, ht * 8
    out = bytearray(w * h * 4)
    for ty in range(ht):
        for tx in range(wt):
            t = tile(code + ty * wt + tx)                 # row by row
            for y in range(8):
                for x in range(8):
                    v = t[y * 8 + x]
                    if not v: continue
                    r, gg, b = irgb(struct.unpack(">H", pal[0x200 + 32 * colour + 2 * v:0x202 + 32 * colour + 2 * v])[0])
                    o = ((ty * 8 + y) * w + tx * 8 + x) * 4
                    out[o:o + 4] = bytes((r, gg, b, 255))
    return w, h, bytes(out)

mazes = json.load(open(mazes_path))["mazes"]
# 1 the dragon asleep (curled up); 2-9 awake, facing left, head high -> low in four steps, each as
# a pair (head back, head out breathing) -- table 0x5D508 (animation 0x544AA), one of its four
# facings; 10 half-curled (waking up / dozing off)
# 11/12 curled / half-curled for the dragons facing east and north (table 0x5D508 groups 0-1);
# 20-27 facing east (0x2180-), 30-37 south (0x2200-), 40-47 north (0x2280-): same layout as 2-9
# (four head positions -- top to bottom for west/east, left to right for south/north -- each a
# pair: head back, head out)
pics = ([(1, 0x2740, 4, 4, 8, 7)] + [(2 + k, 0x2100 + 0x10 * k, 4, 4, 8, 7) for k in range(8)] + [(10, 0x2750, 4, 4, 8, 7)]
        + [(11, 0x2540, 4, 4, 8, 7), (12, 0x2550, 4, 4, 8, 7)]
        + [(20 + k, 0x2180 + 0x10 * k, 4, 4, 8, 7) for k in range(8)]
        + [(30 + k, 0x2200 + 0x10 * k, 4, 4, 8, 7) for k in range(8)]
        + [(40 + k, 0x2280 + 0x10 * k, 4, 4, 8, 7) for k in range(8)]
        # 50-59 the acid puddle's 10 frames (code 0x19: picture 0x2300, 3x3, colour 1; frames 9 tiles
        # apart), 60 the locked chest (code 0x2F: 0x25E4), 61 the skull flask after it (0x25ED)
        + [(50 + k, 0x2300 + 9 * k, 3, 3, 1, 3) for k in range(10)]
        + [(60, 0x25E4, 3, 3, 1, 3), (61, 0x25ED, 3, 3, 1, 3)]
        # 90-113 the mugger: id 90 + 3 * direction (N NE E SE S SW W NW) + walking frame (same as g2pics.c)
        + [(90 + 3 * d + f, c + 9 * f, 3, 3, 1, 9) for d, c in enumerate((0x24C6, 0x24AB, 0x2490, 0x2436, 0x2451, 0x2400, 0x2475, 0x241B)) for f in range(3)])
POSTS = 16                                            # 70-85: force-field posts by beam sides
blob = bytearray(b"G2SP" + struct.pack(">H", len(pics) + POSTS))
for pid, code, wt, ht, colour, maze in pics:
    w, h, px = picture(code, wt, ht, colour, maze_palette(mazes[maze - 1]))
    blob += struct.pack(">HBB", pid, w, h) + px
    print("picture %d: %04x %dx%d colour %d (maze %d palette)" % (pid, code, w, h, colour, maze))
pal100 = maze_palette(mazes[99])                     # maze 100 has force fields
for k in range(POSTS):
    w, h, px = post_picture(k, pal100)
    blob += struct.pack(">HBB", 70 + k, w, h) + px
print("pictures 70-85: force-field posts (table 0x5BA70, maze 100 palette)")
open(out_path, "wb").write(blob)
print("wrote %s (%d bytes)" % (out_path, len(blob)))
