#!/usr/bin/env python3
"""
import_gauntlet2.py -- read the mazes of arcade Gauntlet II (Atari Games 1986, MAME set
"gaunt2") by running the game's own maze decoder in Unicorn.

Findings this rests on:
  * Program layout as MAME (ROM_CONTINUE: each 32 KB chip pair's first half loads above its
    second): 1307/1308 -> 0x8000 / 0x0, 1105/1106 -> 0x38000 (slapstic 106: 8 KB window at
    0x38000-0x39FFF, 4 banks), 1109/1110 -> 0x48000 / 0x40000, 1121/1122 -> 0x58000 / 0x50000.
  * The window's first long (bank 0) points at the maze table (0x3800C): 117 window addresses.
  * Record: an 11-byte header (bytes 7-10 -> $904866/68/6A/6C; byte 8 also seeds the first
    cell) and a command stream from byte 11, decoded by 0x4C1BC(record), which calls
    0x45E40(index, cell, count) for every run of cells (index from 0x20; it returns
    index + count) until index 0x400.
For finding banks 0x45E40 is intercepted (recorded and returned from) so the decoder runs alone.
The maze itself comes from the game's full setup 0x44AC2(record): it keeps the cell map in the top
6 bits of the motion-object link words, $903800 + 2 * (y * 32 + x) (row-major; walls included;
the mirror flags in $90491C left clear).
A record's bank: each bank's records lie back to back, so each bank's chain is followed from
its first record by decoding (the next record begins where one ends, up to 2 bytes of padding).

Usage: python3 -I import_gauntlet2.py <gauntlet2.zip> <out.json>
"""
import json, struct, sys, zipfile
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_MEM_UNMAPPED, UC_HOOK_CODE, UcError
from unicorn.m68k_const import UC_M68K_REG_A7, UC_M68K_REG_SR, UC_M68K_REG_PC, UC_M68K_REG_D0, UC_M68K_REG_A2, UC_CPU_M68K_M68020

zip_path, out_path = sys.argv[1:3]
z = zipfile.ZipFile(zip_path); rd = z.read
def il(a, b): return bytes(x for p in zip(a, b) for x in p)
prog = bytearray(0x80000)
def cont(e, o, hi, lo):
    a, b = rd(e), rd(o); prog[hi:hi + 0x8000] = il(a[:0x4000], b[:0x4000]); prog[lo:lo + 0x8000] = il(a[0x4000:], b[0x4000:])
cont("136037-1307.9a", "136037-1308.9b", 0x8000, 0x0)
prog[0x38000:0x40000] = il(rd("136043-1105.10a"), rd("136043-1106.10b"))
cont("136043-1109.7a", "136043-1110.7b", 0x48000, 0x40000)
cont("136043-1121.6a", "136043-1122.6b", 0x58000, 0x50000)
BANKS = bytes(prog[0x38000:0x40000])
WINDOW, DECODE, PUT, STOP = 0x38000, 0x4C1BC, 0x45E40, 0x7FF00

def bank_long(k, a): o = k * 0x2000 + a - WINDOW; return struct.unpack(">I", BANKS[o:o + 4])[0]
table = bank_long(0, WINDOW)
windows = []
while 0x38000 <= bank_long(0, table + 4 * len(windows)) < 0x3A000: windows.append(bank_long(0, table + 4 * len(windows)))

def decode(window, bank):
    img = bytearray(prog); img[0x38000:0x3A000] = BANKS[bank * 0x2000:(bank + 1) * 0x2000]
    uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN); uc.ctl_set_cpu_model(UC_CPU_M68K_M68020)
    uc.mem_map(0, 0x80000); uc.mem_write(0, bytes(img)); uc.mem_write(STOP, b"\x4e\x72\x27\x00")
    uc.mem_map(0x800000, 0x10000); uc.mem_map(0x900000, 0x40000)
    def unmapped(uc, access, addr, size, value, ud):
        try: uc.mem_map((addr & 0xFFFFFF) & ~0xFFF, 0x1000)
        except UcError: return False
        return True
    uc.hook_add(UC_HOOK_MEM_UNMAPPED, unmapped)
    grid = [0] * 1024; calls = [0]; end = [None]
    def put(uc, addr, size, ud):                        # 0x45E40(index, cell, count): record, return
        sp = uc.reg_read(UC_M68K_REG_A7)
        ret, idx, cell, cnt = struct.unpack(">IIII", uc.mem_read(sp, 16))
        idx, cell, cnt = idx & 0xFFFF, cell & 0xFFFF, cnt & 0xFFFF
        for i in range(idx, min(1024, idx + cnt)): grid[i] = cell
        calls[0] += 1
        uc.reg_write(UC_M68K_REG_D0, (idx + cnt) & 0xFFFF)
        uc.reg_write(UC_M68K_REG_A7, sp + 4); uc.reg_write(UC_M68K_REG_PC, ret)
    def at_end(uc, addr, size, ud): end[0] = uc.reg_read(UC_M68K_REG_A2)
    uc.hook_add(UC_HOOK_CODE, put, begin=PUT, end=PUT)
    uc.hook_add(UC_HOOK_CODE, at_end, begin=0x4C436, end=0x4C436)
    sp = 0x904E00
    uc.mem_write(sp, struct.pack(">II", STOP, window))
    uc.reg_write(UC_M68K_REG_SR, 0x2700); uc.reg_write(UC_M68K_REG_A7, sp)
    try: uc.emu_start(DECODE, STOP, count=2_000_000)
    except UcError: return None
    hdr = bytes(uc.mem_read(window, 11))
    return dict(grid=grid, calls=calls[0], length=(end[0] - window) if end[0] else None, header=list(hdr))

