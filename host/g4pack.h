/* g4pack.h -- graphics packs (see g4pack.c) */
#ifndef G4PACK_H
#define G4PACK_H
#include <stdint.h>
extern int pack_scale, pack_dumping;
int  pack_open(const char *base, const char *name);   /* 1 = loaded (pack_scale set) */
uint32_t pack_key(const uint8_t *pattern, int pal);    /* a tile's fingerprint */
enum { PACK_TILE, PACK_SPRITE, PACK_PIC, PACK_ATTRACT };
extern int pack_frame_ticks;                            /* pack.txt "frame_ticks": frames per animation step (8) */
const uint8_t *pack_pic_anim(uint32_t id, int w, int h, long frame);   /* g2pic_NN[_F].png: animated if frames exist */
void pack_dump_attract(uint32_t key, int w, int h, const uint32_t *xrgb);   /* a still Gauntlet II attract screen */
const uint8_t *pack_get(int kind, uint32_t key, int w, int h);   /* RGBA at the pack's scale, or NULL */
const uint8_t *pack_tile(uint32_t key);                /* its replacement (RGBA, 8*scale square) or NULL */
void pack_dump_kind(int kind, uint32_t key, const uint8_t *pattern, const uint32_t pal_rgb[16]);
void pack_dump_pic(int id, int w, int h, const uint8_t *rgba);
void pack_dump_shape(int id, int w, int h, const uint8_t *rgba);
void pack_dump_open(const char *dir);
void pack_dump(uint32_t key, const uint8_t *pattern, const uint32_t pal_rgb[16]);
#endif
