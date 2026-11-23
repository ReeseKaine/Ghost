#!/usr/bin/env python3
"""
arcade_colours.py -- the floor and wall colours of every arcade Gauntlet maze, as Gauntlet 4
needs them: 3 shades each (dark, mid, light), full 24-bit RGB.

For each maze it runs the arcade's own code in Unicorn (68020 model; the arcade has a 68010):
the maze loader 0x425A0(record) fills playfield RAM, the palette loader 0x413AA(a, b) fills
palette RAM from the maze's colour numbers (header byte 9: a = high nibble, b = low). Then it
decodes the playfield tiles (graphics ROMs 111-118: 4 planes, inverted; tile = ((bank * 0x1000)
+ (word & 0xFFF)) ^ 0x800, bank = Y scroll & 3; colour group = bits 12-14 at palette
$910500 + 32 * group) and counts the pixels of each colour: group 0 is the floor, 1 the walls.
The colours used are split into 3 brightness bands with equal pixel weight; each band's
pixel-weighted mean is one shade.

Also writes the text file the native host reads (host/g4host.c, "arcade colours"): one line
per Gauntlet 4 Arcade record (both of its tables: Game Type normal 0x25BC4 and original 0x25DC4)
  <record address, hex> <floor dark r g b> <floor mid> <floor light> <wall dark> <wall mid> <wall light>

Usage: python3 -I arcade_colours.py <gauntlet.zip> <arcade_g1.json> <levels.json> <out.json> <out.txt>
"""
import collections, json, struct, sys, zipfile
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_MEM_UNMAPPED, UcError
from unicorn.m68k_const import UC_M68K_REG_A7, UC_M68K_REG_SR, UC_CPU_M68K_M68020

zip_path, mazes_path, g4levels_path, out_path, txt_path = sys.argv[1:6]
z = zipfile.ZipFile(zip_path); rd = z.read
def il(a, b): return bytes(x for p in zip(a, b) for x in p)
prog = bytearray(0x80000)                      # MAME's layout (ROM_CONTINUE: first half above)
a, b = rd("136037-1307.9a"), rd("136037-1308.9b"); prog[0x8000:0x10000] = il(a[:0x4000], b[:0x4000]); prog[0:0x8000] = il(a[0x4000:], b[0x4000:])
prog[0x38000:0x40000] = il(rd("136037-205.10a"), rd("136037-206.10b"))
a, b = rd("136037-1409.7a"), rd("136037-1410.7b"); prog[0x48000:0x50000] = il(a[:0x4000], b[:0x4000]); prog[0x40000:0x48000] = il(a[0x4000:], b[0x4000:])
BANKS = bytes(prog[0x38000:0x40000])
gfx = bytes(255 - x for x in b"".join(rd("136037-" + n) for n in
            ("111.1a", "112.1b", "113.1l", "114.1mn", "115.2a", "116.2b", "117.2l", "118.2mn")))
def tile(code):
    out = []
    for r in range(8):
        p = [gfx[q * 0x10000 + code * 8 + r] for q in (3, 2, 1, 0)]
        out += [sum(((p[k] >> (7 - x)) & 1) << (3 - k) for k in range(4)) for x in range(8)]
    return out
def irgb(w):
    i = ((w >> 12) & 15) * 17
    return tuple((i * ((w >> s) & 15) * 17) >> 8 for s in (8, 4, 0))
lum = lambda c: 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]

STOP = 0x7FF00
def run(uc, entry, args):
    sp = 0x904E00
    uc.mem_write(sp, struct.pack(">I" + "I" * len(args), STOP, *args))
    uc.reg_write(UC_M68K_REG_SR, 0x2700); uc.reg_write(UC_M68K_REG_A7, sp)
    uc.emu_start(entry, STOP, count=5_000_000)

def maze_colours(m):
    img = bytearray(prog)
    img[0x38000:0x3A000] = BANKS[m["bank"] * 0x2000:(m["bank"] + 1) * 0x2000]
    uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN); uc.ctl_set_cpu_model(UC_CPU_M68K_M68020)
    uc.mem_map(0, 0x80000); uc.mem_write(0, bytes(img))
    uc.mem_map(0x800000, 0x10000); uc.mem_map(0x900000, 0x40000)
    uc.mem_write(STOP, b"\x4e\x72\x27\x00")
    def unmapped(uc, access, addr, size, value, ud):
        try: uc.mem_map((addr & 0xFFFFFF) & ~0xFFF, 0x1000)
        except UcError: return False
        return True
    uc.hook_add(UC_HOOK_MEM_UNMAPPED, unmapped)
    run(uc, 0x425A0, [m["window"]])
    pa, pb = struct.unpack(">HH", uc.mem_read(0x904964, 4))
    run(uc, 0x413AA, [pa, pb])
    pf = uc.mem_read(0x900000, 0x2000); pal = uc.mem_read(0x910000, 0x800)
    bank = struct.unpack(">H", uc.mem_read(0x905F6E, 2))[0] & 3
    words = collections.Counter(struct.unpack(">4096H", pf))
    use = collections.defaultdict(collections.Counter)
    for w, n in words.items():
        g = (w >> 12) & 7
        for v in tile(((bank * 0x1000) + (w & 0xFFF)) ^ 0x800): use[g][v] += n
    res = {"colour_numbers": [pa, pb]}
    for name, g in (("floor", 0), ("wall", 1)):
        cols = [(irgb(struct.unpack(">H", pal[0x500 + 32 * g + 2 * v:0x502 + 32 * g + 2 * v])[0]), n)
                for v, n in use[g].items() if v != 0]           # colour 0: transparent
        cols.sort(key=lambda cn: lum(cn[0]))
        total = sum(n for _, n in cols) or 1
        bands = [[0, 0, 0, 0] for _ in range(3)]; acc = 0
        for c, n in cols:                                      # 3 bands of equal pixel weight
            k = min(2, int(3 * (acc + n / 2) / total)); acc += n
            for i in range(3): bands[k][i] += c[i] * n
            bands[k][3] += n
        shades = [[round(b[i] / b[3]) for i in range(3)] if b[3] else None for b in bands]
        for k in range(3):                                     # an empty band: its neighbour
            if shades[k] is None: shades[k] = shades[k - 1] if k else next(s for s in shades if s)
        res[name] = shades
        res[name + "_used"] = [[list(c), n] for c, n in cols]
    return res

mazes = json.load(open(mazes_path))["mazes"]
out = {}
for m in mazes:
    out[m["level"]] = maze_colours(m)
    if m["level"] % 20 == 0: print("maze", m["level"], file=sys.stderr)
json.dump(dict(source="arcade Gauntlet, run through its own maze and palette loaders", levels=out), open(out_path, "w"))
g4 = json.load(open(g4levels_path))["levels"]
lines = {}
for slot in g4:
    if slot["table"] in ("arcade", "arcade_alt") and 1 <= slot["level"] <= len(mazes):
        c = out[slot["level"]]
        lines[slot["record"]] = " ".join(str(v) for shade in c["floor"] + c["wall"] for v in shade)
with open(txt_path, "w") as f:
    f.write("# arcade Gauntlet floor/wall shades per Gauntlet 4 record: made by arcade_colours.py from the arcade ROMs\n")
    for rec in sorted(lines): f.write("%06x %s\n" % (rec, lines[rec]))
print(len(lines), "records ->", txt_path)
for lv in (1, 2, 3, 8, 50):
    print(lv, "floor", out[lv]["floor"], "wall", out[lv]["wall"])
