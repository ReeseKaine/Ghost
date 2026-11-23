/* g2attract.c -- g2_attract.bin (g2_attract.py "capture") for the launcher's data steps: the arcade
   Gauntlet II attract screens (title, demo game, legend pages, SCORE PER COIN), recorded by running
   the arcade program itself, frame by frame, on Musashi (g2run.c), from the player's own
   gauntlet2.zip. Same set-up as the Python tool: render_gauntlet2's maze-104 set-up, the slapstic
   bank routines and the two vblank waits hooked, the high-score block given the program's own
   defaults, then per frame the vblank interrupt (0x4017E up to its RTE) and one pass of the main
   loop (0x42A7E..0x42B66). Each frame's display memory is stored as changes (G2AT version 2, see
   write_g2at in g2_attract.py). Nothing from the game is in this file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "musashi/m68k.h"
#include "g4data.h"
#include "g2run.h"

static uint8_t *M;                                     /* the runner's memory */
static void w16(uint32_t a, unsigned v) { M[a] = (uint8_t)(v >> 8); M[a + 1] = (uint8_t)v; }
static void w32(uint32_t a, uint32_t v) { w16(a, v >> 16); w16(a + 2, v & 0xFFFF); }
static unsigned r16(uint32_t a) { return (unsigned)M[a] << 8 | M[a + 1]; }
static uint32_t r32(uint32_t a) { return (uint32_t)r16(a) << 16 | r16(a + 2); }

/* the slapstic: the program writes $38000 then $38010 + 4*bank, always from five small routines */
static void bank_hook_at(uint32_t addr)
{
    uint32_t off, sp;
    if (addr == 0x56E58) off = 0x10;
    else if (addr == 0x56E6E) off = 0x1C;
    else { int32_t d0 = (int16_t)(g2_reg(M68K_REG_D0) & 0xFFFF); off = (g2_reg(M68K_REG_A0) + (uint32_t)d0 - 0x38000) & 0x1FFF; }
    if (off >= 0x10 && off < 0x20) g2_set_bank((int)((off - 0x10) >> 2));
    sp = g2_reg(M68K_REG_A7);                          /* return from the routine */
    g2_set_reg(M68K_REG_PC, r32(sp)); g2_set_reg(M68K_REG_A7, sp + 4);
}
static void bh0(void) { bank_hook_at(0x56E58); } static void bh1(void) { bank_hook_at(0x56E6E); }
static void bh2(void) { bank_hook_at(0x56E84); } static void bh3(void) { bank_hook_at(0x56E90); } static void bh4(void) { bank_hook_at(0x56E98); }
static void vbl_wait(void) { w16(0x904002, 1); }       /* tst.w $904002 waits: let them through */

static void call(uint32_t entry, const uint32_t *args, int n)   /* render_gauntlet2's run(): registers kept */
{
    uint32_t sp = 0x904E00; int k;
    w32(sp, 0x7FF00);
    for (k = 0; k < n; k++) w32(sp + 4 + 4u * (uint32_t)k, args[k]);
    g2_set_reg(M68K_REG_SR, 0x2700); g2_set_reg(M68K_REG_A7, sp);
    g2_run(entry, 0x7FF00, 20000000);
}
static void vblank(void)                               /* the level-4 interrupt, up to its RTE */
{
    uint32_t sp = 0x904E00 - 0x80;
    memset(M + 0x803000, 0xFF, 10);                   /* joysticks released; self-test switch off */
    w16(sp, 0x2000); w32(sp + 2, 0x7FF00); w16(sp + 6, 0x0070);   /* 68020 frame: SR, PC, format/vector */
    g2_set_reg(M68K_REG_SR, 0x2400); g2_set_reg(M68K_REG_A7, sp);
    g2_run(0x4017E, 0x4049E, 2000000);
}
static int frame(void)                                 /* one pass of the main loop */
{
    uint32_t sp = 0x904E00 - 0x40;
    vblank();
    w16(0x904002, 1);
    memset(M + 0x803000, 0xFF, 10);
    g2_set_reg(M68K_REG_A2, 0x904002); g2_set_reg(M68K_REG_A6, sp + 0x20); g2_set_reg(M68K_REG_A3, sp + 0x1E);
    g2_set_reg(M68K_REG_SR, 0x2700); g2_set_reg(M68K_REG_A7, sp);
    return g2_run(0x42A7E, 0x42B66, 5000000);
}

