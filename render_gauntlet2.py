#!/usr/bin/env python3
"""
render_gauntlet2.py -- picture of an arcade Gauntlet II maze as the game itself sets it up.

Runs, in Unicorn (68020 model for the 68010), the game's maze setup 0x44AC2(record) -- which
decodes the maze with 0x4C1BC and builds it with the cell routine 0x45E40 -- and the palette
loader 0x43486(header byte 6 high, low). Then draws the 64 x 64 playfield (8x8 tiles, column
order; tile = ((Y scroll & 3) * 0x1000 + (word & 0xFFF)) ^ 0x800, colour = 0x28 + bits 12-14,
bit 15 = flip x; graphics: spr_tiles, 4 planes of 0x18000 bytes, stored inverted; palette:
IRGB words, rgb = (I*17 * C*17) >> 8) and the motion objects (RAM 0x902000, split: word 0 tile
at +0, word 1 X/colour at +0x800, word 2 Y/size/flip at +0x1000, word 3 link at +0x1800).

Usage: python3 -I render_gauntlet2.py <gauntlet2.zip> <arcade_g2.json> <maze 1-117> <out.png>
"""
import json, struct, sys, zipfile
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_MEM_UNMAPPED, UcError
from unicorn.m68k_const import UC_M68K_REG_A7, UC_M68K_REG_SR, UC_M68K_REG_PC, UC_CPU_M68K_M68020
from PIL import Image

zip_path, mazes_path, which, out_png = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
LABELS = "--labels" in sys.argv
z = zipfile.ZipFile(zip_path); rd = z.read
def il(a, b): return bytes(x for p in zip(a, b) for x in p)
prog = bytearray(0x80000)
def cont(e, o, hi, lo):
    a, b = rd(e), rd(o); prog[hi:hi + 0x8000] = il(a[:0x4000], b[:0x4000]); prog[lo:lo + 0x8000] = il(a[0x4000:], b[0x4000:])
cont("136037-1307.9a", "136037-1308.9b", 0x8000, 0x0)
prog[0x38000:0x40000] = il(rd("136043-1105.10a"), rd("136043-1106.10b"))
cont("136043-1109.7a", "136043-1110.7b", 0x48000, 0x40000)
cont("136043-1121.6a", "136043-1122.6b", 0x58000, 0x50000)
# spr_tiles 0x60000 (MAME gaunt2): 0x4000 chips loaded twice
def g(n): return rd(n)
gfx = bytearray(0x60000)
for off, name, size in ((0x00000, "136043-1111.1a", 0x8000), (0x08000, "136037-112.1b", 0x8000), (0x10000, "136043-1123.1c", 0x4000),
                        (0x14000, "136043-1123.1c", 0x4000), (0x18000, "136043-1113.1l", 0x8000), (0x20000, "136037-114.1mn", 0x8000),
                        (0x28000, "136043-1124.1p", 0x4000), (0x2C000, "136043-1124.1p", 0x4000), (0x30000, "136043-1115.2a", 0x8000),
                        (0x38000, "136037-116.2b", 0x8000), (0x40000, "136043-1125.2c", 0x4000), (0x44000, "136043-1125.2c", 0x4000),
                        (0x48000, "136043-1117.2l", 0x8000), (0x50000, "136037-118.2mn", 0x8000), (0x58000, "136043-1126.2p", 0x4000),
                        (0x5C000, "136043-1126.2p", 0x4000)):
    gfx[off:off + size] = g(name)[:size]
gfx = bytes(255 - x for x in gfx)
PLANE = 0x18000
def tile(code):
    code %= PLANE // 8
    out = []
    for r in range(8):
        p = [gfx[q * PLANE + code * 8 + r] for q in (3, 2, 1, 0)]
        out.append([sum(((p[k] >> (7 - x)) & 1) << (3 - k) for k in range(4)) for x in range(8)])
    return out
def irgb(w):
    i = ((w >> 12) & 15) * 17
    return tuple((i * ((w >> s) & 15) * 17) >> 8 for s in (8, 4, 0))

