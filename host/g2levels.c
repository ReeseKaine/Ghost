/* g2levels.c -- the Gauntlet II level set gauntlet2_levels.bin (g2_convert.py) for the launcher's
   data steps: the arcade mazes (data_g2_mazes, from the player's gauntlet2.zip) turned into
   Gauntlet 4 level records, each checked by decoding it again with Gauntlet 4's own map decoder
   (the lifted 0x19122, from the player's Gauntlet 4 ROM), plus the host's per-maze extras. The
   dragons' cells are found by running the arcade maze set-up for level 12 (dragon set-up 0x5496E;
   levels/g2_dragons.json holds the same numbers). Same bytes as g2_convert.py; format in its
   docstring. Nothing from the games is in this file: the tables below are the port's own choices
   of Gauntlet 4 stand-ins and colour sets. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "musashi/m68k.h"
#include "../c/g4_m68k.h"
#include "g4data.h"
#include "g2run.h"

/* Gauntlet II cell code -> Gauntlet 4 code (-1: a stand-in, STAND_IN) */
static int target_of(int c)
{
    static const short t[64] = {
        0x00, -1, 0x09, -1, 0x09, 0x10, 0x09, 0x0A, 0x0B, 0x0C, 0x14, 0x15, 0x16, 0x07, 0x08, 0xFC,
        0x04, 0x04, 0x82, 0x8A, 0x92, 0xA2, 0x9A, 0x8A, 0xA8, -1, -1, -1, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x1E, -1, 0x1E, 0x22, 0x25, 0x23, 0x24, 0x1B,
        0x1D, -1, -1, -1, -1, -1, -1, -1, 0x13, 0x09};
    if (c >= 0x1C && c < 0x2E) { static const int base[6] = {0x84, 0x8C, 0x94, 0xA4, 0x9C, 0x8C}; return base[(c - 0x1C) / 3] + (c - 0x1C) % 3; }
    return c >= 0 && c < 64 ? t[c] : -1;
}
static int stand_in(int c)
{
    switch (c) { case 0x03: return 0x09; case 0x1A: return 0x9A; case 0x2F: return 0x07; default: return 0x00; }
}
static const int COL_FLOOR[16] = {0, 1, 0, 2, 3, 4, 5, 6, 7, 8, 9, 4, 8, 13, 14, 15};   /* levels/g2_colour_to_g4.json */
static const int COL_WALL[16] = {0, 1, 2, 7, 4, 5, 6, 7, 1, 9, 15, 14, 12, 13, 14, 10};

static uint8_t remap[64]; static int rawof[256];
static uint32_t rule_of(const uint8_t *h) { return (uint32_t)h[1] << 24 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 8 | h[4]; }

static void convert_map(const uint8_t *gmap, int cycling, uint8_t *raw)
{
    int i, start = 0;
    for (i = 0; i < 1024; i++) {
        int c = gmap[i], g;
        if (c == 0x0F) { if (start) c = 0x00; start = 1; }
        if (cycling && (c == 0x07 || c == 0x08 || c == 0x09)) g = 0x09;
        else if ((g = target_of(c)) < 0) g = stand_in(c);
        if (i < 32) g = 0x09;
        raw[i] = (uint8_t)rawof[g];
    }
}
static size_t encode(const uint8_t *raw, uint8_t *out)
{
    size_t o = 0; int i = 0x20;
    while (i < 1024) {
        int t = raw[i], n;
        if (t == 0) { n = 1; while (i + n < 1024 && raw[i + n] == 0 && n < 32) n++; out[o++] = (uint8_t)(0xC0 | (n - 1)); i += n; continue; }
        out[o++] = (uint8_t)(t & 0x3F); i++;
        n = 0; while (i + n < 1024 && raw[i + n] == t && n < 32) n++;
        if (n) { out[o++] = (uint8_t)(0x80 | (n - 1)); i += n; }
    }
    return o;
}
static void header(const uint8_t *h, uint8_t *out)
{
    int fp = h[5] >> 4, wp = h[5] & 15, fc = COL_FLOOR[h[6] >> 4], wc = COL_WALL[h[6] & 15];
    if (wp > 4) wp = 0;
    memset(out, 0, 14); out[0] = 'g'; out[1] = '2';
    out[7] = (uint8_t)((h[4] & 0x20 ? 0x08 : 0) | (h[4] & 0x10 ? 0x04 : 0));
    out[8] = (uint8_t)(fp << 4 | wp); out[9] = (uint8_t)(fc << 4 | wc);
    out[10] = out[11] = out[12] = out[13] = 1;
}
/* Gauntlet 4's decoder on the record (identity remap so the raw tiles come out) */
void (*g4_lookup(uint32_t addr))(M68K *);
extern long g4_ticks;                                  /* g4host: instructions until frame work runs */
static int decode_check(const uint8_t *rec, size_t n, const uint8_t *raw)
{
    M68K cpu; int i; uint8_t save[64];
    memcpy(g4_mem + 0x0F0000, rec, n); memset(g4_mem + 0x0F0000 + n, 0, 64);
    memcpy(save, g4_mem + 0x31D2E, 64);
    for (i = 0; i < 64; i++) g4_mem[0x31D2E + i] = (uint8_t)i;
    g4_mem[0xFFE838] = g4_mem[0xFFE839] = 0;
    g4_mem[0xFFFCFC] = 0x00; g4_mem[0xFFFCFD] = 0x0F; g4_mem[0xFFFCFE] = 0x00; g4_mem[0xFFFCFF] = 0x00;
    memset(&cpu, 0, sizeof cpu); cpu.a[4] = 0xFFE246; cpu.a[7] = 0xFFFD00 - 8;   /* a return address, then the record pointer (4(SP)) */
    {   void (*fn)(M68K *) = g4_lookup(0x019122); long keep_ticks = g4_ticks;
        if (!fn) return 0;
        g4_ticks = 0x3FFFFFFFL;                          /* no frame work (video, sound) in the middle of the check */
        fn(&cpu);
        g4_ticks = keep_ticks; }
    memcpy(g4_mem + 0x31D2E, save, 64);
    for (i = 0; i < 1024; i++) if (g4_mem[0xFFE246 + i] != (i < 32 ? 1 : raw[i])) {
        if (getenv("G4LEVELDEBUG")) { int j; fprintf(stderr, "decode differs at cell %d: got", i); for (j = i; j < i + 8 && j < 1024; j++) fprintf(stderr, " %02X/%02X", g4_mem[0xFFE246 + j], j < 32 ? 1 : raw[j]); fprintf(stderr, "\n"); }
        return 0;
    }
    return 1;
}

