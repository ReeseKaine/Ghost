/* g4launch.c -- the launcher's ROM finder (user, 2026-11-22; launcher step 1).

   The player points the launcher at a folder; every file in it is checked:
     - the Gauntlet 4 Mega Drive ROM, recognised by the CRC32 of the whole file (September 1993 is
       the one the port is built on; August 1993 is refused; anything else with a Mega Drive header
       naming GAUNTLET is offered with a warning, as rom_check in g4host.c does);
     - arcade ROM sets as MAME zips, recognised by the names and CRC32s of their members, read
       from the zip's own directory (nothing is unpacked). A file Gauntlet II needs may sit in
       gauntlet2.zip or, in a "split" MAME set, in its parent gauntlet.zip: each needed file is
       looked for in every zip found.
   Only file names and checksums are listed here (as MAME's own lists do) -- nothing from the
   games themselves. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include "g4launch.h"
#include "g4data.h"
#include "g2run.h"

static uint32_t crc_table[256];
static uint32_t crc32_buf(uint32_t crc, const uint8_t *p, size_t n)
{
    if (!crc_table[1]) {
        uint32_t k, c; int b;
        for (k = 0; k < 256; k++) { c = k; for (b = 0; b < 8; b++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; crc_table[k] = c; }
    }
    crc = ~crc;
    while (n--) crc = crc_table[(crc ^ *p++) & 255] ^ (crc >> 8);
    return ~crc;
}

/* the arcade files the data steps read: name, CRC32, which set needs it
   (1 Gauntlet = its colours for Gauntlet 4's levels, 2 Gauntlet II = everything else, 3 both) */
static const struct { const char *name; uint32_t crc; int need; } arcade_files[] = {
    {"136037-112.1b",   0x869330be, 3}, {"136037-114.1mn",  0x29ef9882, 3},
    {"136037-116.2b",   0x11e0ac5b, 3}, {"136037-118.2mn",  0x8bf3b263, 3},
    {"136037-1307.9a",  0x46fe8743, 3}, {"136037-1308.9b",  0x276e15c4, 3},
    {"136037-1409.7a",  0x6fb8419c, 1}, {"136037-1410.7b",  0x931bd2a0, 1},
    {"136037-205.10a",  0x6d99ed51, 1}, {"136037-206.10b",  0x545ead91, 1},
    {"136037-111.1a",   0x91700f33, 1}, {"136037-113.1l",   0xd497d0a8, 1},   /* Gauntlet's own graphics (its colours) */
    {"136037-115.2a",   0x9510b898, 1}, {"136037-117.2l",   0x29a5db41, 1},
    {"136043-1104.6p",  0xbddc3dfc, 2}, {"136043-1105.10a", 0x45dfda47, 2},
    {"136043-1106.10b", 0x343c029c, 2}, {"136043-1109.7a",  0x58a0a9a3, 2},
    {"136043-1110.7b",  0x658f0da8, 2}, {"136043-1111.1a",  0x09df6e23, 2},
    {"136043-1113.1l",  0x33cb476e, 2}, {"136043-1115.2a",  0xf71e2503, 2},
    {"136043-1117.2l",  0x9e30b2e9, 2}, {"136043-1119.16s", 0xdc3591e7, 2},
    {"136043-1120.16r", 0x5c731006, 2}, {"136043-1121.6a",  0xae301bba, 2},
    {"136043-1122.6b",  0xe94aaa8a, 2}, {"136043-1123.1c",  0xe4c98f01, 2},
    {"136043-1124.1p",  0xc4857879, 2}, {"136043-1125.2c",  0xd9c2c2d1, 2},
    {"136043-1126.2p",  0xa32c732a, 2},
};
#define N_ARCADE (int)(sizeof arcade_files / sizeof arcade_files[0])

static unsigned le16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }

/* a zip's directory: for each member call back with its name, CRC32 and size. 0 = not a zip */
static int zip_members(const char *path, void (*cb)(const char *zip, const char *name, uint32_t crc, uint32_t size, void *ud), void *ud)
{
    FILE *f = fopen(path, "rb"); long size, start; uint8_t *buf, *e = NULL; uint32_t cd_off, cd_size; unsigned n, k;
    if (!f) return 0;
    fseek(f, 0, SEEK_END); size = ftell(f);
    start = size > 65557 ? size - 65557 : 0;              /* the end record: within the last 64 KB + 22 */
    buf = (uint8_t *)malloc((size_t)(size - start));
    if (!buf) { fclose(f); return 0; }
    fseek(f, start, SEEK_SET);
    if (fread(buf, 1, (size_t)(size - start), f) != (size_t)(size - start)) { free(buf); fclose(f); return 0; }
    for (k = (unsigned)(size - start) >= 22 ? (unsigned)(size - start) - 22 + 1 : 0; k-- > 0; )
        if (le32(buf + k) == 0x06054b50) { e = buf + k; break; }
    if (!e) { free(buf); fclose(f); return 0; }
    n = le16(e + 10); cd_size = le32(e + 12); cd_off = le32(e + 16);
    free(buf);
    if ((long)cd_off + (long)cd_size > size) { fclose(f); return 0; }
    buf = (uint8_t *)malloc(cd_size ? cd_size : 1);
    fseek(f, (long)cd_off, SEEK_SET);
    if (!buf || fread(buf, 1, cd_size, f) != cd_size) { free(buf); fclose(f); return 0; }
    fclose(f);
    {   uint32_t o = 0;
        for (k = 0; k < n && o + 46 <= cd_size && le32(buf + o) == 0x02014b50; k++) {
            unsigned nl = le16(buf + o + 28), xl = le16(buf + o + 30), cl = le16(buf + o + 32);
            char name[256]; const char *slash;
            if (o + 46 + nl > cd_size) break;
            snprintf(name, sizeof name, "%.*s", (int)nl, (const char *)buf + o + 46);
            slash = strrchr(name, '/');                    /* members in a sub-folder count by their name */
            cb(path, slash ? slash + 1 : name, le32(buf + o + 16), le32(buf + o + 24), ud);
            o += 46 + nl + xl + cl;
        }
    }
    free(buf);
    return 1;
}

