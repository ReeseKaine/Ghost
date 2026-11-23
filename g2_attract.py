#!/usr/bin/env python3
"""
g2_attract.py -- the arcade Gauntlet II attract screens, made from your own gauntlet2.zip by running
the arcade's own attract code in Unicorn: title (colour-cycling logo), LEGEND (items), MONSTERS,
credits, SCORE PER COIN. Nothing from the ROM is stored in this script.

  capture:  python3 -I g2_attract.py render_gauntlet2.py <gauntlet2.zip> <arcade_g2.json> capture <g2_attract.bin>
            -> the whole sequence (title -> demo game -> legend pages -> score page)
               as a G2AT file (see write_g2at) for the port's attract loop. make_data.sh step 9.
  pictures: python3 -I g2_attract.py render_gauntlet2.py <zip> <json> <page> <out.png> [frames] [every]
            page: s-2 title, s-4 legend (from the items page), s-1 score page, 0/1/2 a legend page
            set up directly; with "every" a contact sheet.

How it runs (findings in CLAUDE.md): render_gauntlet2's set-up (maze 104), slapstic bank routines
hooked, the high-score block given the ROM's defaults (0x49BD0), then per frame the vblank
interrupt (0x4017E) and one pass of the main loop (0x42A7E..0x42B66) with the joysticks released
and the self-test switch off. Screen = playfield + motion objects (SLIP chains) + alpha layer.
"""
import sys, struct
src = open(sys.argv[1]).read(); cut = src.index("W = 512")
page, out = sys.argv[4], sys.argv[5]
from unicorn import UcError
from unicorn.m68k_const import UC_M68K_REG_A2, UC_M68K_REG_A3, UC_M68K_REG_A6, UC_M68K_REG_A7, UC_M68K_REG_SR, UC_M68K_REG_PC
every_arg = sys.argv[7] if len(sys.argv) > 7 else "0"
frames_arg = sys.argv[6] if len(sys.argv) > 6 else "0"          # 0/1/2 = legend page, sN = attract state N (e.g. s-1)
sys.argv = [sys.argv[0], sys.argv[2], sys.argv[3], "104", "/dev/null", frames_arg]
g = {"__name__": "attr"}; exec(compile(src[:cut], "render_gauntlet2.py", "exec"), g)
uc, run, rd, tile = g["uc"], g["run"], g["rd"], g["tile"]
from PIL import Image
# slapstic bank switching (window 0x38000-0x39FFF, 4 banks of 0x2000 in prog[0x38000:0x40000]):
# the program writes $38000 then $38010 + 4*bank, always from five small routines -> do it there
from unicorn import UC_HOOK_CODE as _HC
from unicorn.m68k_const import UC_M68K_REG_A0, UC_M68K_REG_D0
def set_bank(k):
    uc.mem_write(0x38000, bytes(g["prog"][0x38000 + k * 0x2000:0x38000 + (k + 1) * 0x2000]))
def bank_hook(uc_, addr, size, ud):
    if addr == 0x56E58: off = 0x10
    elif addr == 0x56E6E: off = 0x1C
    else:
        d0 = uc.reg_read(UC_M68K_REG_D0) & 0xFFFF; d0 -= (d0 & 0x8000) << 1
        off = (uc.reg_read(UC_M68K_REG_A0) + d0 - 0x38000) & 0x1FFF
    if 0x10 <= off < 0x20: set_bank((off - 0x10) >> 2)
    else: print("bank routine %x: offset %x (not a bank select)" % (addr, off))
    sp = uc.reg_read(UC_M68K_REG_A7)                    # return from the routine
    uc.reg_write(UC_M68K_REG_PC, struct.unpack(">I", uc.mem_read(sp, 4))[0]); uc.reg_write(UC_M68K_REG_A7, sp + 4)
for a in (0x56E58, 0x56E6E, 0x56E84, 0x56E90, 0x56E98):
    uc.hook_add(_HC, bank_hook, begin=a, end=a)
def vbl_wait(uc_, addr, size, ud):                     # the program's only two "wait for the interrupt" loops
    uc.mem_write(0x904002, b"\0\1")                      # (tst.w $904002 at 0x430E0 / 0x434B6, e.g. the palette