#define FIXED(c) ((c) == 0x02 || (c) == 0x04 || (c) == 0x05 || (c) == 0x06 || (c) == 0x07 || (c) == 0x08 || (c) == 0x09 || (c) == 0x3F || \
                  (c) == 0x01 || (c) == 0x0A || (c) == 0x0B || (c) == 0x0C || (c) == 0x10 || (c) == 0x11 || (c) == 0x3E)
static int extras(const uint8_t *gmap, int cycling, uint32_t rules, int goal, int dragon, uint8_t *out_bytes)
{
    short out[1024]; int i, n = 0;
    for (i = 0; i < 1024; i++) out[i] = -1;
    if (rules & 0x1000) out[0] = 13;
    if (rules & 0x2000) out[1] = 16;
    if (rules & 0x08) out[2] = 17;
    if (rules & 0xC000) out[3] = 26;
    if (rules & 0x4000) out[4] = 27;
    if (rules & 0x40) out[5] = 28;
    if (goal) out[6] = (short)(0x80 | goal);
    if (rules & 0x01) out[7] = 29;
    if (rules & 0x02) out[8] = 30;
    if (rules & 0x800000) out[9] = 31;
    if (rules & 0x80000000u) out[10] = 32;
    if (rules & 0x04) out[11] = 33;
    if (dragon >= 0) out[dragon] = 25;
    for (i = 0; i < 1024; i++) {
        int c = gmap[i];
        if (i >= 32 && c == 0x04) out[i] = 14;
        if (i >= 32 && c == 0x06) out[i] = 15;
        if (c == 0x11) out[i] = 18;
        if (c == 0x19) out[i] = 20;
        if (c == 0x1B) out[i] = 21;
        if (c == 0x2F) out[i] = 22;
        if (c == 0x03) out[i] = 23;
        if (c == 0x1A) out[i] = 24;
    }
    for (i = 0; i < 1024; i++) {
        int c = gmap[i];
        if (cycling && (c == 0x07 || c == 0x08 || c == 0x09)) out[i] = (short)(10 + (c - 0x07));
        if (c == 0x01) out[i] = 1;
        if (c == 0x3F) out[i] = 2;
        if (c >= 0x37 && c <= 0x3B) out[i] = (short)(5 + (c - 0x37));
    }
    for (i = 0; i < 1024; i++) {
        int s;
        if (gmap[i] != 0x3F) continue;
        for (s = 0; s < 2; s++) {
            int step = s ? 32 : 1, limit = s ? 16 : 15, j = i, path[32], np = 0, k;
            for (k = 0; k < limit; k++) {
                j += step;
                if (j >= 1024 || (step == 1 && j / 32 != i / 32)) break;
                if (gmap[j] == 0x3F) {
                    int q;
                    for (q = 0; q < np; q++) if (out[path[q]] == -1 || out[path[q]] == 24) out[path[q]] = (short)(step == 1 ? 3 : 4);
                    break;
                }
                if (FIXED(gmap[j])) break;
                path[np++] = j;
            }
        }
    }
    for (i = 0; i < 1024; i++) if (out[i] >= 0) {
        out_bytes[2 + 4 * n] = (uint8_t)(i >> 8); out_bytes[3 + 4 * n] = (uint8_t)i; out_bytes[4 + 4 * n] = (uint8_t)out[i]; out_bytes[5 + 4 * n] = 0; n++;
    }
    out_bytes[0] = (uint8_t)(n >> 8); out_bytes[1] = (uint8_t)n;
    return 2 + 4 * n;
}
static int mirror_cell(int i, int v, int hwrap)
{
    int x = i & 31, y = i >> 5;
    if (v & 1) x = hwrap ? 31 - x : (32 - x) & 31;
    if ((v & 2) && y) y = 32 - y;
    return y * 32 + x;
}
static int mirror_dragon(int cell, int v, int hwrap)
{
    int c[4] = {cell, cell + 1, cell - 32, cell - 31}, k, xs_min = 99, xs_max = -1, ymax = -1, has0 = 0, has31 = 0, nx = 0, seen[32] = {0};
    for (k = 0; k < 4; k++) {
        int m = mirror_cell(c[k], v, hwrap), x = m & 31, y = m >> 5;
        if (!seen[x]) { seen[x] = 1; nx++; }
        if (x < xs_min) xs_min = x; if (x > xs_max) xs_max = x;
        if (y > ymax) ymax = y;
        if (x == 0) has0 = 1; if (x == 31) has31 = 1;
    }
    return ymax * 32 + ((nx == 2 && has0 && has31) ? 31 : xs_min);   /* a block across the wrap edge starts at 31 */
}
/* the dragon's cell for each maze: the arcade's set-up at level 12 calls 0x5496E(cell) */
static int dragon_cell;
static void dragon_hook(void) { dragon_cell = (int)(g2_rd32(g2_reg(M68K_REG_A7) + 4) & 0xFFFF); }
static int find_dragon(const g2_maze *m)
{
    uint32_t a[1];
    g2_reset(m->bank); g2_wr16(0x904004, 12); g2_hook(0x5496E, dragon_hook);
    dragon_cell = -1; a[0] = m->window;
    g2_call(0x44AC2, a, 1, 20000000);
    return dragon_cell;
}
int data_g2_dragons_check(const launch_roms *r)       /* test: print the found cells */
{
    g2_maze *mz; int n = data_g2_mazes(r, &mz), i;
    if (n <= 0) return 0;
    for (i = 0; i < n; i++) if (mz[i].bank >= 0) { int d = find_dragon(&mz[i]); if (d >= 0) printf("%d: %d\n", i + 1, d); }
    free(mz); return 1;
}