/* the display memory of one frame (REGIONS in g2_attract.py) */
static const struct { uint32_t a, n; } REG[7] = {{0x900000, 0x2000}, {0x902000, 0x2000}, {0x905000, 0x0F00}, {0x905F80, 0x0080},
                                                {0x910000, 0x0800}, {0x905F6E, 0x0002}, {0x904008, 0x0004}};
#define STATE 0x5786
static void grab(uint8_t *st) { int k; size_t o = 0; for (k = 0; k < 7; k++) { memcpy(st + o, M + REG[k].a, REG[k].n); o += REG[k].n; } }

/* tiles and alpha characters as g2_attract.py's tables(): 4 bits per pixel, two per byte */
#define PLANE 0x18000
static int load_tiles(const launch_roms *r, uint8_t *out)   /* 0x3000 tiles + 1024 characters, 32 bytes each */
{
    static const struct { uint32_t off; const char *name; uint32_t size; } parts[] = {
        {0x00000, "136043-1111.1a", 0x8000}, {0x08000, "136037-112.1b", 0x8000}, {0x10000, "136043-1123.1c", 0x4000},
        {0x14000, "136043-1123.1c", 0x4000}, {0x18000, "136043-1113.1l", 0x8000}, {0x20000, "136037-114.1mn", 0x8000},
        {0x28000, "136043-1124.1p", 0x4000}, {0x2C000, "136043-1124.1p", 0x4000}, {0x30000, "136043-1115.2a", 0x8000},
        {0x38000, "136037-116.2b", 0x8000}, {0x40000, "136043-1125.2c", 0x4000}, {0x44000, "136043-1125.2c", 0x4000},
        {0x48000, "136043-1117.2l", 0x8000}, {0x50000, "136037-118.2mn", 0x8000}, {0x58000, "136043-1126.2p", 0x4000},
        {0x5C000, "136043-1126.2p", 0x4000}};
    uint8_t *gfx = (uint8_t *)calloc(1, 0x60000), *ach; size_t n, i; unsigned k; int code, y, x;
    for (k = 0; k < sizeof parts / sizeof parts[0]; k++) {
        uint8_t *d = zip_read(launch_zip_for(r, parts[k].name), parts[k].name, &n);
        if (!d || n < parts[k].size) { free(d); free(gfx); return 0; }
        memcpy(gfx + parts[k].off, d, parts[k].size); free(d);
    }
    for (i = 0; i < 0x60000; i++) gfx[i] = (uint8_t)(255 - gfx[i]);
    for (code = 0; code < 0x3000; code++)
        for (y = 0; y < 8; y++) for (x = 0; x < 8; x += 2) {
            int v[2], j, q;
            for (j = 0; j < 2; j++) { v[j] = 0; for (q = 0; q < 4; q++) v[j] |= ((gfx[(3 - q) * PLANE + code * 8 + y] >> (7 - (x + j))) & 1) << (3 - q); }
            out[code * 32 + y * 4 + x / 2] = (uint8_t)(v[0] << 4 | v[1]);
        }
    free(gfx);
    ach = zip_read(launch_zip_for(r, "136043-1104.6p"), "136043-1104.6p", &n);
    if (!ach || n < 1024 * 16) { free(ach); return 0; }
    for (code = 0; code < 1024; code++)
        for (y = 0; y < 8; y++) for (x = 0; x < 8; x += 2) {
            int v[2], j;
            for (j = 0; j < 2; j++) { int b = ach[code * 16 + y * 2 + ((x + j) >> 2)], s = 3 - ((x + j) & 3); v[j] = ((b >> (s + 4)) & 1) << 1 | ((b >> s) & 1); }
            out[0x3000 * 32 + code * 32 + y * 4 + x / 2] = (uint8_t)(v[0] << 4 | v[1]);
        }
    free(ach);
    return 1;
}

typedef struct { uint8_t *buf; size_t n, cap; } outbuf;
static void out_put(outbuf *b, const void *p, size_t n)
{
    if (b->n + n > b->cap) { b->cap = (b->n + n) * 2; b->buf = (uint8_t *)realloc(b->buf, b->cap); }
    memcpy(b->buf + b->n, p, n); b->n += n;
}
static void le16(outbuf *b, unsigned v) { uint8_t t[2] = {(uint8_t)v, (uint8_t)(v >> 8)}; out_put(b, t, 2); }
static void le32(outbuf *b, uint32_t v) { le16(b, v & 0xFFFF); le16(b, v >> 16); }