for a in (0x430E0, 0x434B6):                           #  routine 0x43486): let them through at once
    uc.hook_add(_HC, vbl_wait, begin=a, end=a)
# the legend routine's work after the maze: clear the alpha layer, palette, the page
for r in range(30):
    uc.mem_write(0x905000 + r * 128, struct.pack(">29H", *([0x8000] * 29)))
if page == "capture": page_c = True
else: page_c = False
if page.startswith("s") or page_c:
    # saved settings/scores: at power-on (0x4512) the arcade unpacks its EEPROM into a block it keeps
    # a pointer to at $904FFC (scores at +0x1E: class*0x32 + entry*5). Give it an empty block;
    # the power-on high-score check 0x49BD0 then fills in the ROM's default table (0x57EBA).
    uc.mem_write(0x918000, bytes(0x400)); uc.mem_write(0x904FFC, struct.pack(">I", 0x918000))
    run(0x49BD0, [])
    if page == "s-4":                                  # the legend as the arcade starts it (0x447AC): pages 2, 1, 0
        uc.mem_write(0x90491A, struct.pack(">H", 2))
        run(0x4529A, []); run(0x452D0, [0xFFFFFFFF])
    run(0x44414, [0xFFFFFFFE if page_c else int(page[1:]) & 0xFFFFFFFF])
else:
    page = int(page)
    if page == 1: uc.mem_write(0x904B58, struct.pack(">H", 7))
    pa, pb = struct.unpack(">HH", uc.mem_read(0x904B58, 4))
    run(0x43486, [pa, pb])
    run({0: 0x4CFAE, 2: 0x4CFDA}.get(page, 0x4CDB8), [])
frames = int(frames_arg)
def vblank():                                          # the level-4 interrupt (0x34A -> 0x40006 -> 0x4017E):
    sp = 0x904E00 - 0x80                               # scroll registers, vblank flag $904002, colour effects
    uc.mem_write(0x803000, b"\xff" * 10)                   # joysticks released; 0x803008 bit 3 set = self-test
                                                           # switch off (else the handler resets the machine)
    uc.mem_write(sp, struct.pack(">HIH", 0x2000, g["STOP"], 0x0070))   # 68020 frame: SR, PC, format/vector
    uc.reg_write(UC_M68K_REG_SR, 0x2400); uc.reg_write(UC_M68K_REG_A7, sp)
    try: uc.emu_start(0x4017E, 0x4049E, count=2_000_000) # stop at its RTE (the frame is just dropped)
    except UcError as e: print("vblank stopped:", e, "pc %x" % uc.reg_read(UC_M68K_REG_PC))
def frame():                                           # one pass of the main loop 0x42A7E..0x42B66
    vblank()
    uc.mem_write(0x904002, b"\0\1")                      # the vblank flag the loop waits for
    uc.mem_write(0x803000, b"\xff" * 10)                  # the 4 players' joystick ports (0x40644 copies them to $904920..): nothing pressed
    sp = 0x904E00 - 0x40
    uc.reg_write(UC_M68K_REG_A2, 0x904002); uc.reg_write(UC_M68K_REG_A6, sp + 0x20)
    uc.reg_write(UC_M68K_REG_A3, sp + 0x1E)
    uc.reg_write(UC_M68K_REG_SR, 0x2700); uc.reg_write(UC_M68K_REG_A7, sp)
    try: uc.emu_start(0x42A7E, 0x42B66, count=5_000_000)
    except UcError as e: print("frame stopped:", e, "pc %x" % uc.reg_read(UC_M68K_REG_PC)); return False
    if uc.reg_read(UC_M68K_REG_PC) != 0x42B66:
        print("frame did not finish: pc %x" % uc.reg_read(UC_M68K_REG_PC)); return False
    return True
ach = rd("136043-1104.6p")
_ach = {}
def achar(code):     # 8x8, 2 bpp: 16 bytes per character, planes in nibbles (MAME gauntlet layout)
    if code not in _ach:
        _ach[code] = [[((ach[code * 16 + y * 2 + (x >> 2)] >> (3 - (x & 3) + 4)) & 1) << 1 | ((ach[code * 16 + y * 2 + (x >> 2)] >> (3 - (x & 3))) & 1)
                       for x in range(8)] for y in range(8)]
    return _ach[code]
