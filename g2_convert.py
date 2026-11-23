#!/usr/bin/env python3
"""
g2_convert.py -- convert the arcade Gauntlet II mazes (levels/arcade_g2.json, made by
import_gauntlet2.py) into Gauntlet 4 level records, and check every one by decoding it with
Gauntlet 4's own map decoder (the C translation of 0x19122, verified against the ROM).

Gauntlet 4 record (14-byte header + map stream):
  0-2   initials (not read): "g2" + 0      3-6  0      7  options: 0
  8     high: floor pattern, low: wall pattern   <- G2 byte 5 (wall pattern > 4 -> 0)
  9     high: floor colour, low: wall colour     <- G2 byte 6, each set mapped to the nearest
                                                    first-game set (levels/g2_colour_to_g4.json)
  10-13 dictionary P0-P3 (unused by this encoder; P1 seeds 'last'): 1 1 1 1
  14..  stream; this encoder uses three commands:
        00tttttt  one tile t         100nnnnn  repeat the last tile n+1 times
        110nnnnn  n+1 floor tiles    (row 0 is always wall; the stream starts at cell 0x20)
The decoder produces raw tiles 0-63 that a remap table turns into Gauntlet 4 codes; Gauntlet II
levels use their own table (REMAP below): Gauntlet 4's Arcade table with four unused raw slots
given to trap group 2 and 3 (walls 0B/0C, traps 15/16), which the Arcade table lacks.

Codes come from levels/g2_to_g4_mapping.json; features Gauntlet 4 lacks get the stand-ins in
STAND_IN until they exist. The first player start (0F) is kept; further ones become floor.

Gauntlet II features the host adds itself (host/g4mods.c) are listed per maze ("extras"):
amulets (5 repulsiveness, 6 reflective shots, 7 transportability, 8 super shots, 9 invulnerability),
cycling walls (10-12: phase 1-3, mazes with rule 0x800), maze rules on row 0 (13 at cell 0: one trap group starts open, rule 0x1000; 16 at cell 1: two groups,
rule 0x2000; 17 at cell 2: traps open random groups, rule 0x08), 14 secret wall (G2 04: "FIND SECRET
WALLS FOR EXTRA FOOD/POTIONS": a shot removes it), 15 random wall (G2 06: "SOME WALLS MOVE RANDOMLY"), 18 exit to 6 (G2 11), 20 acid puddle start (G2 19), 21 IT start (G2 1B), 22 locked chest (G2 2F, a door cell), 23 moveable wall (G2 03), 24 super sorcerer (G2 1A, a sorcerer), 25 dragon (G2 3C, levels/g2_dragons.json), exit rules on row 0 (26 at cell 3: one random
real exit, 27 at cell 4: it moves, 28 at cell 5: the others are fake), 0x80|goal at cell 6: the maze's secret-trick goal (header byte 0), 29 at cell 7: player shots stun
others, 30 at cell 8: player shots hurt others, 31 at cell 9: all walls invisible, 32 at cell 10: trap walls invisible, stun tiles (1), force-field posts (2) and the cells of their beams (3 horizontal, 4 vertical), paired by Gauntlet
II's own rule (0x53398): from a post, the nearest post to the right (up to 15 cells) or below
(up to 16 rows) across open floor -- walls and fixed objects block, touching posts do not pair.

Usage: python3 -I g2_convert.py <gauntlet4.bin> <libg4.so> <out.bin>
Writes out.bin: "G4L2", u16 count (mazes), u16 version (2), 64-byte remap table, 4*count x u32
record offsets, 4*count x u32 extras offsets (from the start of the file; entry 4*(maze-1) + v,
v = mirror version: bit 0 left-right, bit 1 top-bottom), the records, the extras
(per maze: u16 n, then n x (u16 cell, u8 type, u8 0)). Also levels/g2_records.json (per level: record bytes hex,
stand-ins used).
"""
import ctypes, json, os, struct, sys, collections

