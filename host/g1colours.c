/* g1colours.c -- the 1985 arcade Gauntlet's floor and wall colours for Gauntlet 4's Arcade levels:
   the first part of arcade_colours.txt (arcade_colours.py, with the maze list of import_arcade.py and
   the level tables of extract_levels.py), for the launcher's data steps. From the player's own
   gauntlet.zip and Gauntlet 4 ROM:
     - the arcade's maze table (bank 0, 0x38032: 114 window addresses); a record's bank is the one
       showing three lowercase initials there; for a record at the window's start (several banks)
       the bank whose record equals Gauntlet 4's for that level (header and decoded map);
     - per maze the arcade's maze loader 0x425A0 and palette loader 0x413AA run on Musashi, and the
       floor (group 0) and wall (group 1) pixels counted into 3 shades;
     - one line per record of Gauntlet 4's two Arcade level tables (its own lookup 0x40D8).
   Same text as the Python tools. Nothing from the games is in this file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "musashi/m68k.h"
#include "../c/g4_m68k.h"
#include "g4data.h"
#include "g2run.h"

void (*g4_lookup(uint32_t addr))(M68K *);
extern long g4_ticks;

/* ---- Gauntlet 4's own code (the lifted routines), on a temporary memory with the player's ROM */
static int g4_call(uint32_t addr, M68K *cpu)
{
    void (*fn)(M68K *) = g4_lookup(addr); long keep = g4_ticks;
    if (!fn) return 0;
    g4_ticks = 0x3FFFFFFFL; fn(cpu); g4_ticks = keep;
    return 1;
}
static uint32_t g4_level_ptr(int level, int alt)      /* 0x40D8: the level's record */
{
    M68K cpu; memset(&cpu, 0, sizeof cpu);
    g4_mem[0xFFE758] = (uint8_t)(level >> 8); g4_mem[0xFFE759] = (uint8_t)level;
    g4_mem[0xFFE790] = alt ? 0 : 0xFF; g4_mem[0xFFE799] = 0;
    cpu.a[7] = 0xFFFD00 - 4;                           /* (a return address) */
    g4_call(0x0040D8, &cpu);
    return (uint32_t)g4_mem[0xFFE75C] << 24 | (uint32_t)g4_mem[0xFFE75D] << 16 | (uint32_t)g4_mem[0xFFE75E] << 8 | g4_mem[0xFFE75F];
}
static void g4_decode_raw(const uint8_t *rec, size_t n, uint8_t *grid)   /* 0x19122, identity remap */
{
    M68K cpu; uint8_t save[64]; int i;
    memcpy(g4_mem + 0x0F0000, rec, n); memset(g4_mem + 0x0F0000 + n, 0, 64);
    memcpy(save, g4_mem + 0x31D2E, 64);
    for (i = 0; i < 64; i++) g4_mem[0x31D2E + i] = (uint8_t)i;
    g4_mem[0xFFE838] = g4_mem[0xFFE839] = 0;
    g4_mem[0xFFFCFC] = 0; g4_mem[0xFFFCFD] = 0x0F; g4_mem[0xFFFCFE] = 0; g4_mem[0xFFFCFF] = 0;
    memset(&cpu, 0, sizeof cpu); cpu.a[4] = 0xFFE246; cpu.a[7] = 0xFFFD00 - 8;
    g4_call(0x019122, &cpu);
    memcpy(grid, g4_mem + 0xFFE246, 1024);
    memcpy(g4_mem + 0x31D2E, save, 64);
}

