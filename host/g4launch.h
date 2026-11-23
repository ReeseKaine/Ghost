/* g4launch.h -- the launcher (see g4launch.c) */
#ifndef G4LAUNCH_H
#define G4LAUNCH_H
#define GHOST_VERSION "0.1.1"          /* shown in the launcher and window titles, and by --version */
#include <stdint.h>
#include <stddef.h>
typedef struct {
    char dir[1024];
    int g4_kind;                       /* 0 none, 1 September 1993, 2 August 1993, 3 other */
    uint32_t g4_crc;
    char g4_path[1024];
    char arcade_zip[31][1024];         /* per needed arcade file: the zip holding it ("" = missing) */
    int have_g1, have_g2;              /* every file the Gauntlet / Gauntlet II data needs is there */
    char atr_orig[1024], atr_dd[1024]; /* Atari 8-bit Gauntlet maze disks: its side 2, Deeper Dungeons ("" = none) */
    char tap_dd[1024];                 /* or the C64 Deeper Dungeons tape (side 2, raw .tap) */
    char crt_orig[1024];               /* or the C64 Gauntlet cartridge (.crt): the Atari's own mazes */
} launch_roms;
void launch_scan(const char *dir, launch_roms *r);
const char *launch_zip_for(const launch_roms *r, const char *name);   /* the zip holding a needed file */
int  launch_missing(const launch_roms *r, int set, const char **names, int max);
int  launch_scan_cli(const char *dir);
enum { LAUNCH_QUIT, LAUNCH_PLAY, LAUNCH_HOST, LAUNCH_JOIN };
typedef struct {
    int mode, vol_music, vol_sfx, vol_voice, players;
    int res, display, aspect;          /* video: index into launch_resolutions, 0 window 1 fullscreen 2 borderless,
                                          0 4:3 1 square pixels 2 stretch 3 sharp (whole-number scaling) */
    char rom_dir[1024], rom[1024], join[256], pack[128];   /* pack: graphics pack folder name, "" none */
} launch_choice;
extern const int launch_resolutions[][2]; extern const int launch_n_resolutions;
int  launch_run(const char *base, launch_choice *ch);   /* the launcher screen; 1 = start the game */
void launch_load_volumes(const char *base, int *music, int *sfx, int *voice);   /* from launcher.cfg, if any */
void launch_save_volumes(const char *base, int music, int sfx, int voice);
void launch_load_join(const char *base, char *out, size_t n);
void launch_save_join(const char *base, const char *addr);
#endif