# each bank's records lie back to back: follow the chain from the bank's first record (bank 0:
# after the table; banks 1-3: the window's start), decoding each to find the next
chain = {}
wset = set(windows)
for k in range(4):
    w = table + 4 * len(windows) if k == 0 else WINDOW          # bank 0: right after the table
    chain[k] = {}
    while w in wset and w not in chain[k]:
        r = decode(w, k)
        if not r or r["length"] is None: break
        chain[k][w] = r
        e = w + r["length"]
        nxt = [x for x in sorted(wset) if e <= x <= e + 2]
        if not nxt: break
        w = nxt[0]
SETUP = 0x44AC2
def setup_map(window, bank):
    """The game's maze setup 0x44AC2(record), unhooked: the cell map it leaves in the top 6 bits
    of the motion-object link words ($903800 + 2 * (y * 32 + x)), walls included."""
    img = bytearray(prog); img[0x38000:0x3A000] = BANKS[bank * 0x2000:(bank + 1) * 0x2000]
    uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN); uc.ctl_set_cpu_model(UC_CPU_M68K_M68020)
    uc.mem_map(0, 0x80000); uc.mem_write(0, bytes(img)); uc.mem_write(STOP, b"\x4e\x72\x27\x00")
    uc.mem_map(0x800000, 0x10000); uc.mem_map(0x900000, 0x40000)
    def unmapped(uc, access, addr, size, value, ud):
        try: uc.mem_map((addr & 0xFFFFFF) & ~0xFFF, 0x1000)
        except UcError: return False
        return True
    uc.hook_add(UC_HOOK_MEM_UNMAPPED, unmapped)
    sp = 0x904E00
    uc.mem_write(sp, struct.pack(">II", STOP, window))
    uc.reg_write(UC_M68K_REG_SR, 0x2700); uc.reg_write(UC_M68K_REG_A7, sp)
    uc.emu_start(SETUP, STOP, count=20_000_000)
    return [w >> 10 for w in struct.unpack(">1024H", uc.mem_read(0x903800, 0x800))]

def unique_bank(j):
    b = [k for k in range(4) if windows[j] in chain[k]]
    return b[0] if len(b) == 1 else None
mazes = []
for i, w in enumerate(windows):
    banks = [k for k in range(4) if w in chain[k]]
    if len(banks) > 1:                                  # same address in several chains: table order --
        prev = mazes[-1]["bank"] if mazes else 0        # the table lists a bank's records as a run,
        nxt = unique_bank(i + 1) if i + 1 < len(windows) else None    # so a neighbour's bank
        banks = [prev] if prev in banks else [nxt] if nxt in banks else banks
    k = banks[0] if len(banks) == 1 else None
    if len(banks) != 1: print(f"maze {i + 1} ({w:x}): banks {banks}")
    mazes.append(dict(index=i, level=i + 1, window=w, bank=k, **(chain[k][w] if k is not None else {})))
print("chain lengths", {k: len(chain[k]) for k in chain})
for m in mazes:                                         # the maze as the game builds it (walls included)
    m.pop("grid", None)
    if m["bank"] is not None: m["map"] = setup_map(m["window"], m["bank"])
json.dump(dict(source="arcade Gauntlet II (MAME 'gaunt2'): bank by decoding (0x4C1BC); 'map' = the cell map "
                      "the game's setup 0x44AC2 leaves in $903800 (word >> 10), row-major y*32+x, no flip",
               mazes=mazes), open(out_path, "w"))
print(len(mazes), "mazes; banks", [sum(m["bank"] == k for m in mazes) for k in range(4)],
      "; undecided", sum(m["bank"] is None for m in mazes))