m = json.load(open(mazes_path))["mazes"][which - 1]
img = bytearray(prog); img[0x38000:0x3A000] = prog[0x38000 + m["bank"] * 0x2000:0x38000 + (m["bank"] + 1) * 0x2000]
uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN); uc.ctl_set_cpu_model(UC_CPU_M68K_M68020)
uc.mem_map(0, 0x80000); uc.mem_write(0, bytes(img)); STOP = 0x7FF00; uc.mem_write(STOP, b"\x4e\x72\x27\x00")
uc.mem_map(0x800000, 0x10000); uc.mem_map(0x900000, 0x40000)
def unmapped(uc, access, addr, size, value, ud):
    try: uc.mem_map((addr & 0xFFFFFF) & ~0xFFF, 0x1000)
    except UcError: return False
    return True
uc.hook_add(UC_HOOK_MEM_UNMAPPED, unmapped)
def run(entry, args):
    sp = 0x904E00
    uc.mem_write(sp, struct.pack(">I" + "I" * len(args), STOP, *args))
    uc.reg_write(UC_M68K_REG_SR, 0x2700); uc.reg_write(UC_M68K_REG_A7, sp)
    try: uc.emu_start(entry, STOP, count=20_000_000)
    except UcError as e: print("stopped:", e, "pc %x" % uc.reg_read(UC_M68K_REG_PC))
from unicorn import UC_HOOK_CODE
special = {}                                             # cell index -> code, from the cell routine's calls
def put(uc, addr, size, ud):
    sp = uc.reg_read(UC_M68K_REG_A7); ret, i, c, n = struct.unpack(">IIII", uc.mem_read(sp, 16))
    for k in range(i & 0xFFFF, (i & 0xFFFF) + (n & 0xFFFF)):
        if c & 0xFFFF: special[k] = c & 0xFFFF
uc.hook_add(UC_HOOK_CODE, put, begin=0x45E40, end=0x45E40)
run(0x44AC2, [m["window"]])
pa, pb = struct.unpack(">HH", uc.mem_read(0x904B58, 4))
run(0x43486, [pa, pb])
pf = struct.unpack(">4096H", uc.mem_read(0x900000, 0x2000))
pal = [irgb(w) for w in struct.unpack(">1024H", uc.mem_read(0x910000, 0x800))]
bank = struct.unpack(">H", uc.mem_read(0x905F6E, 2))[0] & 3
mob = [struct.unpack(">1024H", uc.mem_read(0x902000 + 0x800 * k, 0x800)) for k in range(4)]

W = 512
pic = Image.new("RGB", (W, W)); px = pic.load()
for col in range(64):
    for row in range(64):
        w = pf[col * 64 + row]
        t = tile((bank * 0x1000 + (w & 0xFFF)) ^ 0x800); c = 0x28 + ((w >> 12) & 7); flip = w >> 15
        for y in range(8):
            for x in range(8):
                v = t[y][7 - x if flip else x]
                px[col * 8 + x, row * 8 + y] = pal[c * 16 + v]
used = 0
for i in (range(1024) if "--nosprites" not in sys.argv else ()):
    code = mob[0][i] & 0x7FFF; w1, w2 = mob[1][i], mob[2][i]
    if not code and not w1 and not w2: continue
    used += 1
    color = w1 & 15; x0 = w1 >> 7; y0 = w2 >> 7; wt = ((w2 >> 3) & 7) + 1; ht = (w2 & 7) + 1; flip = (w2 >> 6) & 1
    for tx in range(wt):
        for ty in range(ht):
            t = tile(code + tx * ht + ty)
            for y in range(8):
                for x in range(8):
                    v = t[y][7 - x if flip else x]
                    if v:
                        X = (x0 + (wt - 1 - tx if flip else tx) * 8 + x) % W; Y = (y0 + ty * 8 + y) % W
                        px[X, Y] = pal[0x100 + color * 16 + v]
pic = pic.resize((W * 2, W * 2), Image.NEAREST)
if LABELS:
    from PIL import ImageDraw
    d = ImageDraw.Draw(pic)
    for k, c in special.items():
        x, y = k % 32, k // 32                             # row-major
        d.rectangle((x * 32, y * 32, x * 32 + 31, y * 32 + 31), outline=(255, 0, 255))
        d.text((x * 32 + 8, y * 32 + 10), "%02x" % c, fill=(0, 255, 0))
pic.save(out_png)
if "--cells" in sys.argv:                                   # the specials, row-major (index = y*32 + x)
    json.dump({"%d" % k: c for k, c in special.items()}, open(out_png + ".cells.json", "w"))
print("maze %d: bank %d, palette %d/%d, tile bank %d, %d motion objects; header %s" % (which, m["bank"], pa, pb, bank, used, bytes(m["header"]).hex(" ")))