static void member_cb(const char *zip, const char *name, uint32_t crc, uint32_t size, void *ud)
{
    launch_roms *r = (launch_roms *)ud; int i;
    (void)size;
    for (i = 0; i < N_ARCADE; i++)
        if (!strcmp(name, arcade_files[i].name) && crc == arcade_files[i].crc && !r->arcade_zip[i][0])
            snprintf(r->arcade_zip[i], sizeof r->arcade_zip[i], "%s", zip);
}

/* the Gauntlet 4 ROM: 0 not it, 1 September 1993, 2 August 1993, 3 another Gauntlet 4 dump */
static int g4_kind(const char *path, uint32_t *crc_out)
{
    FILE *f = fopen(path, "rb"); long size; uint8_t *buf; uint32_t crc; int kind = 0;
    if (!f) return 0;
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size < 0x80000 || size > 0x400000) { fclose(f); return 0; }
    buf = (uint8_t *)malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) { free(buf); fclose(f); return 0; }
    fclose(f);
    crc = crc32_buf(0, buf, (size_t)size);
    if (crc == 0xf9d60510) kind = 1;
    else if (crc == 0x3bf46dce) kind = 2;
    else if (!memcmp(buf + 0x100, "SEGA", 4)) {           /* a Mega Drive header naming the game */
        int k; for (k = 0x120; k < 0x1A0 - 8; k++) if (!memcmp(buf + k, "GAUNTLET", 8)) { kind = 3; break; }
    }
    free(buf);
    *crc_out = crc;
    return kind;
}

void launch_scan(const char *dir, launch_roms *r)
{
    DIR *d = opendir(dir); struct dirent *de; int i;
    memset(r, 0, sizeof *r);
    snprintf(r->dir, sizeof r->dir, "%s", dir);
    if (!d) return;
    while ((de = readdir(d))) {
        char path[2048]; const char *n = de->d_name; size_t l = strlen(n); uint32_t crc = 0; int kind;
        if (n[0] == '.') continue;
        snprintf(path, sizeof path, "%s/%s", dir, n);
        if (l > 4 && (!strcmp(n + l - 4, ".zip") || !strcmp(n + l - 4, ".ZIP"))) { zip_members(path, member_cb, r); continue; }
        if (l > 4 && (!strcmp(n + l - 4, ".atr") || !strcmp(n + l - 4, ".ATR"))) {   /* Atari Gauntlet maze disks */
            int a = atr_side2_kind(path);
            if (a == 1 && !r->atr_orig[0]) snprintf(r->atr_orig, sizeof r->atr_orig, "%.1000s", path);
            if (a == 2 && !r->atr_dd[0]) snprintf(r->atr_dd, sizeof r->atr_dd, "%.1000s", path);
            continue;
        }
        if (l > 4 && (!strcmp(n + l - 4, ".crt") || !strcmp(n + l - 4, ".CRT"))) {   /* C64 Gauntlet cartridge */
            if (!r->crt_orig[0] && crt_orig_kind(path)) snprintf(r->crt_orig, sizeof r->crt_orig, "%.1000s", path);
            continue;
        }
        if (l > 4 && (!strcmp(n + l - 4, ".tap") || !strcmp(n + l - 4, ".TAP"))) {   /* C64 Deeper Dungeons tape */
            if (!r->tap_dd[0] && tap_dd_kind(path)) snprintf(r->tap_dd, sizeof r->tap_dd, "%.1000s", path);
            continue;
        }
        kind = g4_kind(path, &crc);
        if (kind && (!r->g4_kind || (kind == 1 && r->g4_kind != 1))) {   /* the September one wins */
            r->g4_kind = kind; r->g4_crc = crc; snprintf(r->g4_path, sizeof r->g4_path, "%.1000s", path);
        }
    }
    closedir(d);
    r->have_g1 = r->have_g2 = 1;
    for (i = 0; i < N_ARCADE; i++) {
        if (!r->arcade_zip[i][0]) {
            if (arcade_files[i].need & 1) r->have_g1 = 0;
            if (arcade_files[i].need & 2) r->have_g2 = 0;
        }
    }
}

const char *launch_zip_for(const launch_roms *r, const char *name)   /* the zip holding that file ("" none) */
{
    int i;
    for (i = 0; i < N_ARCADE; i++) if (!strcmp(arcade_files[i].name, name)) return r->arcade_zip[i];
    return "";
}
int launch_missing(const launch_roms *r, int set, const char **names, int max)   /* set 1 Gauntlet, 2 Gauntlet II */
{
    int i, n = 0;
    for (i = 0; i < N_ARCADE; i++)
        if ((arcade_files[i].need & set) && !r->arcade_zip[i][0]) { if (n < max) names[n] = arcade_files[i].name; n++; }
    return n;
}

