/* g4mods.h -- optional extras outside the original game (see g4mods.c); off with --original */
#ifndef G4MODS_H
#define G4MODS_H
#include "../c/g4_m68k.h"
extern int mods_enabled;
void mods_init(void);                                       /* after the ROM is loaded */
int  mods_override(uint32_t addr, void (**fn)(M68K *));     /* from the dispatcher hook */
extern int mods_no_music;                                   /* --nomusic: sound effects and voices only */
extern int level_set_g2;                                    /* --level-set gauntlet2 */
int  mods_load_g2(const char *path);                        /* host/gauntlet2_levels.bin */
int  mods_load_g3(const char *path);                        /* gauntlet3_levels.bin (GAUNTLET III) */
uint32_t mods_g3_size(void);
void mods_use_g3(void);                                     /* --level-set gauntlet3 */
extern int level_set_g3;
uint32_t mods_g2_size(void);                                /* bytes loaded (0: none) */
void mods_after_vblank(long frame);                         /* Gauntlet II features: player effects */
int mods_mugger_box(int *x0, int *y0, const uint8_t **rgba);   /* the mugger's own picture (24x24 RGBA) on screen */
int mods_tint_boxes(int *box, int max);                     /* super sorcerers: screen boxes x0,y0,x1,y1 to tint */
uint32_t mods_overlay_px(int wx, int wy, long frame);       /* ... and drawing (0 or 0x1rrggbb) */
const char *mods_banner(long frame);
const uint32_t *mods_g2_attract_frame(int *w, int *h);    /* Gauntlet II attract picture to show instead, or NULL */
extern int mods_online;                                     /* online: skip the Gauntlet II attract screens */
int mods_logo_kind(void);                                   /* panel logo: 0 as is, 1 no number, 2 "II", 3 "III" */                        /* pickup message to show, or NULL */
int mods_lobby_lines(const char **out, int max);   /* the lobby box's lines (0 = none) */
extern int mods_menu_host_only;
extern char mods_base[1024];
#endif