int data_g2_levels(const launch_roms *r, const char *out_path)
{
    g2_maze *mz; int n, i, v, k, ok = 1; uint8_t *rom; size_t romn; FILE *f;
    uint8_t *blob, **recs, **exs; size_t *reclen, *exlen, size, off; uint8_t *saved_mem = g4_mem;
    int dragons[256];
    if (!r->g4_path[0] || !(f = fopen(r->g4_path, "rb"))) return 0;
    fseek(f, 0, SEEK_END); romn = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    rom = (uint8_t *)malloc(romn);
    if (!rom || fread(rom, 1, romn, f) != romn || romn < 0x32000) { fclose(f); free(rom); return 0; }
    fclose(f);
    n = data_g2_mazes(r, &mz);
    if (n <= 0) { free(rom); return 0; }
    for (i = 0; i < n; i++) dragons[i] = mz[i].bank >= 0 ? find_dragon(&mz[i]) : -1;
    /* the Gauntlet 4 side: its Arcade remap table (from the ROM) with four unused slots changed */
    memcpy(remap, rom + 0x31D2E, 64);
    remap[0x02] = 0x0B; remap[0x29] = 0x0C; remap[0x3C] = 0x15; remap[0x3D] = 0x16;
    for (i = 0; i < 256; i++) rawof[i] = -1;
    for (i = 0; i < 64; i++) if (rawof[remap[i]] < 0) rawof[remap[i]] = i;
    rawof[0] = 0;
    g4_mem = (uint8_t *)calloc(1, 0x1000000);            /* the decoder's own memory for the checks */
    memcpy(g4_mem, rom, romn < 0x400000 ? romn : 0x400000);
    g4_use_translated = 1;
    recs = (uint8_t **)calloc((size_t)n * 4, sizeof *recs); exs = (uint8_t **)calloc((size_t)n * 4, sizeof *exs);
    reclen = (size_t *)calloc((size_t)n * 4, sizeof *reclen); exlen = (size_t *)calloc((size_t)n * 4, sizeof *exlen);
    for (i = 0; i < n && ok; i++) {
        const g2_maze *m = &mz[i]; uint32_t rules = rule_of(m->header); int cyc = (rules & 0x800) != 0, hwrap = (m->header[4] & 0x20) != 0;
        if (m->bank < 0) { ok = 0; break; }
        for (v = 0; v < 4; v++) {
            uint8_t gmap[1024], raw[1024]; int s = i * 4 + v;
            memcpy(gmap, m->map, 1024);
            for (k = 32; k < 1024; k++) gmap[mirror_cell(k, v, hwrap)] = m->map[k];
            convert_map(gmap, cyc, raw);
            recs[s] = (uint8_t *)malloc(14 + 2048); header(m->header, recs[s]);
            reclen[s] = 14 + encode(raw, recs[s] + 14);
            if (!decode_check(recs[s], reclen[s], raw)) { fprintf(stderr, "level set: maze %d version %d does not decode back\n", i + 1, v); ok = 0; break; }
            exs[s] = (uint8_t *)malloc(2 + 4 * 1024);
            exlen[s] = (size_t)extras(gmap, cyc, rules, m->header[0], dragons[i] >= 0 ? mirror_dragon(dragons[i], v, hwrap) : -1, exs[s]);
        }
    }
    free(g4_mem); g4_mem = saved_mem; free(rom);
    if (ok) {
        size = 8 + 64 + 8 * (size_t)n * 4;
        for (k = 0; k < n * 4; k++) size += reclen[k] + 1 + exlen[k];
        blob = (uint8_t *)calloc(1, size);
        memcpy(blob, "G4L2", 4); blob[4] = (uint8_t)(n >> 8); blob[5] = (uint8_t)n; blob[6] = 0; blob[7] = 2;
        memcpy(blob + 8, remap, 64);
        off = 8 + 64 + 8 * (size_t)n * 4;
        for (k = 0; k < n * 4; k++) {
            uint8_t *p = blob + 72 + 4 * k; p[0] = (uint8_t)(off >> 24); p[1] = (uint8_t)(off >> 16); p[2] = (uint8_t)(off >> 8); p[3] = (uint8_t)off;
            memcpy(blob + off, recs[k], reclen[k]); off += reclen[k];
            if (off & 1) blob[off++] = 0;
        }
        for (k = 0; k < n * 4; k++) {
            uint8_t *p = blob + 72 + 4 * (size_t)n * 4 + 4 * k; p[0] = (uint8_t)(off >> 24); p[1] = (uint8_t)(off >> 16); p[2] = (uint8_t)(off >> 8); p[3] = (uint8_t)off;
            memcpy(blob + off, exs[k], exlen[k]); off += exlen[k];
        }
        if (!(f = fopen(out_path, "wb")) || fwrite(blob, 1, off, f) != off) { if (f) fclose(f); ok = 0; }
        else fclose(f);
        free(blob);
    }
    for (k = 0; k < n * 4; k++) { free(recs[k]); free(exs[k]); }
    free(recs); free(exs); free(reclen); free(exlen); free(mz);
    return ok;
}

