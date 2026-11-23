"""a8maze.py -- our own reading of the Atari 8-bit Gauntlet maze records (side 2 / Deeper Dungeons),
written from the game's unpacker ($56DE lines, $58F9 objects). Returns per maze: header bytes, a 32x32
grid of kinds (0 floor, 'W' wall, 'V' second wall kind, or the object code), placed monsters
[(cell, code)] and the player start cell. Usage: python3 -I a8maze.py disk.atr [ref_mazes.bin]"""
import sys
DIRS = [-32, -31, 1, 33, 32, 31, -1, -33]
TYPE_VAL = [0x00, 0x36, 0x11, 0x00, 0x12, 0x00, 0x90, 0x10]

def sectors(atr, first, n):
    out = b''
    for s in range(first, first + n):
        off = 16 + 128 * (s - 1)
        out += atr[off:off + 128] if off + 128 <= len(atr) else bytes(128)
    return out

def is_wall(v): return 0 < (v & 0x7F) <= 0x10 or v == 0x90

def unpack(rec):
    g = [0] * 1024
    for i in range(32): g[i] = 0x0A           # row 0: wall (kind only matters)
    if not rec[1] & 0x80:
        for y in range(1, 32): g[y * 32] = 0x05
    nline = rec[3]
    e = rec[4:]
    cnt = nline; y = 0; typ = 0; ptr = 0
    def hi(k): return e[k] & 0xE0
    def plot(p, t):
        g[p] = TYPE_VAL[t >> 5]
    while cnt:
        point = False
        if cnt != 1 and hi(y) == hi(y + 1):
            if cnt == 3: point = hi(y + 1) != hi(y + 2)
            else: point = hi(y + 1) != hi(y + 2) or hi(y + 2) == hi(y + 3)
        if point:                                  # $579A: a start point (type|x, ?|y)
            typ = e[y] & 0xE0
            ptr = (e[y] & 0x1F) + 32 * (e[y + 1] & 0x1F)
            plot(ptr, typ); y += 2; cnt -= 2
        else:                                      # $57CB: a run (dir|len)
            d = e[y] >> 5; n = e[y] & 0x1F
            if typ in (0x80, 0x40): typ = [0x40, 0x80, 0x40, 0x80][d >> 1]
            for _ in range(n + 1):
                p = (ptr + DIRS[d]) & 0x3FF
                if d == 2 and p % 32 == 0: p = (p - 32) & 0x3FF
                if d == 6 and p % 32 == 31: p = (p + 32) & 0x3FF
                ptr = p; plot(ptr, typ)
            y += 1; cnt -= 1
    # objects ($58F9)
    total = rec[0] | (rec[2] & 0x80) << 1
    left = total - nline - 4
    o = rec[4 + nline:]
    k = 0; cell = 0; mons = []; start = None
    while left > 0 and cell < 1024:
        b = o[k]
        if b & 0x80:
            cell += (b & 0x7F) + 1
        else:
            rep = 0
            if left != 1 and o[k + 1] < 0x13:
                rep = o[k + 1] + 1; k += 1; left -= 1
            for _ in range(rep + 1):
                if cell >= 1024: break
                v = b
                if b >= 0x40: mons.append((cell, b)); v = 0
                elif b == 0x3F: start = cell; v = 0
                g[cell] = v; cell += 1
        k += 1; left -= 1
    return g, mons, start

def kind(v):
    if v == 0: return 0
    if 0 < v <= 0x10: return 'W'
    if v & 0x80 and (v & 0x7F) <= 0x10: return 'V'
    return v

if __name__ == '__main__':
    atr = open(sys.argv[1], 'rb').read()
    ref = open(sys.argv[2], 'rb').read() if len(sys.argv) > 2 else None
    bad = 0
    for n in range(1, 129):
        rec = sectors(atr, 4 * n, 4)
        g, mons, start = unpack(rec)
        if ref:
            r = ref[(n - 1) * 1032 + 8:n * 1032]
            diff = [i for i in range(1024) if kind(g[i]) != kind(r[i])]
            if diff: bad += 1; print('maze', n, 'differs at', len(diff), 'cells, first', diff[:6], [(g[i], r[i]) for i in diff[:6]])
    print('mazes that differ:', bad)
