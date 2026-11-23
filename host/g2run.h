/* g2run.h -- running the arcade Gauntlet II program on Musashi (see g2run.c) */
#ifndef G2RUN_H
#define G2RUN_H
#include <stdint.h>
#include "g4launch.h"
typedef struct { int index, bank, calls, length; uint32_t window; uint8_t header[11]; uint8_t map[1024]; } g2_maze;
int  g2_load(const launch_roms *r);                    /* the program from the zips; 1 = ok */
int  g2_load_image(const uint8_t *img);                /* any 0x80000-byte program image */
void g2_reset(int bank);                               /* a fresh machine, that slapstic bank in the window */
int  g2_call(uint32_t entry, const uint32_t *args, int nargs, long max_instr);   /* 1 = it returned */
void g2_hook(uint32_t addr, void (*fn)(void));
int  g2_run(uint32_t pc, uint32_t until, long max_instr);   /* registers kept; 1 = reached until */
void g2_set_bank(int k);
uint8_t *g2_mem_w(void);
void g2_unhook_all(void);
uint32_t g2_rd32(uint32_t a); unsigned g2_rd16(uint32_t a); void g2_wr32(uint32_t a, uint32_t v); void g2_wr16(uint32_t a, unsigned v);
uint32_t g2_reg(int r); void g2_set_reg(int r, uint32_t v);
const uint8_t *g2_mem(void); const uint8_t *g2_prog(void);
int  data_g2_mazes(const launch_roms *r, g2_maze **out);   /* import_gauntlet2.py: count, or 0 */
int  data_g2_mazes_json(const launch_roms *r, const char *path);
int  data_g2_sprites(const launch_roms *r, const char *out_path);              /* g2_sprites.bin; 1 = done */
int  data_g2_levels(const launch_roms *r, const char *out_path);   /* gauntlet2_levels.bin (needs the G4 ROM too); 1 = done */
int  data_g3_levels(const launch_roms *r, const char *out_path);   /* gauntlet3_levels.bin (Atari disks + G4 ROM); 1 = done */
int  crt_orig_kind(const char *path);                               /* 1 = a C64 Gauntlet cartridge holding its mazes */
int  tap_dd_kind(const char *path);                                 /* 1 = the C64 Deeper Dungeons maze tape */
int  atr_side2_kind(const char *path);                              /* 1 Atari Gauntlet side 2, 2 Deeper Dungeons, 0 neither */
int  data_g2_attract(const launch_roms *r, const char *out_path, void (*progress)(int frame));   /* g2_attract.bin: frames, 0 = failed */
int  data_g2_dragons_check(const launch_roms *r);
int  data_g1_colours(const launch_roms *r, const char *colours_path);   /* the Gauntlet 4 part of arcade_colours.txt (writes the file anew) */
int  data_g2_colours(const launch_roms *r, const char *levels_path, const char *colours_path);   /* lines written, 0 = failed */
#endif