/* ---- the arcade */
static uint8_t g1prog[0x80000], *g1gfx;
static int zipget(const launch_roms *r, const char *name, uint8_t **out, size_t want)
{
    size_t n; *out = zip_read(launch_zip_for(r, name), name, &n);
    if (!*out || n != want) { free(*out); *out = NULL; return 0; }
    return 1;
}
static int g1_load(const launch_roms *r)
{
    static const char *gnames[8] = {"136037-111.1a", "136037-112.1b", "136037-113.1l", "136037-114.1mn",
                                    "136037-115.2a", "136037-116.2b", "136037-117.2l", "136037-118.2mn"};
    uint8_t *a, *b; size_t i; int k;
    memset(g1prog, 0, sizeof g1prog);
    if (!zipget(r, "136037-1307.9a", &a, 0x8000) || !zipget(r, "136037-1308.9b", &b, 0x8000)) return 0;
    for (i = 0; i < 0x4000; i++) { g1prog[0x8000 + 2 * i] = a[i]; g1prog[0x8001 + 2 * i] = b[i]; g1prog[2 * i] = a[0x4000 + i]; g1prog[2 * i + 1] = b[0x4000 + i]; }
    free(a); free(b);
    if (!zipget(r, "136037-205.10a", &a, 0x4000) || !zipget(r, "136037-206.10b", &b, 0x4000)) return 0;
    for (i = 0; i < 0x4000; i++) { g1prog[0x38000 + 2 * i] = a[i]; g1prog[0x38001 + 2 * i] = b[i]; }
    free(a); free(b);
    if (!zipget(r, "136037-1409.7a", &a, 0x8000) || !zipget(r, "136037-1410.7b", &b, 0x8000)) return 0;
    for (i = 0; i < 0x4000; i++) { g1prog[0x48000 + 2 * i] = a[i]; g1prog[0x48001 + 2 * i] = b[i]; g1prog[0x40000 + 2 * i] = a[0x4000 + i]; g1prog[0x40001 + 2 * i] = b[0x4000 + i]; }
    free(a); free(b);
    free(g1gfx); g1gfx = (uint8_t *)malloc(0x40000);
    for (k = 0; k < 8; k++) {
        if (!zipget(r, gnames[k], &a, 0x8000)) return 0;
        for (i = 0; i < 0x8000; i++) g1gfx[k * 0x8000 + i] = (uint8_t)(255 - a[i]);
        free(a);
    }
    return g2_load_image(g1prog);
}
static void g1_tile(int code, uint8_t out[64])          /* 4 planes of 0x10000 bytes */
{
    int r, x, k;
    for (r = 0; r < 8; r++) {
        uint8_t p[4];
        for (k = 0; k < 4; k++) { size_t o = (size_t)(3 - k) * 0x10000 + (size_t)code * 8 + (size_t)r; p[k] = o < 0x40000 ? g1gfx[o] : 0; }
        for (x = 0; x < 8; x++) { int v = 0; for (k = 0; k < 4; k++) v |= ((p[k] >> (7 - x)) & 1) << (3 - k); out[r * 8 + x] = (uint8_t)v; }
    }
}
static void irgb(unsigned w, int c[3])
{
    unsigned i = ((w >> 12) & 15) * 17;
    c[0] = (int)((i * ((w >> 8) & 15) * 17) >> 8); c[1] = (int)((i * ((w >> 4) & 15) * 17) >> 8); c[2] = (int)((i * (w & 15) * 17) >> 8);
}
static double lum(const int c[3]) { return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]; }
static void maze_shades(uint32_t window, int bank, int out[2][3][3])
{
    const uint8_t *mem; uint32_t a[2]; unsigned short pf[4096], seen_w[4096]; long seen_n[4096]; int nseen = 0, i, g;
    long use_n[8][16]; int use_order[8][16], use_cnt[8];
    g2_reset(bank); a[0] = window; g2_call(0x425A0, a, 1, 5000000);
    a[0] = g2_rd16(0x904964); a[1] = g2_rd16(0x904966); g2_call(0x413AA, a, 2, 5000000);
    mem = g2_mem();
    for (i = 0; i < 4096; i++) pf[i] = (unsigned short)(mem[0x900000 + 2 * i] << 8 | mem[0x900001 + 2 * i]);
    {   int bank_pf = (int)(g2_rd16(0x905F6E) & 3);
        for (i = 0; i < 4096; i++) {                   /* Counter(pf): distinct words, first-seen order */
            int j; for (j = 0; j < nseen && seen_w[j] != pf[i]; j++) ;
            if (j == nseen) { seen_w[nseen] = pf[i]; seen_n[nseen++] = 0; }
            seen_n[j]++;
        }
        memset(use_n, 0, sizeof use_n); memset(use_cnt, 0, sizeof use_cnt);
        for (i = 0; i < nseen; i++) {                  /* use[group][pixel value] in first-seen order */
            uint8_t t[64]; int v, grp = (seen_w[i] >> 12) & 7;
            g1_tile(((bank_pf * 0x1000) + (seen_w[i] & 0xFFF)) ^ 0x800, t);
            for (v = 0; v < 64; v++) {
                int val = t[v], j;
                for (j = 0; j < use_cnt[grp] && use_order[grp][j] != val; j++) ;
                if (j == use_cnt[grp]) { use_order[grp][j] = val; use_n[grp][val] = 0; use_cnt[grp]++; }
                use_n[grp][val] += seen_n[i];
            }
        }
    }
    for (g = 0; g < 2; g++) {                          /* 0 floor, 1 walls */
        int cols[16][3], nc = 0, k, shades[3][3], have[3]; long cn[16], total = 0, acc = 0; double bands[3][4];
        for (k = 0; k < use_cnt[g]; k++) {
            int v = use_order[g][k];
            if (v == 0) continue;                      /* colour 0: transparent */
            irgb((unsigned)(mem[0x910000 + 0x500 + 32 * g + 2 * v] << 8 | mem[0x910000 + 0x501 + 32 * g + 2 * v]), cols[nc]);
            cn[nc++] = use_n[g][v];
        }
        for (k = 1; k < nc; k++) {                     /* stable sort by brightness */
            int c[3] = {cols[k][0], cols[k][1], cols[k][2]}, j = k - 1; long n = cn[k];
            while (j >= 0 && lum(cols[j]) > lum(c)) { memcpy(cols[j + 1], cols[j], sizeof c); cn[j + 1] = cn[j]; j--; }
            memcpy(cols[j + 1], c, sizeof c); cn[j + 1] = n;
        }
        for (k = 0; k < nc; k++) total += cn[k];
        if (!total) total = 1;
        memset(bands, 0, sizeof bands);
        for (k = 0; k < nc; k++) {
            int b = (int)(3.0 * ((double)acc + (double)cn[k] / 2.0) / (double)total), i2; if (b > 2) b = 2;
            acc += cn[k];
            for (i2 = 0; i2 < 3; i2++) bands[b][i2] += (double)cols[k][i2] * (double)cn[k];
            bands[b][3] += (double)cn[k];
        }
        for (k = 0; k < 3; k++) { int i2; have[k] = bands[k][3] != 0; for (i2 = 0; i2 < 3; i2++) shades[k][i2] = have[k] ? (int)nearbyint(bands[k][i2] / bands[k][3]) : 0; }
        for (k = 0; k < 3; k++) if (!have[k]) {
            int s = -1, j, i2;
            if (k) s = k - 1; else for (j = 0; j < 3; j++) if (have[j]) { s = j; break; }
            if (s < 0) s = 0;
            for (i2 = 0; i2 < 3; i2++) shades[k][i2] = shades[s][i2];
            have[k] = 1;
        }
        memcpy(out[g], shades, sizeof shades);
    }
}