/* ------------------------------------------------------------------ GAUNTLET III (user, 2026-11-22) */
/* gauntlet3_levels.bin: the Atari 8-bit Gauntlet's "Deeper Dungeons" disk (128 mazes) plus the 20 mazes
   of the Atari's own side-2 disk that are not arcade Gauntlet mazes, in the same G4L2 format (four mirror
   versions, per-cell extras). File order: the ordinary mazes, then the treasure rooms; header byte 6 =
   how many are ordinary. Read from the player's own .atr images (launch_scan finds them by the game's
   own check: sector 1 byte 0 = 1 original side 2, 2 Deeper Dungeons; sector 2 ends with $FF).
   The record format is our reading of the Atari program's unpacker ($56DE lines, $58F9 objects): maze n
   = 4 sectors from sector 4n; byte 0 length (bit 8 = byte 2 bit 7), byte 1 rules (bit 0 shots hurt,
   bit 1 shots stun, bit 7 no left wall: wraps), byte 3 line bytes; then lines (point = type|x, ?|y;
   run = dir|len), then a run-length object list (bit 7: skip n+1; codes $40+ monsters, $3F start). */
static const int A8_DIRS[8] = {-32, -31, 1, 33, 32, 31, -1, -33};
static const uint8_t A8_TYPE_VAL[8] = {0x00, 0x36, 0x11, 0x00, 0x12, 0x00, 0x90, 0x10};
static int a8_g4(int v)                                 /* an unpacked Atari cell -> Gauntlet 4 code (-1 unknown) */
{
    if (v == 0) return 0x00;
    if (v >= 1 && v <= 0x10) return 0x09;               /* wall */
    if (v >= 0x80 && v <= 0x90) return 0x0A;            /* the second wall kind (arcade raw 57) */
    switch (v) {
    case 0x11: return 0x08; case 0x12: return 0x07;     /* doors */
    case 0x13: return 0x1E; case 0x14: return 0x25; case 0x15: return 0x22;   /* treasure, food, food */
    case 0x16: return 0x24; case 0x17: return 0x23; case 0x18: return 0x1D;   /* potions, invisibility */
    case 0x1F: return 0x1B;                             /* key */
    case 0x2F: return 0x14; case 0x30: return 0x13;     /* trap, transporter */
    case 0x31: return 0x25;                             /* poisoned food: food + extras 26 */
    case 0x33: return 0x10;                             /* special wall */
    case 0x36: return 0x04; case 0x37: return 0x05; case 0x38: return 0x06;   /* exits */
    case 0x3F: return 0xFC;                             /* player start */
    }
    if (v >= 0x20 && v <= 0x2E) { static const int gen[5] = {0x84, 0x8C, 0x94, 0xA4, 0x9C}; return gen[(v - 0x20) / 3] + (v - 0x20) % 3; }
    if (v >= 0x40 && v <= 0x6A && (v & 7) <= 2) {        /* placed monsters: $40 + 8 * kind + level */
        static const int mon[6] = {0x80, 0x88, 0x90, 0xA0, 0x98, 0xA8}; int k = (v - 0x40) >> 3;
        if (k < 6) return k == 5 ? 0xA8 : mon[k] + (v & 7);
    }
    return -1;
}
static int a8_unpack(const uint8_t *rec, int g[1024])  /* the record's cells (Atari codes; monsters/start kept) */
{
    const uint8_t *e = rec + 4, *o; int i, cnt = rec[3], y = 0, typ = 0, ptr = 0, total, left, k = 0, cell = 0;
    for (i = 0; i < 1024; i++) g[i] = 0;
    for (i = 0; i < 32; i++) g[i] = 0x0A;
    if (!(rec[1] & 0x80)) for (i = 1; i < 32; i++) g[i * 32] = 0x05;
#define HI(q) (e[q] & 0xE0)
    while (cnt > 0) {
        int point = 0;
        if (cnt != 1 && HI(y) == HI(y + 1))
            point = cnt == 3 ? HI(y + 1) != HI(y + 2) : (HI(y + 1) != HI(y + 2) || HI(y + 2) == HI(y + 3));
        if (point) {
            typ = e[y] & 0xE0; ptr = (e[y] & 0x1F) + 32 * (e[y + 1] & 0x1F);
            g[ptr] = A8_TYPE_VAL[typ >> 5]; y += 2; cnt -= 2;
        } else {
            int d = e[y] >> 5, n = e[y] & 0x1F, s;
            if (typ == 0x80 || typ == 0x40) typ = (d >> 1) & 1 ? 0x80 : 0x40;
            for (s = 0; s <= n; s++) {
                int p = (ptr + A8_DIRS[d]) & 0x3FF;
                if (d == 2 && p % 32 == 0) p = (p - 32) & 0x3FF;
                if (d == 6 && p % 32 == 31) p = (p + 32) & 0x3FF;
                ptr = p; g[ptr] = A8_TYPE_VAL[typ >> 5];
            }
            y += 1; cnt -= 1;
        }
    }
#undef HI
    total = rec[0] | (rec[2] & 0x80) << 1;
    left = total - rec[3] - 4;
    o = rec + 4 + rec[3];
    while (left > 0 && cell < 1024 && 4 + rec[3] + k < 512) {
        int b = o[k];
        if (b & 0x80) cell += (b & 0x7F) + 1;
        else {
            int rep = 0, r;
            if (left != 1 && o[k + 1] < 0x13) { rep = o[k + 1] + 1; k++; left--; }
            for (r = 0; r <= rep && cell < 1024; r++) g[cell++] = b;
        }
        k++; left--;
    }
    return 1;
}
static uint8_t *atr_load(const char *path, long *n)
{
    FILE *f = fopen(path, "rb"); uint8_t *b; long s;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); s = ftell(f); fseek(f, 0, SEEK_SET);
    if (s < 16 + 128 * 720L) { fclose(f); return NULL; }
    b = (uint8_t *)malloc((size_t)s);
    if (!b || fread(b, 1, (size_t)s, f) != (size_t)s) { fclose(f); free(b); return NULL; }
    fclose(f); *n = s; return b;
}
int atr_side2_kind(const char *path)                   /* 1 Atari Gauntlet side 2, 2 Deeper Dungeons, 0 neither */
{
    long n; uint8_t *b = atr_load(path, &n); int k = 0;
    if (!b) return 0;
    if (b[0] == 0x96 && b[1] == 0x02 && b[16 + 255] == 0xFF && (b[16] == 1 || b[16] == 2)) k = b[16];
    free(b); return k;
}
/* The Commodore 64 Deeper Dungeons tape, side 2 (raw .tap, "C64-TAPE-RAW"): the same maze records as
   the Atari disk. Its turbo format (our reading): one pulse per bit (short ~32 = 0, long ~64 = 1), most
   significant bit first; each block = a pilot of $20 bytes, $FF, $41, a 15-character name, $00, the load
   address $2000 and an end byte, then 5120 bytes = ten 512-byte maze slots. 15 blocks: slots 0-7 of block 0
   = mazes 1-8, of block b = mazes 8b..8b+7 (up to 117); slot 8 = a treasure room (the six even ones,
   118-128); slot 9 = maze 1 again. Differs from the Atari disk only in mazes 47 and 53 (small edits).
   Fills dd[1..128] (512 bytes each, NULL = not on the tape); returns the number of blocks read. */