int data_g2_attract(const launch_roms *r, const char *out_path, void (*progress)(int frame))
{
    g2_maze *mz; int n = data_g2_mazes(r, &mz), k, nframes = 0, nmarks = 0, prev_state = 0x7FFF, f;
    int mark_f[16], mark_s[16]; uint8_t prev[STATE], cur[STATE], *tiles; outbuf frames = {0, 0, 0}, file = {0, 0, 0}; FILE *fp;
    uint32_t a[2];
    if (n < 104 || mz[103].bank < 0) { free(mz); return 0; }
    tiles = (uint8_t *)malloc((0x3000 + 1024) * 32);
    if (!load_tiles(r, tiles)) { free(mz); free(tiles); return 0; }
    /* render_gauntlet2.py's set-up of maze 104 (the legend's maze), on one machine kept from here on */
    g2_reset(mz[103].bank); M = g2_mem_w();
    for (k = 0; k < 8; k++) { g2_set_reg(M68K_REG_D0 + k, 0); if (k < 7) g2_set_reg(M68K_REG_A0 + k, 0); }   /* a fresh machine's registers */
    a[0] = mz[103].window; call(0x44AC2, a, 1);
    a[0] = r16(0x904B58); a[1] = r16(0x904B5A); call(0x43486, a, 2);
    free(mz);
    g2_hook(0x56E58, bh0); g2_hook(0x56E6E, bh1); g2_hook(0x56E84, bh2); g2_hook(0x56E90, bh3); g2_hook(0x56E98, bh4);
    g2_hook(0x430E0, vbl_wait); g2_hook(0x434B6, vbl_wait);
    for (k = 0; k < 30; k++) { int j; for (j = 0; j < 29; j++) w16(0x905000 + 128u * (uint32_t)k + 2u * (uint32_t)j, 0x8000); }
    memset(M + 0x918000, 0, 0x400); w32(0x904FFC, 0x918000);   /* an empty high-score block: */
    call(0x49BD0, NULL, 0);                            /* the program fills in its default table */
    a[0] = 0xFFFFFFFEu; call(0x44414, a, 1);           /* attract state -2: the title */
    memset(prev, 0, sizeof prev);
    for (f = 0; f < 20000; f++) {                      /* title -> demo -> legend pages -> score page -> stop */
        int st = (int16_t)r16(0x904918); size_t recstart, i;
        if (prev_state != 0x7FFF && st == -2 && prev_state == -1) break;
        if (st != prev_state && nmarks < 16) { mark_f[nmarks] = nframes; mark_s[nmarks] = st; nmarks++; prev_state = st; }
        grab(cur);
        recstart = frames.n; le32(&frames, 0); le16(&frames, 0);
        {   size_t runs = 0; i = 0;
            while (i < STATE) {
                size_t s, e, last;
                if (cur[i] == prev[i]) { i++; continue; }
                s = last = i;
                for (e = i + 1; e < STATE; e++) { if (cur[e] != prev[e]) { if (e - last > 4) break; last = e; } }
                le16(&frames, (unsigned)s); le16(&frames, (unsigned)(last - s + 1)); out_put(&frames, cur + s, last - s + 1);
                runs++; i = last + 1;
            }
            frames.buf[recstart + 4] = (uint8_t)runs; frames.buf[recstart + 5] = (uint8_t)(runs >> 8);
            {   uint32_t len = (uint32_t)(frames.n - recstart - 4);
                frames.buf[recstart] = (uint8_t)len; frames.buf[recstart + 1] = (uint8_t)(len >> 8); frames.buf[recstart + 2] = (uint8_t)(len >> 16); frames.buf[recstart + 3] = (uint8_t)(len >> 24); }
        }
        memcpy(prev, cur, sizeof prev); nframes++;
        if (progress && (f & 63) == 0) progress(f);
        if (!frame()) break;
    }
    out_put(&file, "G2AT", 4); le16(&file, 2); le16(&file, 336); le16(&file, 240); le16(&file, (unsigned)nframes); le16(&file, (unsigned)nmarks);
    for (k = 0; k < nmarks; k++) { le16(&file, (unsigned)mark_f[k]); le16(&file, (unsigned)(mark_s[k] & 0xFFFF)); }
    le16(&file, 0x3000); le16(&file, 1024);
    out_put(&file, tiles, (0x3000 + 1024) * 32); free(tiles);
    out_put(&file, frames.buf, frames.n); free(frames.buf);
    fp = fopen(out_path, "wb");
    if (!fp || fwrite(file.buf, 1, file.n, fp) != file.n) { if (fp) fclose(fp); free(file.buf); return 0; }
    fclose(fp); free(file.buf);
    return nframes;
}
