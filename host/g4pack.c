/* g4pack.c -- graphics packs (user, 2026-11-22): replacement images for the game's background
   tiles (walls, floors, items, the panel...), drawn at the pack's own resolution.

   A pack is a folder packs/<name>/ next to the game with
     pack.txt          lines "name = ...", "author = ...", "scale = N" (1-8: images are 8N x 8N)
     tile_XXXXXXXX.png  a background tile picture; XXXXXXXX = the tile's fingerprint
     sprite_XXXXXXXX.png a sprite tile (heroes, monsters, shots), the same kind of fingerprint
     g2pic_NN.png       one of the port's own Gauntlet II pictures (g2_sprites.bin id NN: dragon,
                        acid, chest, poison, force-field posts), its size x scale
     g2pic_NN_F.png     frame F (0, 1, 2...) of picture NN: with frames, the picture is animated --
                        the next frame every "frame_ticks" game frames (pack.txt, default 8)
     attract_XXXXXXXX.png  a still Gauntlet II attract screen (title, credits, scores...), its full
                        size x scale; XXXXXXXX = a fingerprint of the screen (CRC32 of its pixels)
   The game reuses its tile slots for different pictures, so a tile is known by its picture:
   the fingerprint is a CRC32 of the tile's 32 bytes (8x8, 4 bits a pixel) and its palette line.
   Anything a pack leaves out keeps the game's own picture. A replacement pixel with alpha < 128
   shows the game's own pixel there.

   --dump-tiles DIR (for pack makers): while playing, every background tile seen on screen is
   written once as DIR/tile_XXXXXXXX.png (8x8, the colours it is shown with, colour 0
   transparent): a pack's starting point with the right names. Those pictures come from the
   player's own ROM: they are for their own use, not for sharing.
   PNG reading/writing: stb_image / stb_image_write (host/stb, public domain or MIT). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
#include "g4pack.h"

int pack_scale;                                        /* 0 = no pack */
int pack_frame_ticks = 8;
static char pack_dir[1200], dump_dir[1200];
int pack_dumping;
typedef struct { uint32_t key; int kind, loaded, w, h; uint8_t *rgba; } entry_t;   /* kind: PACK_TILE / SPRITE / PIC */
static entry_t *entries; static int n_entries;
static uint32_t *dumped; static int n_dumped, cap_dumped;