W, H = 336, 240
def snap():
    pf = struct.unpack(">4096H", uc.mem_read(0x900000, 0x2000))
    pal = [g["irgb"](w) for w in struct.unpack(">1024H", uc.mem_read(0x910000, 0x800))]
    bank = struct.unpack(">H", uc.mem_read(0x905F6E, 2))[0] & 3
    mob = [struct.unpack(">1024H", uc.mem_read(0x902000 + 0x800 * k, 0x800)) for k in range(4)]
    alpha = struct.unpack(">2048H", uc.mem_read(0x905000, 0x1000))
    xs, ys = struct.unpack(">HH", uc.mem_read(0x904008, 4))     # playfield scroll
    pic = Image.new("RGB", (W, H)); px = pic.load()
    for sy in range(H):                                    # playfield, scrolled
        for sx in range(W):
            X = (sx + xs) & 511; Y = (sy + ys) & 511
            w = pf[(X >> 3) * 64 + (Y >> 3)]
            t = tile((bank * 0x1000 + (w & 0xFFF)) ^ 0x800); c = 0x28 + ((w >> 12) & 7); flip = w >> 15
            px[sx, sy] = pal[c * 16 + t[Y & 7][7 - (X & 7) if flip else (X & 7)]]
    slip = struct.unpack(">64H", uc.mem_read(0x905F80, 128))  # motion objects: per 8-line band (scrolled
    for band in range(64):                                 # space) a start link in SLIP RAM; walk the chain
        top = ((band * 8) - ys) & 511                      # and draw clipped to the band (as MAME atarimo)
        if top >= H and top + 8 <= 512: continue
        chain, i = [], slip[band] & 0x3FF
        while i not in chain and len(chain) < 1024:
            chain.append(i); i = mob[3][i] & 0x3FF
        for i in reversed(chain):                          # earlier in the list = in front (drawn last)
            code = mob[0][i] & 0x7FFF; w1, w2 = mob[1][i], mob[2][i]
            color = w1 & 15; wt = ((w2 >> 3) & 7) + 1; ht = (w2 & 7) + 1; flip = (w2 >> 6) & 1
            x0 = (w1 >> 7) - xs; y0 = 511 - (w2 >> 7) - ht * 8 - ys   # Y counts from the bottom (511: logo strips then fit the 8-line bands exactly)
            for tx in range(wt):
                for ty in range(ht):
                    t = tile((code + ty * wt + tx) ^ 0x800)  # row by row; ^0x800 as the playfield (the ROM halves are swapped)
                    for yy in range(8):
                        Y = (y0 + ty * 8 + yy) & 511
                        if ((Y - top) & 511) >= 8 or Y >= H: continue
                        for xx in range(8):
                            v = t[yy][7 - xx if flip else xx]
                            if v:
                                X = (x0 + (wt - 1 - tx if flip else tx) * 8 + xx) & 511
                                if X < W: px[X, Y] = pal[0x100 + color * 16 + v]
    for r in range(30):                                    # alpha layer on top
        for col in range(42):
            w = alpha[r * 64 + col]; code = w & 0x3FF; color = ((w >> 10) & 0x0F) | ((w >> 9) & 0x20); opaque = w & 0x8000
            if not code and not opaque: continue
            ch = achar(code)
            for yy in range(8):
                for xx in range(8):
                    v = ch[yy][xx]
                    if v or opaque:
                        X, Y = col * 8 + xx, r * 8 + yy
                        if X < W and Y < H: px[X, Y] = pal[color * 4 + v]
    return pic
# ---- fast renderer (numpy): a frame as 10-bit palette indices + the palette ----
import numpy as np
_T = None
def tables():
    global _T, _A
    if _T is None:
        _T = np.array([tile(c) for c in range(0x3000)], dtype=np.uint16)          # (0x3000, 8, 8)
        _A = np.array([achar(c) for c in range(1024)], dtype=np.uint16)           # (1024, 8, 8)
    return _T, _A