#define G1_COUNT 114
int data_g1_colours(const launch_roms *r, const char *colours_path)
{
    uint32_t windows[G1_COUNT]; int bank[G1_COUNT], i, k, nl = 0; uint8_t *rom = NULL, *saved = g4_mem; size_t romn; FILE *f;
    static int sh[G1_COUNT][2][3][3]; uint32_t recs[256]; int rec_level[256];
    if (!r->g4_path[0] || !r->have_g1 || !(f = fopen(r->g4_path, "rb"))) return 0;
    fseek(f, 0, SEEK_END); romn = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    rom = (uint8_t *)malloc(romn);
    if (!rom || fread(rom, 1, romn, f) != romn) { fclose(f); free(rom); return 0; }
    fclose(f);
    if (!g1_load(r)) { free(rom); return 0; }
    g4_mem = (uint8_t *)calloc(1, 0x1000000); memcpy(g4_mem, rom, romn < 0x400000 ? romn : 0x400000);
    g4_use_translated = 1;
    /* the maze table and each record's bank (import_arcade.py) */
    for (i = 0; i < G1_COUNT; i++) {
        uint32_t o = 0x38032 - 0x38000 + 4u * (uint32_t)i, w; int cands[4], nc = 0;
        w = (uint32_t)g1prog[0x38000 + o] << 24 | (uint32_t)g1prog[0x38001 + o] << 16 | (uint32_t)g1prog[0x38002 + o] << 8 | g1prog[0x38003 + o];
        windows[i] = w;
        for (k = 0; k < 4; k++) {
            const uint8_t *p = g1prog + 0x38000 + k * 0x2000 + (w - 0x38000);
            if (w >= 0x38000 && w < 0x3A000 - 3 && p[0] >= 'a' && p[0] <= 'z' && p[1] >= 'a' && p[1] <= 'z' && p[2] >= 'a' && p[2] <= 'z') cands[nc++] = k;
        }
        bank[i] = nc == 1 ? cands[0] : -1;
        if (nc > 1) {                                  /* at the window's start: the bank of Gauntlet 4's record */
            uint32_t g4rec = g4_level_ptr(i + 1, 0); uint8_t g4raw[1024], raw[1024]; int match = -1, nm = 0;
            g4_decode_raw(rom + g4rec, 2048, g4raw);
            for (k = 0; k < nc; k++) {
                const uint8_t *p = g1prog + 0x38000 + cands[k] * 0x2000 + (w - 0x38000);
                if (memcmp(p, rom + g4rec, 14)) continue;
                g4_decode_raw(p, 0x2000 - (w - 0x38000), raw);
                if (!memcmp(raw, g4raw, 1024)) { match = cands[k]; nm++; }
            }
            bank[i] = nm == 1 ? match : -1;
        }
        if (bank[i] < 0) { free(g4_mem); g4_mem = saved; free(rom); fprintf(stderr, "colours: Gauntlet maze %d: bank not found\n", i + 1); return 0; }
    }
    /* Gauntlet 4's two Arcade tables: record -> level (extract_levels.py; a later table wins) */
    for (k = 0; k < 2; k++) for (i = 1; i <= 128; i++) {
        uint32_t rec = g4_level_ptr(i, k); int j;
        if (i > G1_COUNT) continue;
        for (j = 0; j < nl && recs[j] != rec; j++) ;
        if (j == nl) { if (nl >= 256) continue; recs[nl++] = rec; }
        rec_level[j] = i;
    }
    free(g4_mem); g4_mem = saved; free(rom);
    for (i = 0; i < G1_COUNT; i++) maze_shades(windows[i], bank[i], sh[i]);
    for (i = 1; i < nl; i++) {                         /* sorted by record */
        uint32_t rr = recs[i]; int lv = rec_level[i], j = i - 1;
        while (j >= 0 && recs[j] > rr) { recs[j + 1] = recs[j]; rec_level[j + 1] = rec_level[j]; j--; }
        recs[j + 1] = rr; rec_level[j + 1] = lv;
    }
    if (!(f = fopen(colours_path, "w"))) return 0;
    fprintf(f, "# arcade Gauntlet floor/wall shades per Gauntlet 4 record: made by arcade_colours.py from the arcade ROMs\n");
    for (i = 0; i < nl; i++) {
        int (*c)[3][3] = sh[rec_level[i] - 1], g, s;
        fprintf(f, "%06x", recs[i]);
        for (g = 0; g < 2; g++) for (s = 0; s < 3; s++) fprintf(f, " %d %d %d", c[g][s][0], c[g][s][1], c[g][s][2]);
        fprintf(f, "\n");
    }
    fclose(f);
    return nl;
}