g4_path, lib_path, out_path = sys.argv[1:4]
HERE = os.path.dirname(os.path.abspath(__file__))
G4 = open(g4_path, "rb").read()
mazes = json.load(open(os.path.join(HERE, "levels", "arcade_g2.json")))["mazes"]
mapping = {r["g2"]: r for r in json.load(open(os.path.join(HERE, "levels", "g2_to_g4_mapping.json")))}
colours = json.load(open(os.path.join(HERE, "levels", "g2_colour_to_g4.json")))

ARCADE_REMAP = list(G4[0x31D2E:0x31D2E + 64])
REMAP = list(ARCADE_REMAP)
REMAP[0x02] = 0x0B          # was floor (unused by the first game)  -> trap wall group 1
REMAP[0x29] = 0x0C          # was item 1F (unidentified, unused)     -> trap wall group 2
REMAP[0x3C] = 0x15          # was marker slot FD (unused)            -> trap group 1
REMAP[0x3D] = 0x16          # was marker slot FE (unused)            -> trap group 2
RAW = {}                    # Gauntlet 4 code -> raw tile
for r, g in enumerate(REMAP):
    RAW.setdefault(g, r)
RAW[0x00] = 0x00

# Gauntlet II code -> Gauntlet 4 code (from the mapping; stand-ins where Gauntlet 4 has none yet)
TARGET = {
    0x00: 0x00, 0x02: 0x09, 0x04: 0x09, 0x06: 0x09,
    0x07: 0x0A, 0x08: 0x0B, 0x09: 0x0C,          # trap walls, groups 0-2
    0x0A: 0x14, 0x0B: 0x15, 0x0C: 0x16,          # traps, groups 0-2
    0x0D: 0x07, 0x0E: 0x08, 0x0F: 0xFC, 0x10: 0x04, 0x11: 0x04,
    0x12: 0x82, 0x13: 0x8A, 0x17: 0x8A, 0x14: 0x92, 0x15: 0xA2, 0x16: 0x9A, 0x18: 0xA8,
    0x2E: 0x1E, 0x30: 0x1E, 0x31: 0x22, 0x32: 0x25, 0x33: 0x23, 0x34: 0x24, 0x35: 0x1B,
    0x36: 0x1D, 0x3E: 0x13,
    0x05: 0x10,                                   # destructible wall: Gauntlet 4's 3-stage wall 10 -> 0F -> 0E
    0x3F: 0x09,                                   # force-field post: solid (G2 draws it as a wall); the host runs its beams
}
for c in range(0x1C, 0x2E):                     # generators: ghost, grunt, demon, lobber, sorcerer, grunt
    base = [0x84, 0x8C, 0x94, 0xA4, 0x9C, 0x8C][(c - 0x1C) // 3]
    TARGET[c] = base + (c - 0x1C) % 3
STAND_IN = {                                    # new features: what they are until they exist
    0x01: (0x00, "stun tile (host)"), 0x03: (0x09, "moveable wall (host)"),
    0x19: (0x00, "acid puddle (host)"), 0x1A: (0x9A, "super sorcerer (host)"), 0x1B: (0x00, "IT (host)"),
    0x2F: (0x07, "locked treasure chest (host)"), 0x37: (0x00, "repulsiveness amulet (host)"),
    0x38: (0x00, "reflective shots amulet (host)"), 0x39: (0x00, "transportability amulet (host)"),
    0x3A: (0x00, "super shots amulet (host)"), 0x3B: (0x00, "invulnerability amulet (host)"),
    0x3C: (0x00, "dragon"), 0x3D: (0x00, "unused"),
}

def rule(hdr): return (hdr[1] << 24) | (hdr[2] << 16) | (hdr[3] << 8) | hdr[4]
CYCLING = 0x800      # rule flag: walls 07/08/09 cycle (0x52F26/0x5E62A) instead of waiting for traps
ONE_OPEN = 0x1000    # rule flag: at the start one trap group, chosen at random, is open (0x43BA0 -> 0x5E7A6)
TWO_OPEN = 0x2000    # rule flag: two neighbouring trap groups (g, g+1) start open (0x43BCC)
SHUFFLE = 0x08       # rule flag: every trap opens the group (its own + random 0-2) mod 3 (0x439B0, 0x43A5C)

def convert_map(gmap, cycling=False):
    """Gauntlet II cell codes (row-major) -> raw tiles; and the stand-ins used."""
    raw, used, start = [], collections.Counter(), False
    for i, c in enumerate(gmap):
        if c == 0x0F:
            if start: c = 0x00
            start = True
        if cycling and c in (0x07, 0x08, 0x09): g = 0x09    # plain wall; the host cycles it (extras 10-12)
        elif c in TARGET: g = TARGET[c]
        else: g, why = STAND_IN[c]; used[why] += 1
        if i < 32: g = 0x09                       # row 0: the decoder makes it wall anyway
        raw.append(RAW[g])
    return raw, used

def encode(raw):
    out, i = bytearray(), 0x20
    while i < 1024:
        t = raw[i]
        if t == 0:
            n = 1
            while i + n < 1024 and raw[i + n] == 0 and n < 32: n += 1
            out.append(0xC0 | (n - 1)); i += n
            continue
        out.append(t & 0x3F); i += 1
        n = 0
        while i + n < 1024 and raw[i + n] == t and n < 32: n += 1
        if n:
            out.append(0x80 | (n - 1)); i += n
    return bytes(out)

def header(h):
    b5, b6 = h[5], h[6]
    floor_pat, wall_pat = b5 >> 4, b5 & 15
    if wall_pat > 4: wall_pat = 0
    fc, wc = colours["floor"][b6 >> 4], colours["wall"][b6 & 15]
    # byte 7, Gauntlet 4's options (read at 0x2422): bit 3 horizontal wrap ($FFE7B6), bit 2 vertical
    # wrap ($FFE7B7) -- Gauntlet II's rule bits 0x20 / 0x10 ($90491F bits 5 / 4, camera clamp skipped)
    opts = (0x08 if h[4] & 0x20 else 0) | (0x04 if h[4] & 0x10 else 0)
    return bytes([ord("g"), ord("2"), 0, 0, 0, 0, 0, opts, (floor_pat << 4) | wall_pat, (fc << 4) | wc, 1, 1, 1, 1])

# Gauntlet 4's decoder (identity remap so the raw tiles come out) -- as in import_arcade.py
lib = ctypes.CDLL(os.path.abspath(lib_path))
class M68K(ctypes.Structure):
    _fields_ = [("d", ctypes.c_uint32 * 8), ("a", ctypes.c_uint32 * 8), ("x", ctypes.c_uint32),
                ("z", ctypes.c_uint32), ("sr", ctypes.c_uint32), ("usp", ctypes.c_uint32),
                ("n", ctypes.c_uint32), ("v", ctypes.c_uint32), ("cf", ctypes.c_uint32)]
EXT_CB = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(M68K), ctypes.c_uint32)
RD_CB = ctypes.CFUNCTYPE(ctypes.c_uint32, ctypes.c_uint32, ctypes.c_int)
WR_CB = ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_int)
lib.g4_test_run.argtypes = [ctypes.POINTER(M68K), ctypes.c_uint32]
lib.g4_test_set.argtypes = [EXT_CB, RD_CB, WR_CB]
MEM = (ctypes.c_uint8 * 0x1000000)(); ctypes.memmove(MEM, G4, len(G4))
ctypes.c_void_p.in_dll(lib, "g4_mem").value = ctypes.addressof(MEM)
ctypes.c_int.in_dll(lib, "g4_use_translated").value = 1
def no_ext(cp, t): raise SystemExit(f"unexpected call to untranslated code {t:#x}")
CBS = (EXT_CB(no_ext), RD_CB(lambda a, s: 0), WR_CB(lambda a, v, s: None)); lib.g4_test_set(*CBS)
def wr(addr, data): ctypes.memmove(ctypes.addressof(MEM) + addr, bytes(data), len(data))
def rd(addr, n): return ctypes.string_at(ctypes.addressof(MEM) + addr, n)
def decode(record):
    wr(0x0F0000, record + bytes(64))
    wr(0x31D2E, range(64)); wr(0xFFE838, b"\0\0")
    wr(0xFFFD00 - 4, struct.pack(">I", 0x0F0000))
    cpu = M68K(); cpu.a[4] = 0xFFE246; cpu.a[7] = 0xFFFD00 - 4
    assert lib.g4_test_run(ctypes.byref(cpu), 0x019122) == 0
    out = list(rd(0xFFE246, 0x400)); wr(0x31D2E, ARCADE_REMAP); return out

