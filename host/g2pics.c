/* g2pics.c -- Gauntlet II pictures and colours for the launcher's data steps, made from the
   player's own gauntlet2.zip by running the arcade program's maze set-up and palette loader on
   Musashi (g2run.c):
     data_g2_sprites   g2_sprites.bin                       (g2_sprites.py)
     data_g2_colours   the Gauntlet II lines of arcade_colours.txt   (g2_arcade_colours.py)
   Same bytes / same text as the Python tools. Nothing from the game is in this file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "musashi/m68k.h"
#include "g4data.h"
#include "g2run.h"

/* the graphics ROMs: 4 planes of 0x18000 bytes, stored inverted */
#define PLANE 0x18000
static uint8_t *gfx;
static int load_gfx(const launch_roms *r)
{
    static const struct { uint32_t off; const char *name; uint32_t size; } parts[] = {
        {0x00000, "136043-1111.1a", 0x8000}, {0x08000, "136037-112.1b", 0x8000}, {0x10000, "136043-1123.1c", 0x4000},
        {0x14000, "136043-1123.1c", 0x4000}, {0x18000, "136043-1113.1l", 0x8000}, {0x20000, "136037-114.1mn", 0x8000},
        {0x28000, "136043-1124.1p", 0x4000}, {0x2C000, "136043-1124.1p", 0x4000}, {0x30000, "136043-1115.2a", 0x8000},
        {0x38000, "136037-116.2b", 0x8000}, {0x40000, "136043-1125.2c", 0x4000}, {0x44000, "136043-1125.2c", 0x4000},
        {0x48000, "136043-1117.2l", 0x8000}, {0x50000, "136037-118.2mn", 0x8000}, {0x58000, "136043-1126.2p", 0x4000},
        {0x5C000, "136043-1126.2p", 0x4000}};
    unsigned k; size_t i;
    if (gfx) return 1;
    gfx = (uint8_t *)calloc(1, 0x60000);
    for (k = 0; k < sizeof parts / sizeof parts[0]; k++) {
        size_t n; uint8_t *d = zip_read(launch_zip_for(r, parts[k].name), parts[k].name, &n);
        if (!d || n < parts[k].size) { free(d); free(gfx); gfx = NULL; return 0; }
        memcpy(gfx + parts[k].off, d, parts[k].size); free(d);
    }
    for (i = 0; i < 0x60000; i++) gfx[i] = (uint8_t)(255 - gfx[i]);
    return 1;
}
static void tile(int code, uint8_t out[64])
{
    int r, x, k; code %= PLANE / 8;
    for (r = 0; r < 8; r++) {
        uint8_t p[4];
        for (k = 0; k < 4; k++) p[k] = gfx[(3 - k) * PLANE + code * 8 + r];
        for (x = 0; x < 8; x++) { int v = 0; for (k = 0; k < 4; k++) v |= ((p[k] >> (7 - x)) & 1) << (3 - k); out[r * 8 + x] = (uint8_t)v; }
    }
}
static void irgb(unsigned w, int c[3])
{
    unsigned i = ((w >> 12) & 15) * 17;
    c[0] = (int)((i * ((w >> 8) & 15) * 17) >> 8); c[1] = (int)((i * ((w >> 4) & 15) * 17) >> 8); c[2] = (int)((i * (w & 15) * 17) >> 8);
}
static uint8_t pal[0x800]; static int last_bank;
static void maze_palette(const g2_maze *m, int set_level)   /* maze set-up 0x44AC2 + palette loader 0x43486 */
{
    uint32_t a[2];
    g2_reset(m->bank);
    if (set_level) g2_wr16(0x904004, 12);
    a[0] = m->window; g2_call(0x44AC2, a, 1, 20000000);
    a[0] = g2_rd16(0x904B58); a[1] = g2_rd16(0x904B5A); g2_call(0x43486, a, 2, 20000000);
    last_bank = (int)(g2_rd16(0x905F6E) & 3);
    memcpy(pal, g2_mem() + 0x910000, 0x800);
}
static unsigned pal_word(unsigned off) { return (unsigned)pal[off] << 8 | pal[off + 1]; }