/* --scan DIR (test): what the launcher would find there */
int launch_scan_cli(const char *dir)
{
    launch_roms r; const char *miss[32]; int n, k;
    static const char *kinds[] = {"not found", "found (September 1993)", "found, but the August 1993 version (not supported)",
                                  "found, an unknown version (may not work)"};
    launch_scan(dir, &r);
    printf("folder: %s\n", dir);
    printf("Gauntlet 4 (Mega Drive): %s%s%s\n", kinds[r.g4_kind], r.g4_kind ? "  " : "", r.g4_kind ? r.g4_path : "");
    n = launch_missing(&r, 1, miss, 32);
    printf("Gauntlet (arcade): %s\n", n ? "incomplete or not found" : "found");
    for (k = 0; k < n && k < 32; k++) printf("    missing %s\n", miss[k]);
    n = launch_missing(&r, 2, miss, 32);
    printf("Gauntlet II (arcade): %s\n", n ? "incomplete or not found" : "found");
    for (k = 0; k < n && k < 32; k++) printf("    missing %s\n", miss[k]);
    printf("Atari Gauntlet side 2: %s\n", r.atr_orig[0] ? r.atr_orig : "not found");
    printf("Atari Deeper Dungeons: %s\n", r.atr_dd[0] ? r.atr_dd : "not found");
    printf("C64 Deeper Dungeons tape: %s\n", r.tap_dd[0] ? r.tap_dd : "not found");
    printf("C64 Gauntlet cartridge: %s\n", r.crt_orig[0] ? r.crt_orig : "not found");
    return 0;
}

/* ------------------------------------------------------------------ the launcher screen (step 2)
   ghost with no ROM argument opens this first. It remembers its settings in launcher.cfg next to
   the program; finds the ROMs in the chosen folder (launch_scan); shows what they make available;
   and starts the game, alone or online, by handing main() the same settings the .bat files pass.
   Text: SDL3's built-in 8x8 debug font, at a 320x240 logical size. Keys: up/down choose, left/right
   change, Enter / A start, Esc / B quits. Test aids: G4LAUNCHKEYS=down,right,enter (scripted keys,
   one per frame), G4LAUNCHSHOT=file.ppm (save the screen after the keys and quit). */
