"""
g2_arcade_colours.py -- arcade Gauntlet II floor/wall colours for the Gauntlet II level set.

For every maze, runs (in Unicorn, as render_gauntlet2.py does) Gauntlet II's maze set-up
0x44AC2(record) and its palette loader 0x43486(header byte 6 high, low), then counts the pixels of
each playfield colour of the floor (colour group 0: palette $910500) and the walls (colour group 7
in Gauntlet II: palette $9105E0), IRGB words, and makes 3 shades each (dark, mid, light) as arcade_colours.py does
for the 1985 game: 3 bands of equal pixel weight by brightness, the pixel-weighted mean of each.

The lines are keyed by where the host loads each maze record (0x380000 + its offset in
gauntlet2_levels.bin), so the host's arcade-colours switch finds them in Gauntlet II mode. They
replace any earlier Gauntlet II lines in the colours file; the Gauntlet 4 lines are kept.

Usage: python3 -I g2_arcade_colours.py <gauntlet2.zip> <arcade_g2.json> <gauntlet2_levels.bin> <arcade_colours.txt> [more colour files]
"""
import collections, json, struct, sys, zipfile
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_MEM_UNMAPPED, UcError
from unicorn.m68k_const import UC_M68K_REG_A7, UC_M68K_REG_SR, UC_CPU_M68K_M68020

zip_path, mazes_path, levels_path = sys.argv[1:4]
colour_files = sys.argv[4:]
z = zipfile.ZipFile(zip_path); rd = z.read
def il(a, b): return bytes(x for p in zip(a, b) for x in p)
prog = bytearray(0x80000)
def cont(e, o, hi, lo):
    a, b = rd(e), rd(o); prog[hi:hi + 0x8000] = il(a[:0x4000], b[:0x4000]); prog[lo:lo + 0x8000] = il(a[0x4000:], b[0x4000:])
cont("136037-1307.9a", "136037-1308.9b", 0x8000, 0x0)
prog[0x38000:0x40000] = il(rd("136043-1105.10a"), rd("136043-1106.10b"))
cont("136043-1109.7a", "136043-1110.7b", 0x48000, 0x40000)
cont("136043-1121.6a", "136043-1122.6b", 0x58000, 0x50000)
gfx = bytearray(0x60000)
for off, name, size in ((0x00000, "136043-1111.1a", 0x8000), (0x08000, "136037-112.1b", 0x8000), (0x10000, "136043-1123.1c", 0x4000),
                        (0x14000, "136043-1123.1c", 0x4000), (0x18000, "136043-1113.1l", 0x8000), (0x20000, "136037-114.1mn", 0x8000),
                        (0x28000, "136043-1124.1p", 0x4000), (0x2C000, "136043-1124.1p", 0x4000), (0x30000, "136043-1115.2a", 0x8000),
                        (0x38000, "136037-116.2b", 0x8000), (0x40000, "136043-1125.2c", 0x4000), (0x44000, "136043-1125.2c", 0x4000),
                        (0x48000, "136043-1117.2l", 0x8000), (0x50000, "136037-118.2mn", 0x8000), (0x58000, "136043-1126.2p", 0x4000),
                        (0x5C000, "136043-1126.2p", 0x4000)):
    gfx[off:off + size] = rd(name)[:size]
gfx = bytes(255 - x for x in gfx)
PLANE = 0x18000
def tile(code):
    code %= PLANE // 8
    out = []
    for r in range(8):
        p = [gfx[q * PLANE + code * 8 + r] for q in (3, 2, 1, 0)]
        out.extend(sum(((p[k] >> (7 - x)) & 1) << (3 - k) for k in range(4)) for x in range(8))
    return out
def irgb(w):
    i = ((w >> 12) & 15) * 17
    return tuple((i * ((w >> s) & 15) * 17) >> 8 for s in (8, 4, 0))
lum = lambda c: 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]
STOP = 0x7FF00

def maze_colours(m):
    img = bytearray(prog); img[0x38000:0x3A000] = prog[0x38000 + m["bank"] * 0x2000:0x38000 + (m["bank"] + 1) * 0x2000]
    uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN); uc.ctl_set_cpu_model(UC_CPU_M68K_M68020)
    uc.mem_map(0, 0x80000); uc.mem_write(0, bytes(img)); uc.mem_write(STOP, b"\x4e\x72\x27\x00")
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
        except UcError: pass
    run(0x44AC2, [m["window"]])
    pa, pb = struct.unpack(">HH", uc.mem_read(0x904B58, 4))
    run(0x43486, [pa, pb])
    pf = struct.unpack(">4096H", uc.mem_read(0x900000, 0x2000))
    pal = uc.mem_read(0x910000, 0x800)
    bank = struct.unpack(">H", uc.mem_read(0x905F6E, 2))[0] & 3
    use = collections.defaultdict(collections.Counter)        # the playfield: floor = colour group 0,
    for w, n in collections.Counter(pf).items():               # walls = colour group 7 (palette 0x28 + group)
        g = (w >> 12) & 7
        name = "floor" if g == 0 else "wall" if g == 7 else None
        if not name: continue
        for v in tile(((bank * 0x1000) + (w & 0xFFF)) ^ 0x800):
            if v: use[name][(0x28 + g) * 32 + 2 * v] += n
    res = {}
    for name in ("floor", "wall"):
        cols = [(irgb(struct.unpack(">H", pal[a:a + 2])[0]), n) for a, n in use[name].items()]
        cols.sort(key=lambda cn: lum(cn[0]))
        total = sum(n for _, n in cols) or 1
        bands = [[0, 0, 0, 0] for _ in range(3)]; acc = 0
        for c, n in cols:
            k = min(2, int(3 * (acc + n / 2) / total)); acc += n
            for i in range(3): bands[k][i] += c[i] * n
            bands[k][3] += n
        shades = [[round(b[i] / b[3]) for i in range(3)] if b[3] else None for b in bands]
        if all(s is None for s in shades): shades = [[64, 64, 64], [128, 128, 128], [192, 192, 192]]
        for k in range(3):
            if shades[k] is None: shades[k] = shades[k - 1] if k else next(s for s in shades if s)
        res[name] = shades
    return res

mazes = json.load(open(mazes_path))["mazes"]
B = open(levels_path, "rb").read()
count, version = struct.unpack(">HH", B[4:8])
versions = 4 if version >= 2 else 1                     # version 2: four mirror versions per maze
lines = []
for k, m in enumerate(mazes[:count]):
    if m.get("bank") is None: continue
    c = maze_colours(m)
    for v in range(versions):                           # same colours for every mirror version
        slot = k * versions + v
        rec = 0x380000 + struct.unpack(">I", B[72 + 4 * slot:76 + 4 * slot])[0]
        lines.append("%06x %s" % (rec, " ".join(str(x) for shade in c["floor"] + c["wall"] for x in shade)))
    print("maze %3d: floor %s wall %s" % (k + 1, c["floor"][1], c["wall"][1]), flush=True)

HEAD = "# arcade Gauntlet II floor/wall shades per Gauntlet II maze (record address as loaded by the host): made by g2_arcade_colours.py"
for path in colour_files:
    old = open(path).read().splitlines()
    keep, skip = [], False
    for l in old:
        if l == HEAD: skip = True; continue
        if skip and (l.startswith("#") or not l.strip()): skip = False
        if skip: continue
        keep.append(l)
    open(path, "w", newline="\n").write("\n".join(keep + [HEAD] + lines) + "\n")
    print("wrote %d Gauntlet II lines into %s" % (len(lines), path))