/* ------------------------------------------------------------------ g2_sprites.bin */
typedef struct { uint8_t *buf; size_t n, cap; } blob_t;
static void put(blob_t *b, const void *p, size_t n)
{
    if (b->n + n > b->cap) { b->cap = (b->n + n) * 2; b->buf = (uint8_t *)realloc(b->buf, b->cap); }
    memcpy(b->buf + b->n, p, n); b->n += n;
}
static void put_head(blob_t *b, int id, int w, int h) { uint8_t t[4] = {(uint8_t)(id >> 8), (uint8_t)id, (uint8_t)w, (uint8_t)h}; put(b, t, 4); }
static void picture(blob_t *b, int id, int code, int wt, int ht, int colour)
{
    int w = wt * 8, h = ht * 8, tx, ty, x, y; uint8_t *px = (uint8_t *)calloc((size_t)(w * h * 4), 1), t[64];
    for (ty = 0; ty < ht; ty++) for (tx = 0; tx < wt; tx++) {
        tile(code + ty * wt + tx, t);                  /* row by row */
        for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) {
            int v = t[y * 8 + x], c[3]; uint8_t *o;
            if (!v) continue;
            irgb(pal_word(0x200 + 32u * (unsigned)colour + 2u * (unsigned)v), c);
            o = px + ((ty * 8 + y) * w + tx * 8 + x) * 4; o[0] = (uint8_t)c[0]; o[1] = (uint8_t)c[1]; o[2] = (uint8_t)c[2]; o[3] = 255;
        }
    }
    put_head(b, id, w, h); put(b, px, (size_t)(w * h * 4)); free(px);
}
static void post_picture(blob_t *b, int id, int k)    /* force-field post k: table 0x5BA70[k], 4 playfield tiles */
{
    const uint8_t *prog = g2_prog(); uint32_t p = (uint32_t)prog[0x5BA70 + 4 * k] << 24 | (uint32_t)prog[0x5BA71 + 4 * k] << 16 | (uint32_t)prog[0x5BA72 + 4 * k] << 8 | prog[0x5BA73 + 4 * k];
    uint8_t px[16 * 16 * 4], t[64]; int j, x, y;
    for (j = 0; j < 4; j++) {
        int cx = j & 1, cy = j >> 1; unsigned w = (((unsigned)prog[p + 2 * (unsigned)j] << 8 | prog[p + 2 * (unsigned)j + 1]) + 0x4000) & 0xFFFF;
        int c = 0x28 + (int)((w >> 12) & 7), flip = (int)(w >> 15);
        tile(((last_bank * 0x1000) + (int)(w & 0xFFF)) ^ 0x800, t);
        for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) {
            int v = t[y * 8 + (flip ? 7 - x : x)], col[3]; uint8_t *o = px + ((cy * 8 + y) * 16 + cx * 8 + x) * 4;
            irgb(pal_word(2u * (unsigned)(c * 16 + v)), col);
            o[0] = (uint8_t)col[0]; o[1] = (uint8_t)col[1]; o[2] = (uint8_t)col[2]; o[3] = 255;
        }
    }
    put_head(b, id, 16, 16); put(b, px, sizeof px);
}
int data_g2_sprites(const launch_roms *r, const char *out_path)
{
    g2_maze *mz; int n = data_g2_mazes(r, &mz), k, npics = 0; blob_t b = {0, 0, 0}; FILE *f;
    struct { int id, code, wt, ht, colour, maze; } pics[128];
    if (n < 100 || !load_gfx(r)) return 0;
#define PIC(i, c, w, h, col, m) do { pics[npics].id = i; pics[npics].code = c; pics[npics].wt = w; pics[npics].ht = h; pics[npics].colour = col; pics[npics].maze = m; npics++; } while (0)
    PIC(1, 0x2740, 4, 4, 8, 7);
    for (k = 0; k < 8; k++) PIC(2 + k, 0x2100 + 0x10 * k, 4, 4, 8, 7);
    PIC(10, 0x2750, 4, 4, 8, 7); PIC(11, 0x2540, 4, 4, 8, 7); PIC(12, 0x2550, 4, 4, 8, 7);
    for (k = 0; k < 8; k++) PIC(20 + k, 0x2180 + 0x10 * k, 4, 4, 8, 7);
    for (k = 0; k < 8; k++) PIC(30 + k, 0x2200 + 0x10 * k, 4, 4, 8, 7);
    for (k = 0; k < 8; k++) PIC(40 + k, 0x2280 + 0x10 * k, 4, 4, 8, 7);
    for (k = 0; k < 10; k++) PIC(50 + k, 0x2300 + 9 * k, 3, 3, 1, 3);
    PIC(60, 0x25E4, 3, 3, 1, 3); PIC(61, 0x25ED, 3, 3, 1, 3);
    {   /* 90-113 the mugger (its own pictures: spawn 0x4DF06 picks animation table $58E1E for a mugger, colour 1;
           3x3 tiles, 3 walking frames 9 tiles apart): id 90 + 3 * direction + frame, directions N NE E SE S SW W NW */
        static const int first[8] = {0x24C6, 0x24AB, 0x2490, 0x2436, 0x2451, 0x2400, 0x2475, 0x241B};
        int d, f; for (d = 0; d < 8; d++) for (f = 0; f < 3; f++) PIC(90 + 3 * d + f, first[d] + 9 * f, 3, 3, 1, 9);
    }
#undef PIC
    put(&b, "G2SP", 4);
    { uint8_t c[2] = {(uint8_t)((npics + 16) >> 8), (uint8_t)(npics + 16)}; put(&b, c, 2); }
    for (k = 0; k < npics; k++) { maze_palette(&mz[pics[k].maze - 1], 1); picture(&b, pics[k].id, pics[k].code, pics[k].wt, pics[k].ht, pics[k].colour); }
    maze_palette(&mz[99], 1);                         /* maze 100 has force fields */
    for (k = 0; k < 16; k++) post_picture(&b, 70 + k, k);
    free(mz);
    if (!(f = fopen(out_path, "wb")) || fwrite(b.buf, 1, b.n, f) != b.n) { if (f) fclose(f); free(b.buf); return 0; }
    fclose(f); free(b.buf);
    return 1;
}

