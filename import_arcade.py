#!/usr/bin/env python3
"""
import_arcade.py -- read the mazes of arcade Gauntlet (Atari Games 1985, MAME set
"gauntlet") and decode them with Gauntlet 4's own map decoder.

Findings this rests on:
  * Gauntlet 4 kept the arcade's maze records unchanged in format: a 14-byte header
    (bytes 0-2 the designer's initials, e.g. "edz", "cle"; 7 option flags; 8-9 nibbles;
    10-13 the four colour-scheme numbers) and the compressed 32x32 map stream after it.
    Most Gauntlet 4 Arcade records are byte-identical to the arcade's.
  * In the arcade the records are in ROMs 136037-205/206 (32 KB, even/odd bytes), seen by
    the 68010 through an 8 KB window at 0x38000-0x39FFF whose bank (4 of them) the slapstic
    chip selects. The maze table at 0x38032 (bank 0) holds 114 window addresses; a record is
    in the bank where its address shows three lowercase letters.

Usage: python3 -I import_arcade.py <gauntlet.zip> <gauntlet4.bin> <libg4.so> <out.json>
Writes out.json: per maze {index, level, bank, window, header, palette, raw (32x32 before
the remap), g4_same (Gauntlet 4's level of the same number has the identical record)}.
"""
import ctypes, json, os, struct, sys, zipfile

zip_path, g4_path, lib_path, out_path = sys.argv[1:5]
with zipfile.ZipFile(zip_path) as z:
    lo, hi = z.read("136037-205.10a"), z.read("136037-206.10b")
ROM205 = bytes(b for pair in zip(lo, hi) for b in pair)            # 32 KB: 4 banks of 8 KB
BANKS = [ROM205[k * 0x2000:(k + 1) * 0x2000] for k in range(4)]
TABLE, COUNT, WINDOW = 0x38032, 114, 0x38000

def win_read(bank, addr, n): o = addr - WINDOW; return BANKS[bank][o:o + n]
def L(bank, addr): return struct.unpack(">I", win_read(bank, addr, 4))[0]

G4 = open(g4_path, "rb").read()
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
MEM = (ctypes.c_uint8 * 0x1000000)()
ctypes.memmove(MEM, G4, len(G4))
ctypes.c_void_p.in_dll(lib, "g4_mem").value = ctypes.addressof(MEM)
ctypes.c_int.in_dll(lib, "g4_use_translated").value = 1
def no_ext(cp, t): raise SystemExit(f"unexpected call to untranslated code {t:#x}")
CBS = (EXT_CB(no_ext), RD_CB(lambda a, s: 0), WR_CB(lambda a, v, s: None))
lib.g4_test_set(*CBS)
def wr(addr, data): ctypes.memmove(ctypes.addressof(MEM) + addr, bytes(data), len(data))
def rd(addr, n): return ctypes.string_at(ctypes.addressof(MEM) + addr, n)

MAP, SP, ARCADE_REMAP, SCRATCH = 0xFFE246, 0xFFFD00, 0x31D2E, 0x0F0000
def decode_raw(record_bytes):
    """Gauntlet 4's map_decode (0x19122) on a record placed at SCRATCH, identity remap."""
    wr(SCRATCH, record_bytes + bytes(64))
    saved = rd(ARCADE_REMAP, 64)
    wr(ARCADE_REMAP, range(64)); wr(0xFFE838, b"\0\0")
    wr(SP - 4, struct.pack(">I", SCRATCH))
    cpu = M68K(); cpu.a[4] = MAP; cpu.a[7] = SP - 4
    assert lib.g4_test_run(ctypes.byref(cpu), 0x019122) == 0
    grid = rd(MAP, 0x400)
    wr(ARCADE_REMAP, saved)
    return list(grid)

def g4_record(level):
    """Gauntlet 4's Arcade record for a level (its lookup 0x40D8, $ffe790 set)."""
    wr(0xFFE758, struct.pack(">H", level)); wr(0xFFE790, b"\xff"); wr(0xFFE799, b"\0")
    cpu = M68K(); cpu.a[7] = SP
    assert lib.g4_test_run(ctypes.byref(cpu), 0x0040D8) == 0
    return struct.unpack(">I", rd(0xFFE75C, 4))[0]

def initials(b): return all(0x61 <= c <= 0x7A for c in b[:3])

windows = [L(0, TABLE + 4 * i) for i in range(COUNT)]
cands = [[k for k in range(4) if initials(win_read(k, w, 3))] for w in windows]
# a record at the very start of the window (0x38000) has initials in banks 1-3 (mazes 30, 51,
# 72; the table does not show which bank): the bank whose record is Gauntlet 4's for that
# level (same header, all 1024 cells; the others differ in ~600 cells), each bank once
chosen = [c[0] if len(c) == 1 else None for c in cands]
for i in range(COUNT):
    if chosen[i] is None:
        g4rec = g4_record(i + 1); g4raw = decode_raw(G4[g4rec:g4rec + 2048])
        match = [k for k in cands[i] if win_read(k, windows[i], 14) == G4[g4rec:g4rec + 14]
                 and decode_raw(win_read(k, windows[i], 0x2000)) == g4raw]
        chosen[i] = match[0] if len(match) == 1 else None
        print(f"maze {i + 1}: window {windows[i]:x} in banks {cands[i]}: bank {chosen[i]} (Gauntlet 4's)")
starts = [chosen[i] for i in range(COUNT) if windows[i] == WINDOW]
assert len(starts) == len(set(starts)) and None not in starts, starts

mazes = []
for i in range(COUNT):
    w, bank = windows[i], chosen[i]
    rec = win_read(bank, w, 0x2000 - (w - WINDOW)) if bank is not None else b""
    raw = decode_raw(rec) if rec else None
    g4rec = g4_record(i + 1)
    g4raw = decode_raw(G4[g4rec:g4rec + 2048])
    mazes.append(dict(index=i, level=i + 1, bank=bank, window=w, header=list(rec[:8]),
                      initials=rec[:3].decode() if rec else "", palette=list(rec[10:14]), raw=raw,
                      g4_record=g4rec, g4_header_same=G4[g4rec:g4rec + 14] == rec[:14],
                      g4_map_same=raw == g4raw))

json.dump(dict(source="arcade Gauntlet (MAME 'gauntlet'), ROMs 136037-205/206", mazes=mazes), open(out_path, "w"))
same_map = sum(m["g4_map_same"] for m in mazes); same_hdr = sum(m["g4_header_same"] for m in mazes)
print(f"{len(mazes)} mazes; banks {[sum(m['bank'] == k for m in mazes) for k in range(4)]}; "
      f"same map as Gauntlet 4: {same_map}, same header: {same_hdr}")
print("different maps:", " ".join(str(m["level"]) for m in mazes if not m["g4_map_same"]))
print("initials:", sorted(set(m["initials"] for m in mazes)))