# The display state of one frame: the memory the screen is drawn from, in this order (file v2 stores
# these bytes per frame as changes against the frame before).
REGIONS = [(0x900000, 0x2000),   # playfield: 64 x 64 words, [column][row]
           (0x902000, 0x2000),   # motion objects: 4 tables of 1024 words (picture, x/colour, y/size, link)
           (0x905000, 0x0F00),   # alpha layer: 30 rows of 64 words (42 shown)
           (0x905F80, 0x0080),   # SLIP: start link per 8-line band
           (0x910000, 0x0800),   # palette: 1024 words
           (0x905F6E, 0x0002),   # playfield tile bank
           (0x904008, 0x0004)]   # playfield scroll x, y
STATE_SIZE = sum(n for _, n in REGIONS)
def grab_state():
    return b"".join(bytes(uc.mem_read(a, n)) for a, n in REGIONS)
def split_state(st):
    out, o = [], 0
    for _, n in REGIONS: out.append(st[o:o + n]); o += n
    return out
def snap_idx(state=None):
    T, A = tables()
    pf_b, mo_b, al_b, slip_b, pal_b, bank_b, sc_b = split_state(state if state is not None else grab_state())
    pfw = np.frombuffer(pf_b, dtype=">u2").astype(np.int32).reshape(64, 64)     # [col, row]
    bank = struct.unpack(">H", bank_b)[0] & 3
    xs, ys = struct.unpack(">HH", sc_b)
    codes = ((bank * 0x1000 + (pfw & 0xFFF)) ^ 0x800) % 0x3000
    tl = T[codes].copy()                                                          # (col, row, y, x)
    fl = (pfw >> 15) == 1
    tl[fl] = tl[fl][:, :, ::-1]
    tl += ((0x28 + ((pfw >> 12) & 7)) * 16).astype(np.uint16)[:, :, None, None]
    full = tl.transpose(1, 2, 0, 3).reshape(512, 512)                             # [row*8+y, col*8+x]
    img = full[np.ix_((np.arange(H) + ys) & 511, (np.arange(W) + xs) & 511)].copy()
    mob = [struct.unpack(">1024H", mo_b[0x800 * k:0x800 * (k + 1)]) for k in range(4)]
    slip = struct.unpack(">64H", slip_b)
    for band in range(64):
        top = ((band * 8) - ys) & 511
        if top >= H and top + 8 <= 512: continue
        chain, i = [], slip[band] & 0x3FF
        while i not in chain and len(chain) < 1024:
            chain.append(i); i = mob[3][i] & 0x3FF
        for i in reversed(chain):
            code = mob[0][i] & 0x7FFF; w1, w2 = mob[1][i], mob[2][i]
            color = w1 & 15; wt = ((w2 >> 3) & 7) + 1; ht = (w2 & 7) + 1; flip = (w2 >> 6) & 1
            x0 = (w1 >> 7) - xs; y0 = 511 - (w2 >> 7) - ht * 8 - ys
            spr = np.zeros((ht * 8, wt * 8), np.uint16)
            for ty in range(ht):
                for tx in range(wt):
                    spr[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8] = T[((code + ty * wt + tx) ^ 0x800) % 0x3000]
            if flip: spr = spr[:, ::-1]
            Y = (y0 + np.arange(ht * 8)) & 511; X = (x0 + np.arange(wt * 8)) & 511
            ry = (((Y - top) & 511) < 8) & (Y < H); rx = X < W
            if not ry.any() or not rx.any(): continue
            sub = spr[np.ix_(ry, rx)]; yy, xx = Y[ry], X[rx]
            dst = img[np.ix_(yy, xx)]
            img[np.ix_(yy, xx)] = np.where(sub != 0, 0x100 + color * 16 + sub, dst)
    al = np.frombuffer(al_b, dtype=">u2").astype(np.int32).reshape(30, 64)[:, :42]
    ch = A[al & 0x3FF]                                                             # (30, 42, 8, 8)
    colr = ((al >> 10) & 0x0F) | ((al >> 9) & 0x20)
    pix = (colr * 4)[:, :, None, None] + ch
    show = (ch != 0) | ((al & 0x8000) != 0)[:, :, None, None]
    pix = pix.transpose(0, 2, 1, 3).reshape(240, 336); show = show.transpose(0, 2, 1, 3).reshape(240, 336)
    img = np.where(show, pix, img).astype(np.uint16)
    pal = np.array([g["irgb"](w) for w in struct.unpack(">1024H", pal_b)], np.uint8)
    return img, pal