static uint32_t crc_tab[256];
uint32_t pack_key(const uint8_t *pattern, int pal)
{
    uint32_t c = 0xFFFFFFFFu; int i;
    if (!crc_tab[1]) { uint32_t k, v; int b; for (k = 0; k < 256; k++) { v = k; for (b = 0; b < 8; b++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1; crc_tab[k] = v; } }
    for (i = 0; i < 32; i++) c = crc_tab[(c ^ pattern[i]) & 255] ^ (c >> 8);
    c = crc_tab[(c ^ (uint8_t)pal) & 255] ^ (c >> 8);
    return ~c;
}
static int cmp_entry(const void *a, const void *b)
{
    const entry_t *x = (const entry_t *)a, *y = (const entry_t *)b;
    if (x->kind != y->kind) return x->kind < y->kind ? -1 : 1;
    return x->key < y->key ? -1 : x->key > y->key;
}
static const char *KIND_FMT[4] = {"tile_%08x.png", "sprite_%08x.png", "g2pic_%02u.png", "attract_%08x.png"};

int pack_open(const char *base, const char *name)
{
    char path[1400], line[256]; FILE *f; DIR *d; struct dirent *de; int cap = 0;
    snprintf(pack_dir, sizeof pack_dir, "%spacks/%s", base, name);
    pack_scale = 1;
    snprintf(path, sizeof path, "%s/pack.txt", pack_dir);
    if ((f = fopen(path, "r"))) {
        while (fgets(line, sizeof line, f)) {
            int s;
            if (sscanf(line, " scale = %d", &s) == 1 && s >= 1 && s <= 8) pack_scale = s;
            if (sscanf(line, " frame_ticks = %d", &s) == 1 && s >= 1 && s <= 600) pack_frame_ticks = s;
        }
        fclose(f);
    }
    if (!(d = opendir(pack_dir))) { fprintf(stderr, "pack: %s not found\n", pack_dir); pack_scale = 0; return 0; }
    while ((de = readdir(d))) {
        unsigned key, fr; int kind; char back[64];
        if (sscanf(de->d_name, "tile_%8x.png", &key) == 1) kind = PACK_TILE;
        else if (sscanf(de->d_name, "sprite_%8x.png", &key) == 1) kind = PACK_SPRITE;
        else if (sscanf(de->d_name, "attract_%8x.png", &key) == 1) kind = PACK_ATTRACT;
        else if (sscanf(de->d_name, "g2pic_%u_%u.png", &key, &fr) == 2 && key < 0x10000 && fr < 255) {   /* a frame */
            snprintf(back, sizeof back, "g2pic_%02u_%u.png", key, fr);
            if (strcmp(back, de->d_name)) continue;
            key |= (fr + 1) << 16; kind = PACK_PIC;
            goto add;
        }
        else if (sscanf(de->d_name, "g2pic_%u.png", &key) == 1) kind = PACK_PIC;
        else continue;
        snprintf(back, sizeof back, KIND_FMT[kind], key);
        if (strcmp(back, de->d_name)) continue;           /* exactly the expected spelling */
    add:
        if (n_entries == cap) { cap = cap ? cap * 2 : 256; entries = (entry_t *)realloc(entries, sizeof *entries * (size_t)cap); }
        entries[n_entries].key = key; entries[n_entries].kind = kind; entries[n_entries].loaded = 0; entries[n_entries].rgba = NULL; n_entries++;
    }
    closedir(d);
    qsort(entries, (size_t)n_entries, sizeof *entries, cmp_entry);
    fprintf(stderr, "pack: %s, scale %d, %d pictures\n", name, pack_scale, n_entries);
    return 1;
}
/* a replacement picture: kind, key (fingerprint, or the Gauntlet II picture number), its size at
   scale 1 (8x8 for tiles and sprites) -> RGBA at the pack's scale, or NULL */
const uint8_t *pack_get(int kind, uint32_t key, int w, int h)
{
    int lo = 0, hi = n_entries - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2; entry_t *e = &entries[mid];
        int c = e->kind != kind ? (e->kind < kind ? -1 : 1) : (e->key < key ? -1 : e->key > key);
        if (!c) {
            if (!e->loaded) {                          /* the first time it is needed */
                char path[1400], fn[64]; int iw, ih, n; uint8_t *p;
                e->loaded = 1;
                if (kind == PACK_PIC && (key >> 16)) snprintf(fn, sizeof fn, "g2pic_%02u_%u.png", key & 0xFFFF, (key >> 16) - 1);
                else snprintf(fn, sizeof fn, KIND_FMT[kind], key);
                snprintf(path, sizeof path, "%s/%s", pack_dir, fn);
                p = stbi_load(path, &iw, &ih, &n, 4);
                if (p && iw == w * pack_scale && ih == h * pack_scale) e->rgba = p;
                else if (p) { stbi_image_free(p); fprintf(stderr, "pack: %s is %dx%d, not %dx%d -- skipped\n", path, iw, ih, w * pack_scale, h * pack_scale); }
            }
            return e->rgba;
        }
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}
const uint8_t *pack_tile(uint32_t key) { return pack_get(PACK_TILE, key, 8, 8); }
static int has_entry(int kind, uint32_t key)
{
    int lo = 0, hi = n_entries - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2; entry_t *e = &entries[mid];
        int c = e->kind != kind ? (e->kind < kind ? -1 : 1) : (e->key < key ? -1 : e->key > key);
        if (!c) return 1;
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}
const uint8_t *pack_pic_anim(uint32_t id, int w, int h, long frame)   /* frames 0, 1, 2... if the pack has them */
{
    static uint32_t ids[64]; static int counts[64], n_ids;
    int k, n = -1;
    for (k = 0; k < n_ids; k++) if (ids[k] == id) { n = counts[k]; break; }
    if (n < 0) {                                         /* count them once */
        n = 0; while (n < 255 && has_entry(PACK_PIC, id | (uint32_t)(n + 1) << 16)) n++;
        if (n_ids < 64) { ids[n_ids] = id; counts[n_ids] = n; n_ids++; }
    }
    if (n > 0) {
        const uint8_t *p = pack_get(PACK_PIC, id | (uint32_t)((frame / pack_frame_ticks) % n + 1) << 16, w, h);
        if (p) return p;
    }
    return pack_get(PACK_PIC, id, w, h);
}
void pack_dump_open(const char *dir) { snprintf(dump_dir, sizeof dump_dir, "%s", dir); pack_dumping = 1; }
static int dumped_has(uint32_t k)                     /* remember (sorted) what was written */
{
    int lo = 0, hi = n_dumped - 1;
    while (lo <= hi) { int mid = (lo + hi) / 2; if (dumped[mid] == k) return 1; if (dumped[mid] < k) lo = mid + 1; else hi = mid - 1; }
    if (n_dumped == cap_dumped) { cap_dumped = cap_dumped ? cap_dumped * 2 : 1024; dumped = (uint32_t *)realloc(dumped, sizeof *dumped * (size_t)cap_dumped); }
    memmove(dumped + lo + 1, dumped + lo, sizeof *dumped * (size_t)(n_dumped - lo)); dumped[lo] = k; n_dumped++;
    return 0;
}
void pack_dump_kind(int kind, uint32_t key, const uint8_t *pattern, const uint32_t pal_rgb[16])
{
    int i; uint8_t px[8 * 8 * 4]; char path[1400], fn[64];
    if (dumped_has(key ^ (uint32_t)kind * 0x9E3779B9u)) return;
    for (i = 0; i < 64; i++) {
        int v = (pattern[i >> 1] >> ((i & 1) ? 0 : 4)) & 15; uint32_t c = pal_rgb[v];
        px[4 * i] = (uint8_t)(c >> 16); px[4 * i + 1] = (uint8_t)(c >> 8); px[4 * i + 2] = (uint8_t)c; px[4 * i + 3] = v ? 255 : 0;
    }
    snprintf(fn, sizeof fn, KIND_FMT[kind], key);
    snprintf(path, sizeof path, "%s/%s", dump_dir, fn);
    stbi_write_png(path, 8, 8, 4, px, 8 * 4);
}
void pack_dump(uint32_t key, const uint8_t *pattern, const uint32_t pal_rgb[16]) { pack_dump_kind(PACK_TILE, key, pattern, pal_rgb); }
void pack_dump_pic(int id, int w, int h, const uint8_t *rgba)   /* a Gauntlet II picture, whole */
{
    char path[1400], fn[64];
    if (dumped_has((uint32_t)id ^ 2u * 0x9E3779B9u)) return;
    snprintf(fn, sizeof fn, KIND_FMT[PACK_PIC], (unsigned)id);
    snprintf(path, sizeof path, "%s/%s", dump_dir, fn);
    stbi_write_png(path, w, h, 4, rgba, w * 4);
}
void pack_dump_attract(uint32_t key, int w, int h, const uint32_t *xrgb)   /* a still attract screen, whole */
{
    char path[1400], fn[64]; uint8_t *px; int i;
    if (dumped_has(key ^ 3u * 0x9E3779B9u)) return;
    px = (uint8_t *)malloc((size_t)w * h * 4);
    if (!px) return;
    for (i = 0; i < w * h; i++) { px[4 * i] = (uint8_t)(xrgb[i] >> 16); px[4 * i + 1] = (uint8_t)(xrgb[i] >> 8); px[4 * i + 2] = (uint8_t)xrgb[i]; px[4 * i + 3] = 255; }
    snprintf(fn, sizeof fn, KIND_FMT[PACK_ATTRACT], key);
    snprintf(path, sizeof path, "%s/%s", dump_dir, fn);
    stbi_write_png(path, w, h, 4, px, w * 4);
    free(px);
}
void pack_dump_shape(int id, int w, int h, const uint8_t *rgba)   /* a drawn shape, as seen so far (rewritten as it fills in) */
{
    char path[1400], fn[64];
    snprintf(fn, sizeof fn, KIND_FMT[PACK_PIC], (unsigned)id);
    snprintf(path, sizeof path, "%s/%s", dump_dir, fn);
    stbi_write_png(path, w, h, 4, rgba, w * 4);
}