static void cfg_path(char *out, size_t n, const char *base) { snprintf(out, n, "%slauncher.cfg", base); }
/* video choices (the game's --resolution / --display / --aspect / --pack) */
const int launch_resolutions[][2] = {{640, 480}, {800, 600}, {960, 720}, {1024, 768}, {1280, 720}, {1280, 960}, {1366, 768},
                                     {1600, 900}, {1600, 1200}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
const int launch_n_resolutions = (int)(sizeof launch_resolutions / sizeof launch_resolutions[0]);
static const char *DISPLAY_NAMES[3] = {"window", "fullscreen", "borderless"};
static const char *ASPECT_KEYS[4] = {"4:3", "square", "stretch", "sharp"};
static void cfg_load(const char *base, launch_choice *ch)
{
    char path[1200], line[1200]; FILE *f;
    cfg_path(path, sizeof path, base);
    if (!(f = fopen(path, "r"))) return;
    while (fgets(line, sizeof line, f)) {
        char *v = strchr(line, '='), *e;
        if (!v) continue;
        *v++ = 0; if ((e = strpbrk(v, "\r\n"))) *e = 0;
        if (!strcmp(line, "rom_dir")) snprintf(ch->rom_dir, sizeof ch->rom_dir, "%s", v);
        else if (!strcmp(line, "music_volume")) ch->vol_music = atoi(v);
        else if (!strcmp(line, "sfx_volume")) ch->vol_sfx = atoi(v);
        else if (!strcmp(line, "voice_volume")) ch->vol_voice = atoi(v);
        else if (!strcmp(line, "resolution")) { int w = 0, h = 0, k; if (sscanf(v, "%dx%d", &w, &h) == 2) for (k = 0; k < launch_n_resolutions; k++) if (launch_resolutions[k][0] == w && launch_resolutions[k][1] == h) ch->res = k; }
        else if (!strcmp(line, "display")) ch->display = !strcmp(v, "fullscreen") ? 1 : !strcmp(v, "borderless") ? 2 : 0;
        else if (!strcmp(line, "aspect")) ch->aspect = !strcmp(v, "square") ? 1 : !strcmp(v, "stretch") ? 2 : !strcmp(v, "sharp") ? 3 : 0;
        else if (!strcmp(line, "pack")) snprintf(ch->pack, sizeof ch->pack, "%s", v);
        else if (!strcmp(line, "players")) { ch->players = atoi(v); if (ch->players < 2 || ch->players > 4) ch->players = 2; }
        else if (!strcmp(line, "join")) snprintf(ch->join, sizeof ch->join, "%s", v);
    }
    fclose(f);
}
static void cfg_save(const char *base, const launch_choice *ch)
{
    char path[1200]; FILE *f;
    cfg_path(path, sizeof path, base);
    if (!(f = fopen(path, "w"))) return;
    fprintf(f, "rom_dir=%s\nmusic_volume=%d\nsfx_volume=%d\nvoice_volume=%d\nplayers=%d\njoin=%s\n", ch->rom_dir,
            ch->vol_music, ch->vol_sfx, ch->vol_voice, ch->players, ch->join);
    fprintf(f, "resolution=%dx%d\ndisplay=%s\naspect=%s\npack=%s\n", launch_resolutions[ch->res][0], launch_resolutions[ch->res][1],
            DISPLAY_NAMES[ch->display], ASPECT_KEYS[ch->aspect], ch->pack);
    fclose(f);
}
static void cfg_defaults(launch_choice *ch, const char *base)
{
    memset(ch, 0, sizeof *ch);
    ch->vol_music = 7; ch->vol_sfx = 10; ch->vol_voice = 10; ch->players = 2;
    ch->res = 5;                                       /* 1280x960 */
    snprintf(ch->rom_dir, sizeof ch->rom_dir, "%s", base[0] ? base : ".");
}
static int clamp10(int v) { return v < 0 ? 0 : v > 10 ? 10 : v; }
void launch_load_volumes(const char *base, int *music, int *sfx, int *voice)
{
    launch_choice ch; char path[1200]; FILE *f;
    cfg_path(path, sizeof path, base);
    if (!(f = fopen(path, "r"))) return;                 /* no launcher.cfg: keep the defaults */
    fclose(f);
    cfg_defaults(&ch, base); cfg_load(base, &ch);
    *music = clamp10(ch.vol_music); *sfx = clamp10(ch.vol_sfx); *voice = clamp10(ch.vol_voice);
}
void launch_load_join(const char *base, char *out, size_t n)   /* the last address joined */
{
    launch_choice ch; cfg_defaults(&ch, base); cfg_load(base, &ch); snprintf(out, n, "%s", ch.join);
}
void launch_save_join(const char *base, const char *addr)
{
    launch_choice ch; cfg_defaults(&ch, base); cfg_load(base, &ch); snprintf(ch.join, sizeof ch.join, "%s", addr); cfg_save(base, &ch);
}
void launch_save_volumes(const char *base, int music, int sfx, int voice)
{
    launch_choice ch;
    cfg_defaults(&ch, base); cfg_load(base, &ch);
    ch.vol_music = music; ch.vol_sfx = sfx; ch.vol_voice = voice;
    cfg_save(base, &ch);
}
static int have_file(const char *base, const char *name)
{
    char path[1200]; FILE *f;
    snprintf(path, sizeof path, "%s%s", base, name);
    if (!(f = fopen(path, "rb"))) return 0;
    fclose(f); return 1;
}

#ifdef G4_SDL
#include <SDL3/SDL.h>
static volatile int folder_picked; static char folder_new[1024];
static void SDLCALL folder_cb(void *ud, const char *const *list, int filter)
{
    (void)ud; (void)filter;
    if (list && list[0]) { snprintf(folder_new, sizeof folder_new, "%s", list[0]); folder_picked = 1; }
}
static void text_at(SDL_Renderer *r, int col, int row, const char *s, Uint8 cr, Uint8 cg, Uint8 cb)
{
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_RenderDebugText(r, (float)(col * 8), (float)(row * 8), s);
}
static void tail(char *out, size_t n, const char *s, int width)   /* a long path: its end, "..." first */
{
    int l = (int)strlen(s);
    if (l <= width) snprintf(out, n, "%s", s); else snprintf(out, n, "...%s", s + l - (width - 3));
}
enum { M_PLAY, M_HOST, M_JOIN, M_VIDEO, M_AUDIO, M_FOLDER, M_BUILD, M_QUIT,
       M_RES, M_DISPLAY, M_ASPECT, M_PACK, M_MUSIC, M_SFX, M_VOICE, M_BACK, M_COUNT };
/* pages: 0 main, 1 Video, 2 Audio -- each a list of the items above, ending with -1 */
static const int PAGE_ITEMS[3][10] = {
    {M_PLAY, M_VIDEO, M_AUDIO, M_FOLDER, M_BUILD, M_QUIT, -1},   /* (online play is in the game's own menus) */
    {M_RES, M_DISPLAY, M_ASPECT, M_PACK, M_BACK, -1},
    {M_MUSIC, M_SFX, M_VOICE, M_BACK, -1}};
static const char *ITEM_NAMES[M_COUNT] = {"Play", "Host an online game", "Join an online game", "Video", "Audio", "ROM folder",
    "Build the data files", "Quit", "Resolution", "Display", "Aspect ratio", "Graphics pack", "Music volume", "Effects volume",
    "Voice volume", "Back"};
static int page_count(int page) { int n = 0; while (PAGE_ITEMS[page][n] >= 0) n++; return n; }
/* graphics packs: the folders inside "packs" next to the game */
static char pack_names[32][128]; static int n_packs;
static void scan_packs(const char *base)
{
    char dir[1200]; DIR *d; struct dirent *de;
    n_packs = 0;
    snprintf(dir, sizeof dir, "%spacks", base);
    if (!(d = opendir(dir))) return;
    while ((de = readdir(d)) && n_packs < 32) {
        char p[1400]; DIR *sub;
        if (de->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "%s/%s", dir, de->d_name);
        if ((sub = opendir(p))) { closedir(sub); snprintf(pack_names[n_packs++], sizeof pack_names[0], "%s", de->d_name); }
    }
    closedir(d);
}
/* The music is recorded by a second copy of the program running in the background
   (ghost ROM --frames 0 --extract-music g4_music.bin --progress FILE; about 5 minutes):
   the launcher stays usable, shows the track from the progress file, and Esc stops it. */
static SDL_Process *rec_proc; static char rec_prog[1200]; static int rec_made_speech;
static int rec_start(const char *base, const char *rom)
{
    static char exe[1200], out[1200]; const char *args[10]; SDL_PropertiesID pr; int n = 0;
#ifdef _WIN32
    snprintf(exe, sizeof exe, "%sghost.exe", base);
#else
    snprintf(exe, sizeof exe, "%sghost", base[0] ? base : "./");
#endif
    snprintf(out, sizeof out, "%sg4_music.bin", base);
    snprintf(rec_prog, sizeof rec_prog, "%sbuild_progress.txt", base);
    remove(rec_prog);
    args[n++] = exe; args[n++] = rom; args[n++] = "--frames"; args[n++] = "0"; args[n++] = "--extract-music"; args[n++] = out;
    args[n++] = "--progress"; args[n++] = rec_prog; args[n] = NULL;
    pr = SDL_CreateProperties();
    SDL_SetPointerProperty(pr, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetBooleanProperty(pr, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);   /* no console window */
    rec_proc = SDL_CreateProcessWithProperties(pr);
    SDL_DestroyProperties(pr);
    return rec_proc != NULL;
}
static void rec_poll(char *msg, size_t n)              /* while recording: progress into msg */
{
    char line[64] = ""; FILE *f = fopen(rec_prog, "r"); int a = 0, b = 0, code = 0, ended;
    if (f) { if (!fgets(line, sizeof line, f)) line[0] = 0; fclose(f); }
    ended = SDL_WaitProcess(rec_proc, false, &code);
    if (sscanf(line, "done %d", &a) == 1) {
        snprintf(msg, n, "Data built: %smusic (%d tracks).", rec_made_speech ? "arcade parts, " : "", a);
        SDL_DestroyProcess(rec_proc); rec_proc = NULL; remove(rec_prog);
    } else if (!strncmp(line, "fail", 4) || ended) {
        snprintf(msg, n, "Music recording FAILED.");
        SDL_DestroyProcess(rec_proc); rec_proc = NULL; remove(rec_prog);
    } else if (sscanf(line, "track %d %d", &a, &b) == 2) snprintf(msg, n, "Recording music %d/%d (Esc stops)", a, b);
    else snprintf(msg, n, "Recording music... (Esc stops)");
}
int launch_run(const char *base, launch_choice *ch)
{
    SDL_Window *win; SDL_Renderer *ren; launch_roms roms; int sel = 0, done = 0, editing = 0, rescan = 1, frame = 0, page = 0, item;
    const char *keys = getenv("G4LAUNCHKEYS"), *shot = getenv("G4LAUNCHSHOT");
    char msg[128] = "";
    cfg_defaults(ch, base);
    cfg_load(base, ch);
    ch->vol_music = clamp10(ch->vol_music); ch->vol_sfx = clamp10(ch->vol_sfx); ch->vol_voice = clamp10(ch->vol_voice);
    scan_packs(base);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { fprintf(stderr, "launcher: %s\n", SDL_GetError()); return 0; }
    win = SDL_CreateWindow("Ghost " GHOST_VERSION " - launcher", 960, 720, SDL_WINDOW_RESIZABLE | (getenv("G4HIDDEN") ? SDL_WINDOW_HIDDEN : 0));
    ren = win ? SDL_CreateRenderer(win, NULL) : NULL;
    if (!ren) { fprintf(stderr, "launcher: %s\n", SDL_GetError()); SDL_Quit(); return 0; }
    SDL_SetRenderLogicalPresentation(ren, 320, 240, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    {   int n = 0; SDL_JoystickID *ids = SDL_GetGamepads(&n); int k;
        for (k = 0; k < n; k++) SDL_OpenGamepad(ids[k]);
        SDL_free(ids); }
    while (!done) {
        SDL_Event e; int key = 0;
        if (rescan) { launch_scan(ch->rom_dir, &roms); rescan = 0; }
        if (folder_picked) { folder_picked = 0; snprintf(ch->rom_dir, sizeof ch->rom_dir, "%s", folder_new); rescan = 1; cfg_save(base, ch); continue; }
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) { ch->mode = LAUNCH_QUIT; done = 1; if (rec_proc) { SDL_KillProcess(rec_proc, true); SDL_DestroyProcess(rec_proc); rec_proc = NULL; remove(rec_prog); } }
            else if (e.type == SDL_EVENT_KEY_DOWN) key = e.key.key;
            else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                int b = e.gbutton.button;
                key = b == SDL_GAMEPAD_BUTTON_DPAD_UP ? SDLK_UP : b == SDL_GAMEPAD_BUTTON_DPAD_DOWN ? SDLK_DOWN :
                      b == SDL_GAMEPAD_BUTTON_DPAD_LEFT ? SDLK_LEFT : b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT ? SDLK_RIGHT :
                      b == SDL_GAMEPAD_BUTTON_SOUTH || b == SDL_GAMEPAD_BUTTON_START ? SDLK_RETURN :
                      b == SDL_GAMEPAD_BUTTON_EAST ? SDLK_ESCAPE : 0;
            }
            else if (e.type == SDL_EVENT_TEXT_INPUT && editing) {
                size_t l = strlen(ch->join);
                if (l + strlen(e.text.text) < sizeof ch->join - 1) strcat(ch->join, e.text.text);
            }
        }
        if (keys && *keys) {                               /* test aid: one scripted key per frame */
            char w[16]; int l = 0;
            while (*keys && *keys != ',' && l < 15) w[l++] = *keys++;
            w[l] = 0; if (*keys == ',') keys++;
            key = !strcmp(w, "up") ? SDLK_UP : !strcmp(w, "down") ? SDLK_DOWN : !strcmp(w, "left") ? SDLK_LEFT :
                  !strcmp(w, "right") ? SDLK_RIGHT : !strcmp(w, "enter") ? SDLK_RETURN : 0;
        }
        if (editing) {                                     /* typing the host's address */
            if (key == SDLK_BACKSPACE && ch->join[0]) ch->join[strlen(ch->join) - 1] = 0;
            else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) { editing = 0; SDL_StopTextInput(win); cfg_save(base, ch); if (ch->join[0] && roms.g4_kind && roms.g4_kind != 2) { ch->mode = LAUNCH_JOIN; done = 1; } }
            else if (key == SDLK_ESCAPE) { editing = 0; SDL_StopTextInput(win); }
            key = 0;
        }
        item = PAGE_ITEMS[page][sel];
        if (key == SDLK_UP) sel = (sel + page_count(page) - 1) % page_count(page);
        else if (key == SDLK_DOWN) sel = (sel + 1) % page_count(page);
        else if (key == SDLK_ESCAPE && page) { sel = page == 1 ? 1 : 2; page = 0; }   /* back to the main page (Video / Audio) */
        else if (key == SDLK_ESCAPE && rec_proc) { SDL_KillProcess(rec_proc, true); SDL_WaitProcess(rec_proc, true, NULL); SDL_DestroyProcess(rec_proc); rec_proc = NULL; remove(rec_prog); snprintf(msg, sizeof msg, "Music recording stopped."); }
        else if (key == SDLK_ESCAPE) { ch->mode = LAUNCH_QUIT; done = 1; }
        else if (key == SDLK_LEFT || key == SDLK_RIGHT) {
            int d = key == SDLK_RIGHT ? 1 : -1;
            if (item == M_HOST) ch->players = 2 + (ch->players - 2 + d + 3) % 3;
            if (item == M_MUSIC) ch->vol_music = clamp10(ch->vol_music + d);
            if (item == M_SFX) ch->vol_sfx = clamp10(ch->vol_sfx + d);
            if (item == M_VOICE) ch->vol_voice = clamp10(ch->vol_voice + d);
            if (item == M_RES) ch->res = (ch->res + d + launch_n_resolutions) % launch_n_resolutions;
            if (item == M_DISPLAY) ch->display = (ch->display + d + 3) % 3;
            if (item == M_ASPECT) ch->aspect = (ch->aspect + d + 4) % 4;
            if (item == M_PACK) {                          /* "" (none), then the packs found */
                int cur = -1, k;
                for (k = 0; k < n_packs; k++) if (!strcmp(ch->pack, pack_names[k])) cur = k;
                cur = (cur + 1 + d + n_packs + 1) % (n_packs + 1) - 1;
                snprintf(ch->pack, sizeof ch->pack, "%s", cur < 0 ? "" : pack_names[cur]);
            }
            cfg_save(base, ch);
        }
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            msg[0] = 0;
            if (item == M_VIDEO) { page = 1; sel = 0; }
            else if (item == M_AUDIO) { page = 2; sel = 0; }
            else if (item == M_BACK) { sel = page == 1 ? 1 : 2; page = 0; }
            else if (item == M_RES || item == M_DISPLAY || item == M_ASPECT || item == M_PACK || item == M_MUSIC || item == M_SFX || item == M_VOICE) {
                /* Enter steps forward, as right does */
                if (item == M_RES) ch->res = (ch->res + 1) % launch_n_resolutions;
                if (item == M_DISPLAY) ch->display = (ch->display + 1) % 3;
                if (item == M_ASPECT) ch->aspect = (ch->aspect + 1) % 4;
                if (item == M_MUSIC) ch->vol_music = clamp10(ch->vol_music + 1);
                if (item == M_SFX) ch->vol_sfx = clamp10(ch->vol_sfx + 1);
                if (item == M_VOICE) ch->vol_voice = clamp10(ch->vol_voice + 1);
                if (item == M_PACK) { int cur = -1, k; for (k = 0; k < n_packs; k++) if (!strcmp(ch->pack, pack_names[k])) cur = k;
                    cur = (cur + 2) % (n_packs + 1) - 1; snprintf(ch->pack, sizeof ch->pack, "%s", cur < 0 ? "" : pack_names[cur]); }
                cfg_save(base, ch);
            }
            else if ((item == M_PLAY || item == M_HOST || item == M_JOIN || item == M_QUIT) && rec_proc)
                snprintf(msg, sizeof msg, "Wait for the music (or Esc stops it).");
            else if (item == M_PLAY || item == M_HOST || item == M_JOIN) {
                if (!roms.g4_kind) snprintf(msg, sizeof msg, "No Gauntlet 4 ROM in that folder.");
                else if (roms.g4_kind == 2) snprintf(msg, sizeof msg, "The August 1993 version won't work.");
                else if (item == M_JOIN) { editing = 1; SDL_StartTextInput(win); }
                else { ch->mode = item == M_PLAY ? LAUNCH_PLAY : LAUNCH_HOST; done = 1; }
            }
            else if (item == M_FOLDER) SDL_ShowOpenFolderDialog(folder_cb, NULL, win, ch->rom_dir, false);
            else if (item == M_BUILD && !rec_proc) {        /* the data steps done in C so far (g4data.c) */
                char out[1200]; int failed = 0;
                rec_made_speech = 0;
                if (roms.have_g1 && roms.g4_kind == 1) {   /* Gauntlet's colours for Gauntlet 4's levels: */
                    snprintf(out, sizeof out, "%sarcade_colours.txt", base);   /* the file starts anew */
                    if (data_g1_colours(&roms, out)) rec_made_speech = 1; else failed++;
                }
                if (roms.have_g2) {                        /* Gauntlet II: narration, pictures, colours */
                    snprintf(out, sizeof out, "%sg2_speech.bin", base);
                    if (data_speech(launch_zip_for(&roms, "136043-1120.16r"), launch_zip_for(&roms, "136043-1119.16s"), out)) rec_made_speech = 1; else failed++;
                    snprintf(out, sizeof out, "%sg2_sprites.bin", base);
                    if (!data_g2_sprites(&roms, out)) failed++;
                    snprintf(out, sizeof out, "%sg2_sfx.bin", base);   /* the sound effects (runs the arcade sound board) */
                    if (!data_g2_sfx(launch_zip_for(&roms, "136043-1120.16r"), launch_zip_for(&roms, "136043-1119.16s"), out, 74)) failed++;
                    if (roms.g4_kind == 1) {               /* the level set (checked with Gauntlet 4's own decoder) */
                        snprintf(out, sizeof out, "%sgauntlet2_levels.bin", base);
                        if (!data_g2_levels(&roms, out)) failed++;
                    }
                    snprintf(out, sizeof out, "%sg2_attract.bin", base);   /* the attract screens (runs the arcade program) */
                    if (!data_g2_attract(&roms, out, NULL)) failed++;
                    if (have_file(base, "gauntlet2_levels.bin")) {   /* the colours are keyed to the level file */
                        char lv[1200]; snprintf(lv, sizeof lv, "%sgauntlet2_levels.bin", base);
                        snprintf(out, sizeof out, "%sarcade_colours.txt", base);
                        if (!data_g2_colours(&roms, lv, out)) failed++;
                    }
                }
                if ((roms.atr_dd[0] || roms.tap_dd[0]) && roms.g4_kind == 1) {  /* GAUNTLET III: the Atari 8-bit mazes (Deeper Dungeons + the Atari's own) */
                    snprintf(out, sizeof out, "%sgauntlet3_levels.bin", base);
                    if (data_g3_levels(&roms, out)) rec_made_speech = 1; else failed++;
                }
                if (failed) snprintf(msg, sizeof msg, "Data: FAILED (%d step%s).", failed, failed == 1 ? "" : "s");
                else if (roms.g4_kind == 1) {              /* then Gauntlet 4's music, in the background */
                    if (!rec_start(base, roms.g4_path)) snprintf(msg, sizeof msg, "Could not start the music recording.");
                    else snprintf(msg, sizeof msg, "Recording music... (Esc stops)");
                }
                else if (rec_made_speech) snprintf(msg, sizeof msg, "Data built: arcade parts.");
                else snprintf(msg, sizeof msg, "Needs the Gauntlet 4 ROM or an arcade zip.");
            }
            else if (item == M_QUIT) { ch->mode = LAUNCH_QUIT; done = 1; }
        }
        if (rec_proc) rec_poll(msg, sizeof msg);
        {   /* draw */
            static const char *g4s[] = {"not found", "found", "August 1993: no", "unknown version"};
            static const char *aspects[4] = {"4:3 (TV)", "square pixels", "stretch", "sharp (whole steps)"};
            static const char *displays[3] = {"window", "fullscreen", "borderless"};
            char line[96], t[64]; int k, row, n = page_count(page);
            SDL_SetRenderDrawColor(ren, 12, 10, 20, 255); SDL_RenderClear(ren);
            text_at(ren, 2, 1, page == 1 ? "GHOST - VIDEO" : page == 2 ? "GHOST - AUDIO" : "GHOST " GHOST_VERSION " - LAUNCHER", 230, 200, 90);
            for (k = 0, row = 3; k < n; k++, row += 2) {
                int on = k == sel, it = PAGE_ITEMS[page][k];
                if (on) { SDL_FRect bar = {8, (float)(row * 8 - 2), 304, 12}; SDL_SetRenderDrawColor(ren, 60, 40, 90, 255); SDL_RenderFillRect(ren, &bar); }
                text_at(ren, 2, row, ITEM_NAMES[it], on ? 255 : 200, on ? 255 : 200, on ? 255 : 200);
                line[0] = 0;
                if (it == M_HOST) snprintf(line, sizeof line, "<%d players>", ch->players);
                if (it == M_JOIN) { tail(t, sizeof t, ch->join[0] ? ch->join : "(type address)", 15); snprintf(line, sizeof line, "%s%s", t, editing && (frame / 20) % 2 ? "_" : ""); }
                if (it == M_FOLDER) { tail(t, sizeof t, strcmp(ch->rom_dir, ".") && strcmp(ch->rom_dir, "./") ? ch->rom_dir : "(next to game)", 16); snprintf(line, sizeof line, "%s", t); }
                if (it == M_VIDEO || it == M_AUDIO) snprintf(line, sizeof line, ">");
                if (it == M_MUSIC) snprintf(line, sizeof line, "< %2d >%s", ch->vol_music, ch->vol_music ? "" : " off");
                if (it == M_SFX) snprintf(line, sizeof line, "< %2d >", ch->vol_sfx);
                if (it == M_VOICE) snprintf(line, sizeof line, "< %2d >", ch->vol_voice);
                if (it == M_RES) snprintf(line, sizeof line, "< %dx%d >", launch_resolutions[ch->res][0], launch_resolutions[ch->res][1]);
                if (it == M_DISPLAY) snprintf(line, sizeof line, "< %s >", displays[ch->display]);
                if (it == M_ASPECT) snprintf(line, sizeof line, "< %s >", aspects[ch->aspect]);
                if (it == M_PACK) { tail(t, sizeof t, ch->pack[0] ? ch->pack : (n_packs ? "none" : "none found"), 14); snprintf(line, sizeof line, "< %s >", t); }
                text_at(ren, page ? 18 : 23, row, line, 150, 200, 255);   /* main page: after the longest name */
            }
            if (page == 1) {                               /* what the choices mean */
                SDL_SetRenderDrawColor(ren, 160, 160, 160, 255);
                SDL_RenderDebugText(ren, 16, 150, "Graphics packs: folders in \"packs\"");
                SDL_RenderDebugText(ren, 16, 160, "next to the game (see the README).");
            }
            row = 21;
            if (page == 0) {   /* what was found: every line fits the 38 columns from x 16 */
                int y = 124;
                #define LINE(str, ok) do { SDL_SetRenderDrawColor(ren, (ok) == 1 ? 120 : (ok) == 2 ? 255 : 160, (ok) == 1 ? 230 : (ok) == 2 ? 120 : 160, (ok) == 1 ? 120 : (ok) == 2 ? 120 : 160, 255); \
                                           SDL_RenderDebugText(ren, 16, (float)y, str); y += 9; } while (0)
                #define OK(f) (have_file(base, f) ? "ok" : "--")
                SDL_SetRenderDrawColor(ren, 230, 200, 90, 255); SDL_RenderDebugText(ren, 16, (float)y, "Found in the ROM folder:"); y += 10;
                snprintf(line, sizeof line, " Gauntlet 4:      %s", g4s[roms.g4_kind]); LINE(line, roms.g4_kind == 1 ? 1 : roms.g4_kind ? 2 : 0);
                snprintf(line, sizeof line, " Gauntlet:        %s", roms.have_g1 ? "found (arcade)" : "not found"); LINE(line, roms.have_g1);
                snprintf(line, sizeof line, " Gauntlet II:     %s", roms.have_g2 ? "found (arcade)" : "not found"); LINE(line, roms.have_g2);
                snprintf(line, sizeof line, " Deeper Dungeons: %s%s", roms.atr_dd[0] ? "Atari disk" : roms.tap_dd[0] ? "C64 tape" : "not found",
                         roms.atr_orig[0] ? " +side 2" : roms.crt_orig[0] ? " +C64 cart" : ""); LINE(line, roms.atr_dd[0] || roms.tap_dd[0] ? 1 : 0);
                y += 4;
                SDL_SetRenderDrawColor(ren, 230, 200, 90, 255); SDL_RenderDebugText(ren, 16, (float)y, "Data files:"); y += 10;
                snprintf(line, sizeof line, " levels %s   pictures %s   voice %s", OK("gauntlet2_levels.bin"), OK("g2_sprites.bin"), OK("g2_speech.bin")); LINE(line, 0);
                snprintf(line, sizeof line, " effects %s  attract %s    music %s", OK("g2_sfx.bin"), OK("g2_attract.bin"), OK("g4_music.bin")); LINE(line, 0);
                snprintf(line, sizeof line, " GAUNTLET III levels %s", OK("gauntlet3_levels.bin")); LINE(line, 0);
                #undef OK
                #undef LINE
            }
            (void)row;
            if (msg[0]) {                                  /* green when something was done, red for a problem */
                int good = (strstr(msg, "built") || strstr(msg, "Recording")) && !strstr(msg, "FAIL");
                SDL_SetRenderDrawColor(ren, good ? 120 : 255, good ? 230 : 140, 120, 255);
                {   /* up to two lines of 38 columns, broken at a space */
                    int l = (int)strlen(msg), cut = l;
                    if (l > 38) { for (cut = 38; cut > 0 && msg[cut] != ' '; cut--) ; if (cut == 0) cut = 38; }
                    snprintf(line, sizeof line, "%.*s", cut, msg); SDL_RenderDebugText(ren, 16, l > 38 ? 220 : 229, line);
                    if (l > 38) { const char *r = msg + cut + (msg[cut] == ' '); snprintf(line, sizeof line, "%.38s", r); SDL_RenderDebugText(ren, 16, 229, line); }
                } }
            SDL_RenderPresent(ren);
        }
        frame++;
        if (shot && (!keys || !*keys) && frame > (getenv("G4LAUNCHWAIT") ? atoi(getenv("G4LAUNCHWAIT")) : 2)) {   /* test aid: save the
                                                              screen and quit (G4LAUNCHWAIT=frames first) */
            SDL_Surface *sf = SDL_RenderReadPixels(ren, NULL);
            if (sf) {
                SDL_Surface *rgb = SDL_ConvertSurface(sf, SDL_PIXELFORMAT_RGB24);
                FILE *f = fopen(shot, "wb"); int y;
                if (f && rgb) { fprintf(f, "P6\n%d %d\n255\n", rgb->w, rgb->h); for (y = 0; y < rgb->h; y++) fwrite((Uint8 *)rgb->pixels + y * rgb->pitch, 3, (size_t)rgb->w, f); }
                if (f) fclose(f);
                SDL_DestroySurface(rgb); SDL_DestroySurface(sf);
            }
            ch->mode = LAUNCH_QUIT; done = 1;
        }
        SDL_Delay(16);
    }
    if (rec_proc) {                                    /* leaving with a recording running: stop it */
        SDL_KillProcess(rec_proc, true); SDL_WaitProcess(rec_proc, true, NULL); SDL_DestroyProcess(rec_proc); rec_proc = NULL; remove(rec_prog);
    }
    cfg_save(base, ch);
    snprintf(ch->rom, sizeof ch->rom, "%s", roms.g4_path);
    SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
    return ch->mode != LAUNCH_QUIT;
}
#else
int launch_run(const char *base, launch_choice *ch)        /* no SDL: say what would be found */
{
    cfg_defaults(ch, base);
    cfg_load(base, ch);
    launch_scan_cli(ch->rom_dir);
    fprintf(stderr, "(this build has no window: start it as  ghost ROM [options])\n");
    return 0;
}
#endif