def pack4(rows):                                       # 8x8 pixels (values 0-15) -> 32 bytes, two per byte
    flat = [v for r in rows for v in r]
    return bytes((flat[k] << 4) | flat[k + 1] for k in range(0, 64, 2))
def write_g2at(path, states, marks):
    """G2AT version 2 (all little-endian unless said otherwise):
         "G2AT", u16 version 2, u16 width 336, u16 height 240, u16 frames, u16 section count,
         per section (u16 first frame, i16 arcade attract state: -2 title, -3 demo game,
         -4 legend pages, -1 score per coin),
         u16 tile count (0x3000), u16 alpha character count (1024), then each tile and each
         character as 32 bytes (8 rows of 8 pixels, two pixels per byte, high nibble first),
         then the frames. A frame is the arcade's display memory (REGIONS above, STATE_SIZE bytes,
         big-endian words as in the arcade) given as changes against the frame before (all zero
         before frame 0): u32 byte length of the rest, u16 runs, per run (u16 offset, u16 length,
         that many bytes). The game draws each frame from it as snap_idx() does."""
    T, A = tables()
    out = bytearray(b"G2AT" + struct.pack("<5H", 2, W, H, len(states), len(marks)))
    for f0, st in marks: out += struct.pack("<Hh", f0, st)
    out += struct.pack("<2H", 0x3000, 1024)
    for c in range(0x3000): out += pack4(T[c].tolist())
    for c in range(1024): out += pack4(A[c].tolist())
    prev = np.zeros(STATE_SIZE, np.uint8)
    for st in states:
        cur = np.frombuffer(st, np.uint8)
        idx = np.flatnonzero(cur != prev)
        rec = bytearray()
        runs = []
        if idx.size:
            br = np.flatnonzero(np.diff(idx) > 4) + 1         # join runs with gaps of <= 4 unchanged bytes
            runs = [(int(idx[a]), int(idx[b - 1]) - int(idx[a]) + 1) for a, b in zip(np.r_[0, br], np.r_[br, idx.size])]
        rec += struct.pack("<H", len(runs))
        for o, n in runs: rec += struct.pack("<2H", o, n) + st[o:o + n]
        out += struct.pack("<I", len(rec)) + rec
        prev = cur
    open(path, "wb").write(out)
    print("%s: %d frames, %d bytes, sections %s" % (path, len(states), len(out), marks))
def legend_start():                                    # as the arcade (0x447AC): pages 2, 1, 0, then SCORE PER COIN
    uc.mem_write(0x90491A, struct.pack(">H", 2))
    run(0x4529A, []); run(0x452D0, [0xFFFFFFFF]); run(0x44414, [0xFFFFFFFC])
if page == "capture":                                  # title -> demo game -> legend 3 pages -> score page -> stop
    seq = []; marks = []; prev_state = None
    for f in range(20000):
        st = struct.unpack(">h", uc.mem_read(0x904918, 2))[0]
        if prev_state is not None and st == -2 and prev_state == -1: break
        if st != prev_state: marks.append((len(seq), st)); prev_state = st
        seq.append(grab_state())
        if not frame(): break
    write_g2at(out, seq, marks)
    sys.exit(0)
every = int(every_arg)     # contact sheet: one picture every N frames
shots = []
for f in range(frames):
    if every and f % every == 0: shots.append((f, snap()))
    if not frame(): break
shots.append((frames, snap()))
if len(shots) == 1:
    shots[0][1].resize((W * 2, H * 2), Image.NEAREST).save(out)
else:
    from PIL import ImageDraw
    cols = 4; rows = (len(shots) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (W + 4), rows * (H + 14)), (40, 40, 40)); d = ImageDraw.Draw(sheet)
    for k, (f, im) in enumerate(shots):
        x, y = (k % cols) * (W + 4), (k // cols) * (H + 14)
        sheet.paste(im, (x, y + 12)); d.text((x + 2, y), "frame %d" % f, fill=(255, 255, 0))
    sheet.save(out)