/* ------------------------------------------------------------------ arcade_colours.txt (Gauntlet II part) */
static double lum3(const int c[3]) { return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]; }
typedef struct { unsigned addr; long n; int c[3]; } use_t;
static int cmp_lum(const void *a, const void *b)       /* stable: equal brightness keeps first-seen order */
{
    const use_t *x = (const use_t *)a, *y = (const use_t *)b; double lx = lum3(x->c), ly = lum3(y->c);
    if (lx < ly) return -1; if (lx > ly) return 1;
    return x->addr < y->addr ? -1 : x->addr > y->addr;  /* (never reached for distinct entries: see below) */
}
static void maze_shades(const g2_maze *m, int out[2][3][3])
{
    unsigned short pf[4096], seen_w[4096]; long seen_n[4096]; int nseen = 0, i, name;
    maze_palette(m, 0);
    for (i = 0; i < 4096; i++) pf[i] = (unsigned short)g2_rd16(0x900000 + 2u * (unsigned)i);
    for (i = 0; i < 4096; i++) {                       /* Counter(pf): distinct words in first-seen order */
        int j; for (j = 0; j < nseen && seen_w[j] != pf[i]; j++) ;
        if (j == nseen) { seen_w[nseen] = pf[i]; seen_n[nseen++] = 0; }
        seen_n[j]++;
    }
    for (name = 0; name < 2; name++) {
        int g7 = name ? 7 : 0, nu = 0, k; use_t use[512]; long total = 0, acc = 0; double bands[3][4];
        int shades[3][3], have[3];
        for (i = 0; i < nseen; i++) {
            unsigned w = seen_w[i]; uint8_t t[64]; int v;
            if ((int)((w >> 12) & 7) != g7) continue;
            tile(((last_bank * 0x1000) + (int)(w & 0xFFF)) ^ 0x800, t);
            for (v = 0; v < 64; v++) if (t[v]) {
                unsigned a = (unsigned)(0x28 + g7) * 32u + 2u * t[v]; int j;
                for (j = 0; j < nu && use[j].addr != a; j++) ;
                if (j == nu) { use[nu].addr = a; use[nu].n = 0; irgb(pal_word(a), use[nu].c); nu++; }
                use[j].n += seen_n[i];
            }
        }
        for (k = 1; k < nu; k++) {                     /* stable insertion sort by brightness (as Python's sort) */
            use_t x = use[k]; int j = k - 1;
            while (j >= 0 && lum3(use[j].c) > lum3(x.c)) { use[j + 1] = use[j]; j--; }
            use[j + 1] = x;
        }
        (void)cmp_lum;
        for (k = 0; k < nu; k++) total += use[k].n;
        if (!total) total = 1;
        memset(bands, 0, sizeof bands);
        for (k = 0; k < nu; k++) {
            int b = (int)(3.0 * ((double)acc + (double)use[k].n / 2.0) / (double)total); if (b > 2) b = 2;
            acc += use[k].n;
            for (i = 0; i < 3; i++) bands[b][i] += (double)use[k].c[i] * (double)use[k].n;
            bands[b][3] += (double)use[k].n;
        }
        for (k = 0; k < 3; k++) { have[k] = bands[k][3] != 0; for (i = 0; i < 3; i++) shades[k][i] = have[k] ? (int)nearbyint(bands[k][i] / bands[k][3]) : 0; }
        if (!have[0] && !have[1] && !have[2]) { for (k = 0; k < 3; k++) { for (i = 0; i < 3; i++) shades[k][i] = 64 * (k + 1); have[k] = 1; } }
        for (k = 0; k < 3; k++) if (!have[k]) {
            int s = -1, j;
            if (k) s = k - 1; else for (j = 0; j < 3; j++) if (have[j]) { s = j; break; }
            for (i = 0; i < 3; i++) shades[k][i] = shades[s][i];
            have[k] = 1;
        }
        memcpy(out[name], shades, sizeof shades);
    }
}
static const char *G2_HEAD = "# arcade Gauntlet II floor/wall shades per Gauntlet II maze (record address as loaded by the host): made by g2_arcade_colours.py";
int data_g2_colours(const launch_roms *r, const char *levels_path, const char *colours_path)
{
    g2_maze *mz; int n = data_g2_mazes(r, &mz), count, versions, k, v, nl = 0; FILE *f; long size;
    uint8_t *lv; char **lines; char *old = NULL, *outbuf; size_t olen = 0;
    if (n < 100 || !load_gfx(r) || !(f = fopen(levels_path, "rb"))) return 0;
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    lv = (uint8_t *)malloc((size_t)size);
    if (!lv || fread(lv, 1, (size_t)size, f) != (size_t)size) { fclose(f); free(lv); return 0; }
    fclose(f);
    count = lv[4] << 8 | lv[5]; versions = (lv[6] << 8 | lv[7]) >= 2 ? 4 : 1;
    lines = (char **)calloc((size_t)(count * versions + 1), sizeof *lines);
    for (k = 0; k < count && k < n; k++) {
        int sh[2][3][3];
        if (mz[k].bank < 0) continue;
        maze_shades(&mz[k], sh);
        for (v = 0; v < versions; v++) {
            int slot = k * versions + v, i, j; uint32_t off = (uint32_t)lv[72 + 4 * slot] << 24 | (uint32_t)lv[73 + 4 * slot] << 16 | (uint32_t)lv[74 + 4 * slot] << 8 | lv[75 + 4 * slot];
            char *l = (char *)malloc(200); int o = snprintf(l, 200, "%06x", 0x380000 + off);
            for (j = 0; j < 2; j++) for (i = 0; i < 3; i++) o += snprintf(l + o, 200 - (size_t)o, " %d %d %d", sh[j][i][0], sh[j][i][1], sh[j][i][2]);
            lines[nl++] = l;
        }
    }
    free(lv); free(mz);
    if ((f = fopen(colours_path, "rb"))) {            /* keep everything but an earlier Gauntlet II part */
        fseek(f, 0, SEEK_END); olen = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
        old = (char *)calloc(olen + 1, 1); if (fread(old, 1, olen, f) != olen) olen = 0; old[olen] = 0; fclose(f);
    }
    outbuf = (char *)malloc(olen + (size_t)nl * 200 + 400); outbuf[0] = 0;
    {   size_t o = 0; int skip = 0; char *p = old;
        while (p && *p) {
            char *e = strchr(p, '\n'); size_t len = e ? (size_t)(e - p) : strlen(p);
            int is_head = len == strlen(G2_HEAD) && !strncmp(p, G2_HEAD, len);
            if (is_head) skip = 1;
            else {
                if (skip && (p[0] == '#' || len == 0 || (len == 1 && p[0] == '\r'))) skip = 0;
                if (!skip) { memcpy(outbuf + o, p, len); o += len; outbuf[o++] = '\n'; }
            }
            if (!e) break; p = e + 1;
        }
        o += (size_t)sprintf(outbuf + o, "%s\n", G2_HEAD);
        for (k = 0; k < nl; k++) { o += (size_t)sprintf(outbuf + o, "%s\n", lines[k]); free(lines[k]); }
        free(lines); free(old);
        if (!(f = fopen(colours_path, "wb")) || fwrite(outbuf, 1, o, f) != o) { if (f) fclose(f); free(outbuf); return 0; }
        fclose(f); free(outbuf);
    }
    return nl;
}