static int tap_dd_read(const char *path, uint8_t *dd[129])
{
    long n, i, ps = 0; uint8_t *b = NULL, *bits; int blocks = 0, k; FILE *f = fopen(path, "rb"); long nbits = 0;
    uint8_t *troom[16]; int ntroom = 0;
    for (k = 0; k <= 128; k++) dd[k] = NULL;
    if (!f) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 20 || n > 8000000 || !(b = (uint8_t *)malloc((size_t)n)) || fread(b, 1, (size_t)n, f) != (size_t)n || memcmp(b, "C64-TAPE-RAW", 12)) { fclose(f); free(b); return 0; }
    fclose(f);
    bits = (uint8_t *)malloc((size_t)n);                 /* one per pulse; 2 = a pause (block gap) */
    for (i = 20; i < n && bits; i++) {
        int v = b[i];
        if (v == 0) { bits[nbits++] = 2; i += b[12] >= 1 ? 3 : 0; continue; }
        bits[nbits++] = v > 0xF0 ? 2 : v > 48 ? 1 : 0;
    }
    free(b);
    if (!bits) return 0;
    for (i = 0; i + 8 * 64 < nbits; ) {                  /* find "$20 x8, $FF, $41" at any bit position */
        long p = i, q; int ok = 1, m;
        for (m = 0; m < 10 && ok; m++) {
            int v = 0, t;
            for (t = 0; t < 8; t++) { int bit = bits[p + 8 * m + t]; if (bit > 1) { ok = 0; break; } v = v << 1 | bit; }
            if (ok && v != (m < 8 ? 0x20 : m == 8 ? 0xFF : 0x41)) ok = 0;
        }
        if (!ok) { i++; continue; }
        q = p + 8 * 8;                                    /* at the $FF */
        if (q + 8 * (21 + 5120) > nbits) break;
        {   uint8_t *blk = (uint8_t *)malloc(21 + 5120); long t;
            int bad = 0;
            for (t = 0; t < 21 + 5120 && !bad; t++) {
                int v = 0, u; for (u = 0; u < 8; u++) { int bit = bits[q + 8 * t + u]; if (bit > 1) { bad = 1; break; } v = v << 1 | bit; }
                blk[t] = (uint8_t)v;
            }
            if (!bad && blk[17] == 0x00 && blk[18] == 0x20 && blk[20] == 0x34) {   /* load $2000, end $34xx */
                int j;
                for (j = 0; j < 8; j++) {
                    int m = blocks == 0 ? j + 1 : 8 * blocks + j;
                    if (m >= 1 && m <= 117 && !dd[m]) { dd[m] = (uint8_t *)malloc(512); memcpy(dd[m], blk + 21 + 512 * j, 512); }
                }
                {   uint8_t *tr = blk + 21 + 512 * 8; int dup = 0, L = tr[0] | (tr[2] & 0x80) << 1;
                    for (j = 0; j < ntroom; j++) if (!memcmp(troom[j], tr, (size_t)(L > 0 ? L : 1))) dup = 1;
                    if (!dup && ntroom < 16 && L > 4) troom[ntroom++] = tr; }
                blocks++;
                ps = 1;
            }
            if (!ps) free(blk);                           /* kept blocks hold the treasure-room pointers */
            ps = 0;
            i = q + 8 * (21 + 5120);
        }
    }
    free(bits);
    for (k = 0; k < ntroom; k++) {                        /* the six treasure rooms: 118, 120, ... 128 */
        int m = 118 + 2 * k;
        if (m <= 128) { dd[m] = (uint8_t *)malloc(512); memcpy(dd[m], troom[k], 512); }
    }
    return blocks;
}
/* The Commodore 64 Gauntlet as an EasyFlash cartridge (.crt, "C64 CARTRIDGE", e.g. a one-load conversion
   of the tape): its banks hold the Atari's side-2 maze records in the same format, one after another,
   each followed by 0 or 2 more bytes (mostly $00 $1E). Found by walking: from the first offset where 128
   records in a row are plausible (length 12-512, line bytes < length, unpacks with a player start). Slot n
   = the Atari's maze n, except slots 115-117 = mazes 117, 115, 116 (all three are ordinary mazes, so the
   order does not matter here) and slots 31, 70, 71, 79, 87 (C64 changes; not used by GAUNTLET III).
   Fills slot[1..128] (512 bytes each); returns how many. */