# cells Gauntlet II draws as fixed playfield pieces (motion-object picture 0x8000/0x8001): they block beams
FIXED = {0x02, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x3F, 0x01, 0x0A, 0x0B, 0x0C, 0x10, 0x11, 0x3E}
DRAGONS = {int(k): v for k, v in json.load(open(os.path.join(HERE, "levels", "g2_dragons.json")))["dragons"].items()}
def extras(gmap, cycling=False, rules=0, level=0, goal=0, dragon=None):
    out = {}                                          # row 0 is always wall: cells 0-2 carry maze rules
    if rules & ONE_OPEN: out[0] = 13                  # open a random trap group
    if rules & TWO_OPEN: out[1] = 16                  # open two neighbouring trap groups
    if rules & SHUFFLE:  out[2] = 17                  # traps shifted to random groups
    if rules & 0xC000:   out[3] = 26                  # one exit, chosen at random, is real (0x43D8C(0))
    if rules & 0x4000:   out[4] = 27                  # ... and it moves among the exit spots (0x5287C)
    if rules & 0x40:     out[5] = 28                  # the other exits stay, as fakes
    if goal:             out[6] = 0x80 | goal         # the secret-trick goal (header byte 0), for treasure rooms
    if rules & 0x01:     out[7] = 29                  # player shots stun other players (0x4B06A)
    if rules & 0x02:     out[8] = 30                  # player shots hurt other players (0x4B0DA)
    if rules & 0x800000: out[9] = 31                  # all walls invisible (not drawn, 0x5EB2E; mazes 6, 23, 101)
    if rules & 0x80000000: out[10] = 32               # trap walls invisible (mazes 94, 97, 101, 115)
    if rules & 0x04:     out[11] = 33                 # no random wrap-around in this maze (0x437A6)
    if dragon is not None: out[dragon] = 25          # the dragon (bottom-left of its 2x2 block; from level 12)
    for i, c in enumerate(gmap):
        if i >= 32 and c == 0x04: out[i] = 14         # secret wall: looks like a wall, a shot removes it
        if i >= 32 and c == 0x06: out[i] = 15         # random wall: appears and vanishes (0x5E41A)
        if c == 0x11: out[i] = 18                     # exit to 6 (Gauntlet 4 exit 04; the host makes it lead to 6)
        if c == 0x19: out[i] = 20                     # acid puddle (floor; the host moves and draws it)
        if c == 0x1B: out[i] = 21                     # IT (floor; the host moves and draws it)
        if c == 0x2F: out[i] = 22                     # locked chest: a Gauntlet 4 door cell the host opens
        if c == 0x03: out[i] = 23                     # moveable wall (a wall the host lets heroes push)
        if c == 0x1A: out[i] = 24                     # super sorcerer (a Gauntlet 4 sorcerer the host lets fire)
    for i, c in enumerate(gmap):
        if cycling and c in (0x07, 0x08, 0x09): out[i] = 10 + (c - 0x07)   # cycling wall, phase 1-3
        if c == 0x01: out[i] = 1
        if c == 0x3F: out[i] = 2
        if 0x37 <= c <= 0x3B: out[i] = 5 + (c - 0x37)      # amulets: 5 repulsiveness, 6 reflective shots,
                                                       # 7 transportability, 8 super shots, 9 invulnerability
    for i, c in enumerate(gmap):
        if c != 0x3F: continue
        for step, limit in ((1, 15), (32, 16)):
            j, path = i, []
            for _ in range(limit):
                j += step
                if j >= 1024 or (step == 1 and j // 32 != i // 32): break
                if gmap[j] == 0x3F:
                    if path:
                        for k in path:
                            if out.get(k) in (None, 24): out[k] = 3 if step == 1 else 4   # beams before a super-sorcerer mark
                    break
                if gmap[j] in FIXED: break
                path.append(j)
    return sorted(out.items())

# Mirroring (Gauntlet II 0x436FE: every play, random(4) << 26 -> $90491C bit 26 left-right, bit 27
# top-bottom; applied where cells are placed, 0x45E40): left-right x -> (32 - x) & 31, keeping
# column 0 (the border), or x -> 31 - x in a maze that wraps sideways (rule 0x20); top-bottom
# y -> 32 - y (row 0, the border, stays). Each maze is stored in all four versions.
VERSIONS = 4                                          # version index: bit 0 left-right, bit 1 top-bottom
def mirror_cell(i, v, hwrap):
    x, y = i & 31, i >> 5
    if v & 1: x = (31 - x) if hwrap else (32 - x) & 31
    if v & 2 and y: y = 32 - y
    return y * 32 + x
def mirror_map(gmap, v, hwrap):
    out = list(gmap)
    for i in range(32, 1024): out[mirror_cell(i, v, hwrap)] = gmap[i]
    return out
def mirror_dragon(cell, v, hwrap):                    # the 2x2 block cell, cell+1, cell-32, cell-31
    block = [mirror_cell(c, v, hwrap) for c in (cell, cell + 1, cell - 32, cell - 31)]
    xs, ys = sorted({c & 31 for c in block}), sorted({c >> 5 for c in block})
    left = xs[0] if xs != [0, 31] else 31             # a block across the wrap edge starts at 31
    return ys[-1] * 32 + left

records, report, total_used = [], {}, collections.Counter()
extra_lists = []
for m in mazes:
    cyc = bool(rule(m["header"]) & CYCLING)
    hwrap = bool(m["header"][4] & 0x20)
    for v in range(VERSIONS):
        gmap = mirror_map(m["map"], v, hwrap)
        raw, used = convert_map(gmap, cyc)
        rec = header(m["header"]) + encode(raw)
        got = decode(rec)
        want = [1] * 32 + raw[32:]
        assert got == want, f"maze {m['level']} version {v}: decoded map differs at {[i for i in range(1024) if got[i] != want[i]][:8]}"
        records.append(rec)
        dragon = mirror_dragon(DRAGONS[m["level"]], v, hwrap) if m["level"] in DRAGONS else None
        extra_lists.append(extras(gmap, cyc, rule(m["header"]), m["level"], m["header"][0], dragon))
        if v == 0:
            total_used.update(used); report[m["level"]] = dict(record=rec.hex(), stand_ins=dict(used))

blob = bytearray(b"G4L2" + struct.pack(">HH", len(mazes), 2) + bytes(REMAP))     # version 2: 4 versions per maze
offsets_at = len(blob); blob += bytes(4 * len(records))
extras_at = len(blob); blob += bytes(4 * len(records))
for k, rec in enumerate(records):
    struct.pack_into(">I", blob, offsets_at + 4 * k, len(blob))
    blob += rec
    if len(blob) & 1: blob += b"\0"
for k, ex in enumerate(extra_lists):
    struct.pack_into(">I", blob, extras_at + 4 * k, len(blob))
    blob += struct.pack(">H", len(ex)) + b"".join(struct.pack(">HBB", cell, t, 0) for cell, t in ex)
open(out_path, "wb").write(blob)
json.dump(report, open(os.path.join(HERE, "levels", "g2_records.json"), "w"), indent=1)
sizes = [len(r) for r in records]
print(f"{len(mazes)} mazes x {VERSIONS} mirror versions converted, all decode back exactly; records {min(sizes)}-{max(sizes)} bytes, "
      f"file {len(blob)} bytes")
print("stand-ins used:", dict(total_used.most_common()))
ex = collections.Counter(t for lst in extra_lists for _, t in lst)
print("host extras: %d stun tiles, %d force-field posts, %d beam cells (mazes with beams: %d), %d amulets" % (ex[1], ex[2], ex[3] + ex[4], sum(1 for l in extra_lists if any(t in (3, 4) for _, t in l)), sum(ex[t] for t in range(5, 10))))