static int crt_plausible(const uint8_t *b, long n, long o)
{
    int L, g[1024], i; uint8_t rec[512];
    if (o + 8 > n) return 0;
    L = b[o] | (b[o + 2] & 0x80) << 1;
    if (L < 12 || L > 512 || b[o + 3] < 2 || b[o + 3] >= L - 4 || o + L > n) return 0;
    memset(rec, 0, sizeof rec); memcpy(rec, b + o, (size_t)L);
    a8_unpack(rec, g);
    for (i = 32; i < 1024; i++) if (g[i] == 0x3F) return L;
    return 0;
}
static int crt_orig_read(const char *path, uint8_t *slot[129])
{
    FILE *f = fopen(path, "rb"); long n, p, o, blen = 0; uint8_t *c = NULL, *b; int k, got = 0;
    for (k = 0; k <= 128; k++) slot[k] = NULL;
    if (!f) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0x50 || n > 4000000 || !(c = (uint8_t *)malloc((size_t)n)) || fread(c, 1, (size_t)n, f) != (size_t)n || memcmp(c, "C64 CARTRIDGE   ", 16)) { fclose(f); free(c); return 0; }
    fclose(f);
    b = (uint8_t *)malloc((size_t)n);
    p = (long)c[0x10] << 24 | (long)c[0x11] << 16 | c[0x12] << 8 | c[0x13];
    while (b && p + 16 <= n && !memcmp(c + p, "CHIP", 4)) {    /* the ROM banks, in file order */
        long len = (long)c[p + 4] << 24 | (long)c[p + 5] << 16 | c[p + 6] << 8 | c[p + 7], size = c[p + 14] << 8 | c[p + 15];
        if (len < 16 || p + 16 + size > n) break;
        memcpy(b + blen, c + p + 16, (size_t)size); blen += size; p += len;
    }
    free(c);
    if (!b) return 0;
    for (o = 0; o < blen && !got; o++) {
        long q = o; int cnt = 0, L;
        while (cnt < 128 && (L = crt_plausible(b, blen, q))) {
            cnt++;
            if (cnt < 128) {
                int a = crt_plausible(b, blen, q + L), a2 = crt_plausible(b, blen, q + L + 2);
                if (a && a2) q += (b[q + L] == 0x00 && b[q + L + 1] == 0x1E) || (b[q + L] == 0x80 && b[q + L + 1] == 0xE7) ? L + 2 : L;
                else if (a) q += L; else if (a2) q += L + 2; else break;
            }
        }
        if (cnt == 128) {                               /* found: take them */
            q = o;
            for (k = 1; k <= 128; k++) {
                L = crt_plausible(b, blen, q);
                slot[k] = (uint8_t *)calloc(1, 512); memcpy(slot[k], b + q, (size_t)L);
                if (k < 128) {
                    int a = crt_plausible(b, blen, q + L), a2 = crt_plausible(b, blen, q + L + 2);
                    if (a && a2) q += (b[q + L] == 0x00 && b[q + L + 1] == 0x1E) || (b[q + L] == 0x80 && b[q + L + 1] == 0xE7) ? L + 2 : L;
                    else if (a) q += L; else q += L + 2;
                }
            }
            got = 128;
        }
    }
    free(b);
    return got;
}
int crt_orig_kind(const char *path)                     /* 1 = a C64 Gauntlet cartridge with its mazes */
{
    uint8_t *sl[129]; int k, n = crt_orig_read(path, sl);
    for (k = 0; k <= 128; k++) free(sl[k]);
    return n == 128;
}
int tap_dd_kind(const char *path)                       /* 1 = the C64 Deeper Dungeons maze tape */
{
    uint8_t *dd[129]; int k, n = tap_dd_read(path, dd), ok;
    ok = n >= 10 && dd[1] && dd[117];
    for (k = 0; k <= 128; k++) free(dd[k]);
    return ok;
}
int data_g3_levels(const launch_roms *r, const char *out_path)
{
    static const int orig_extra[20] = {16, 40, 48, 63, 86, 107, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128};
    uint8_t *dd = NULL, *og = NULL, *rom, *blob, *sel_disk[160], *tape[129]; int sel_n[160], n = 0, normal, i, v, k, ok = 1, unknown = 0;
    long ddn, ogn = 0; size_t romn, size, off; FILE *f; uint8_t *saved_mem = g4_mem;
    uint8_t **recs, **exs; size_t *reclen, *exlen;
    for (k = 0; k <= 128; k++) tape[k] = NULL;
    if (r->atr_dd[0]) dd = atr_load(r->atr_dd, &ddn);   /* the Atari disk; else the C64 tape */
    if (!dd && (!r->tap_dd[0] || tap_dd_read(r->tap_dd, tape) < 10)) { for (k = 0; k <= 128; k++) free(tape[k]); return 0; }
    uint8_t *cart[129]; for (k = 0; k <= 128; k++) cart[k] = NULL;
    if (r->atr_orig[0]) og = atr_load(r->atr_orig, &ogn);   /* the Atari's own mazes: its disk, else the C64 cartridge */
    if (!og && r->crt_orig[0]) crt_orig_read(r->crt_orig, cart);
    if (!r->g4_path[0] || !(f = fopen(r->g4_path, "rb"))) { free(dd); free(og); for (k = 0; k <= 128; k++) free(tape[k]); return 0; }
    fseek(f, 0, SEEK_END); romn = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    rom = (uint8_t *)malloc(romn);
    if (!rom || fread(rom, 1, romn, f) != romn || romn < 0x32000) { fclose(f); free(rom); free(dd); free(og); for (k = 0; k <= 128; k++) free(tape[k]); return 0; }
    fclose(f);
    /* the list: ordinary mazes first (Deeper Dungeons 1-117, the original's own), then treasure rooms */
    for (i = 1; i <= 117; i++) if (dd || tape[i]) { sel_disk[n] = dd; sel_n[n++] = i; }
    if (og || cart[1]) for (i = 0; i < 9; i++) { sel_disk[n] = og; sel_n[n++] = 1000 + orig_extra[i]; }
    normal = n;
    for (i = 118; i <= 128; i++) if (dd || tape[i]) { sel_disk[n] = dd; sel_n[n++] = i; }
    if (og || cart[1]) for (i = 9; i < 20; i++) { sel_disk[n] = og; sel_n[n++] = 1000 + orig_extra[i]; }
    memcpy(remap, rom + 0x31D2E, 64);
    remap[0x02] = 0x0B; remap[0x29] = 0x0C; remap[0x3C] = 0x15; remap[0x3D] = 0x16;
    for (i = 0; i < 256; i++) rawof[i] = -1;
    for (i = 0; i < 64; i++) if (rawof[remap[i]] < 0) rawof[remap[i]] = i;
    rawof[0] = 0;
    g4_mem = (uint8_t *)calloc(1, 0x1000000);
    memcpy(g4_mem, rom, romn < 0x400000 ? romn : 0x400000);
    g4_use_translated = 1;
    recs = (uint8_t **)calloc((size_t)n * 4, sizeof *recs); exs = (uint8_t **)calloc((size_t)n * 4, sizeof *exs);
    reclen = (size_t *)calloc((size_t)n * 4, sizeof *reclen); exlen = (size_t *)calloc((size_t)n * 4, sizeof *exlen);
    for (i = 0; i < n && ok; i++) {
        uint8_t rec[512]; int a[1024], g4[1024], poison[1024], hwrap;
        {   int m = sel_n[i] % 1000;                    /* 1000+: the Atari's own (side 2) mazes */
            if (sel_disk[i]) for (k = 0; k < 4; k++) memcpy(rec + 128 * k, sel_disk[i] + 16 + 128L * (4 * m + k - 1), 128);
            else memcpy(rec, sel_n[i] >= 1000 ? cart[m] : tape[m], 512);   /* no disk: the C64 cartridge / tape copy */
        }
        a8_unpack(rec, a);
        hwrap = (rec[1] & 0x80) != 0;
        for (k = 0; k < 1024; k++) {
            int t = a8_g4(a[k]);
            if (t < 0) { unknown++; t = 0; }
            g4[k] = t; poison[k] = a[k] == 0x31;
        }
        for (v = 0; v < 4; v++) {
            uint8_t raw[1024], *h; int s = i * 4 + v, m[1024], pz[1024], ne = 0;
            for (k = 0; k < 1024; k++) { m[k] = g4[k]; pz[k] = 0; }
            for (k = 32; k < 1024; k++) { m[mirror_cell(k, v, hwrap)] = g4[k]; pz[mirror_cell(k, v, hwrap)] = poison[k]; }
            for (k = 0; k < 1024; k++) {
                int c = k < 32 ? 0x09 : m[k];
                if (rawof[c] < 0) { fprintf(stderr, "GAUNTLET III: no tile number for code %02X\n", c); ok = 0; break; }
                raw[k] = (uint8_t)rawof[c];
            }
            if (!ok) break;
            h = recs[s] = (uint8_t *)malloc(14 + 2048);
            memset(h, 0, 14); h[0] = 'g'; h[1] = '3';
            {   /* colours (the Atari, 0x29EB): record byte 2 bits 3-5 = a colour scheme -> hue table $2A11 (7 entries;
                   scheme 7 reads the next byte, $A9); walls in that hue (3 brightnesses), the floor black; treasure
                   rooms always scheme 4 (0x11BC). Here: Gauntlet 4's closest wall colour set, its darkest floor,
                   and the hue kept in header byte 2 (0x80 | hue) for the exact colours (g4host.c) */
                static const uint8_t hue_of[8] = {0x3, 0xC, 0x7, 0x5, 0x1, 0x0, 0xA, 0xA};
                static const int8_t g4wall[16] = {1, 9, 9, 4, 4, 12, 12, 13, 13, 3, 3, 3, 7, 7, 7, 9};
                int scheme = (i >= normal) ? 4 : (rec[2] >> 3) & 7, hue = hue_of[scheme];
                h[2] = (uint8_t)(0x80 | hue);
                h[8] = (uint8_t)((i % 16) << 4 | (i % 5)); h[9] = (uint8_t)(0 << 4 | g4wall[hue]);
            }
            h[7] = (uint8_t)(hwrap ? 0x08 : 0);
            h[10] = h[11] = h[12] = h[13] = 1;
            reclen[s] = 14 + encode(raw, recs[s] + 14);
            if (!decode_check(recs[s], reclen[s], raw)) { fprintf(stderr, "GAUNTLET III: maze %d version %d does not decode back\n", i + 1, v); ok = 0; break; }
            exs[s] = (uint8_t *)malloc(2 + 4 * 1024);
            {   uint8_t *x = exs[s];
#define EX(cell, val) do { x[2 + 4 * ne] = (uint8_t)((cell) >> 8); x[3 + 4 * ne] = (uint8_t)(cell); x[4 + 4 * ne] = (uint8_t)(val); x[5 + 4 * ne] = 0; ne++; } while (0)
                if (rec[1] & 0x02) EX(7, 29);           /* shots stun other players */
                if (rec[1] & 0x01) EX(8, 30);           /* shots hurt other players */
                for (k = 32; k < 1024; k++) if (pz[k]) EX(k, 26);   /* poisoned food */
#undef EX
                x[0] = (uint8_t)(ne >> 8); x[1] = (uint8_t)ne; exlen[s] = 2 + 4 * (size_t)ne;
            }
        }
    }
    free(g4_mem); g4_mem = saved_mem; free(rom); free(dd); free(og);
    for (k = 0; k <= 128; k++) { free(tape[k]); free(cart[k]); }
    if (unknown) fprintf(stderr, "GAUNTLET III: %d cells of unknown kinds left as floor\n", unknown);
    if (ok) {
        size = 8 + 64 + 8 * (size_t)n * 4;
        for (k = 0; k < n * 4; k++) size += reclen[k] + 1 + exlen[k];
        blob = (uint8_t *)calloc(1, size);
        memcpy(blob, "G4L2", 4); blob[4] = (uint8_t)(n >> 8); blob[5] = (uint8_t)n; blob[6] = (uint8_t)normal; blob[7] = 2;
        memcpy(blob + 8, remap, 64);
        off = 8 + 64 + 8 * (size_t)n * 4;
        for (k = 0; k < n * 4; k++) {
            uint8_t *p = blob + 72 + 4 * k; p[0] = (uint8_t)(off >> 24); p[1] = (uint8_t)(off >> 16); p[2] = (uint8_t)(off >> 8); p[3] = (uint8_t)off;
            memcpy(blob + off, recs[k], reclen[k]); off += reclen[k];
            if (off & 1) blob[off++] = 0;
        }
        for (k = 0; k < n * 4; k++) {
            uint8_t *p = blob + 72 + 4 * (size_t)n * 4 + 4 * k; p[0] = (uint8_t)(off >> 24); p[1] = (uint8_t)(off >> 16); p[2] = (uint8_t)(off >> 8); p[3] = (uint8_t)off;
            memcpy(blob + off, exs[k], exlen[k]); off += exlen[k];
        }
        if (!(f = fopen(out_path, "wb")) || fwrite(blob, 1, off, f) != off) { if (f) fclose(f); ok = 0; }
        else { fclose(f); fprintf(stderr, "GAUNTLET III: %d mazes (%d ordinary, %d treasure rooms) -> %s\n", n, normal, n - normal, out_path); }
        free(blob);
    }
    for (k = 0; k < n * 4; k++) { free(recs[k]); free(exs[k]); }
    free(recs); free(exs); free(reclen); free(exlen);
    return ok;
}
