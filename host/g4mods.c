/*
 * g4mods.c -- optional extras for the native Gauntlet 4 host, outside the original game.
 * All of them are off with --original. Nothing here changes the translated code: the extras
 * are new data written into unused ROM space (0xFC000-0xFDFFF, zeros in the cartridge) and
 * C routines reached through the dispatcher's override hook.
 *
 * START LEVEL (Arcade and Record): an Options item "Start Level  1 / 8".
 *   The Options screen (0x23F52) reads a per-mode table at 0x24096 -> {items, change
 *   handlers, draw routines, English labels, Japanese labels}. Arcade (mode 0) gets a copy of
 *   its table with the item added (10 items), Record (mode 3) a copy of the shared
 *   Quest/Battle/Record table with it first (5 items). Ten items do not fit below the
 *   OPTIONS box (rows 7-26 are labels/values, row 26 "PRESS START TO RETURN"), so the item
 *   list (0x244DE) starts one row higher, on row 6, when it has ten.
 *   Effect, both through the game's own level-8 step (arcade_next 0xF06, wrapped so that it
 *   takes the real map 8 instead of the game's random / last-game map): Arcade -- after
 *   setup_arcade (0xC26) the level counter is set to 7, then arcade_next; Record -- the new
 *   record's level counter (+$1AA) is set to 7 after 0x17B2C chose it, and 0x17A70 continues
 *   it like a record at level 7.
 *   Music: Arcade starts its song only on level 1 (0x28AE) and plays it on; a level-8 start
 *   runs that once as on level 1 (0x288E wrapper).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../c/g4_m68k.h"
#include "g4mods.h"
#include "g4pack.h"
#include "g4net.h"

int mods_enabled = 1;
static int start_level8;                 /* the option; changed only through the menu, so online
                                            every copy changes it on the same frame */

/* ------------------------------------------------------------------ addresses */
enum {
    DATA        = 0x0FC000,              /* new tables and strings */
    FN_LEVEL_CHANGE = 0x0FD000,          /* "routines": addresses only, run by the override hook */
    FN_LEVEL_DRAW   = 0x0FD010,
    OPT_TABLE   = 0x024096,              /* per mode: pointer to {items, a2, a3, d6, d7} */
    ARCADE_OPT  = 0x0240A6,
    SHARED_OPT  = 0x0240DE
};

static uint32_t here;                    /* next free byte in DATA */
char mods_base[1024];                    /* the program's folder (g4host.c sets it) */
static uint32_t put_string(const char *s)
{
    uint32_t a = here;
    do wr8(here++, (uint8_t)*s); while (*s++);
    here = (here + 1) & ~1u;
    return a;
}
static uint32_t put_long(uint32_t v) { uint32_t a = here; wr32(here, v); here += 4; return a; }

/* copy a menu table with one item inserted at position pos */
static uint32_t make_menu(uint32_t old, int pos, uint32_t change, uint32_t draw, uint32_t en, uint32_t ja)
{
    uint32_t n = rd32(old), tab[4], t, newtab[4], extra[4], i, k;
    extra[0] = change; extra[1] = draw; extra[2] = en; extra[3] = ja;
    for (t = 0; t < 4; t++) tab[t] = rd32(old + 4 + 4 * t);
    for (t = 0; t < 4; t++) {
        newtab[t] = here;
        for (i = 0, k = 0; i <= n; i++)
            put_long(i == (uint32_t)pos ? extra[t] : rd32(tab[t] + 4 * k++));
    }
    t = here;
    put_long(n + 1);
    for (i = 0; i < 4; i++) put_long(newtab[i]);
    return t;
}

static uint32_t str_label_en, str_label_ja, str_1, str_8, str_g4, str_g2, str_g3_1, str_g3_8;
static int g2_count, g2_versions = 1;
static uint32_t g2_base = 0x380000;                  /* the level file in use: Gauntlet II (0x380000) or */
#define G2_BASE g2_base                               /* GAUNTLET III (0x300000), switched on SELECT GAME */
int level_set_g2, level_set_g3; static uint32_t g2_end;
static struct { uint32_t base, end; int count, versions, normal; } lset[2] = {{0x380000, 0, 0, 1, 0}, {0x300000, 0, 0, 1, 0}};
static void use_set(int s)
{
    g2_base = lset[s].base; g2_end = lset[s].end; g2_count = lset[s].count; g2_versions = lset[s].versions;
}
static int g2_mirror;                    /* this play's mirror version: bit 0 left-right, bit 1 top-bottom */
static uint32_t rnd_rule;          /* the random rule of this play of the maze (Gauntlet II 0x436FE) */
static const char *rnd_msg;        /* its message, shown when play starts */

void mods_init(void)
{
    {   extern int16_t g4_hero_class_tab[4];             /* test aid: G4CLASS=a,b,c,d -- hero class per player */
        const char *e = getenv("G4CLASS"); int k = 0;
        while (e && *e && k < 4) { g4_hero_class_tab[k++] = (int16_t)atoi(e); while (*e && *e != ',') e++; if (*e) e++; }
    }
    uint32_t arcade, record;
    if (!mods_enabled) return;
    here = DATA;
    str_label_en = put_string("Start Level");
    str_label_ja = put_string("\xbd\xc0\xb0\xc4 \xda\xcd\xde\xd9");    /* ｽﾀｰﾄ ﾚﾍﾞﾙ */
    str_1 = put_string("GAUNTLET: Level 1    ");     /* all six the same length: each covers the last */
    str_8 = put_string("GAUNTLET: Level 8    ");
    str_g3_1 = put_string("GAUNTLET III: Level 1");
    str_g3_8 = put_string("GAUNTLET III: Level 8");
    arcade = make_menu(ARCADE_OPT, 5, FN_LEVEL_CHANGE, FN_LEVEL_DRAW, str_label_en, str_label_ja);
    record = make_menu(SHARED_OPT, 0, FN_LEVEL_CHANGE, FN_LEVEL_DRAW, str_label_en, str_label_ja);
    str_g4 = put_string("GAUNTLET II: Level 1 ");       /* with the Gauntlet II mazes loaded, Start Level */
    str_g2 = put_string("GAUNTLET II: Level 8 ");        /* also picks the level set (one item: 11 do not fit) */
    wr32(OPT_TABLE + 0 * 4, arcade);
    wr32(OPT_TABLE + 3 * 4, record);
}

/* ------------------------------------------------------------------ new routines */
/* change: any direction or button toggles (the game's own toggles, e.g. Reduce Text 0x241A4,
   ignore D4 the same way) */
static void level_change(M68K *c)                       /* Level 1 <-> Level 8 (the game -- Gauntlet or
                                                           Gauntlet II -- is chosen on its own screen) */
{
    (void)c;
    start_level8 = !start_level8;
}

/* The game-choice screen (user, 2026-11-21): after Arcade or Record on the mode select (0x236C6:
   title record 0x39766 through 0x1CCFA, then the menu 0x23E96(count, row 11, current, list) ->
   choice or < 0 when backed out; then the mode's own screen from table 0x23796, whose Z clear
   means "back to the mode select"), a screen "SELECT GAME: GAUNTLET / GAUNTLET II" in the same
   style. Without gauntlet2_levels.bin it is skipped (Gauntlet). */
static void (*orig_arcade_screen)(M68K *), (*orig_record_screen)(M68K *);
static void game_choice_then(M68K *c, void (*mode_screen)(M68K *))
{
    static uint32_t rec, list;
    int choice, k;
    static int n_items, item_set[3];                    /* menu line -> 0 Gauntlet, 1 Gauntlet II, 2 GAUNTLET III */
    if (!lset[0].count && !lset[1].count) { level_set_g2 = level_set_g3 = 0; mode_screen(c); return; }
    if (!rec) {                                         /* the title record: a copy of MODE SELECT's (text,
                                                           box) with our text; the names */
        uint32_t t = put_string("SELECT GAME"), g1 = put_string("GAUNTLET"), g2 = put_string("GAUNTLET II"), g3 = put_string("GAUNTLET III");
        list = put_long(g1); item_set[n_items++] = 0;
        if (lset[0].count) { put_long(g2); item_set[n_items++] = 1; }
        if (lset[1].count) { put_long(g3); item_set[n_items++] = 2; }
        rec = here;
        for (k = 0; k < 24; k++) wr8(here++, rd8(0x039766 + (uint32_t)k));
        wr32(rec + 8, t);
    }
    for (;;) {
        JSR(c, 0x0FD020, 0x020496); JSR(c, 0x0FD020, 0x01CE30);      /* wait a frame, clear the screen */
        c->a[7] -= 4; wr32(c->a[7], rec); JSR(c, 0x0FD020, 0x01CCFA); c->a[7] += 4;
        c->a[7] -= 4; wr32(c->a[7], list);
        {   int cur = level_set_g3 ? 2 : level_set_g2 ? 1 : 0, q, line = 0;
            for (q = 0; q < n_items; q++) if (item_set[q] == cur) line = q;
            c->a[7] -= 2; wr16(c->a[7], (uint16_t)line); }
        c->a[7] -= 2; wr16(c->a[7], 0x0B);
        c->a[7] -= 2; wr16(c->a[7], (uint16_t)n_items);
        JSR(c, 0x0FD020, 0x023E96);
        choice = (int16_t)rd16(c->a[7]); c->a[7] += 10;
        if (choice < 0) { c->z = 0; return; }           /* backed out: to the mode select */
        {   int set = item_set[choice < n_items ? choice : 0];
            level_set_g2 = set >= 1; level_set_g3 = set == 2;
            use_set(level_set_g3 ? 1 : 0); }
        mode_screen(c);
        if (c->z) return;                               /* played (or the game went on) */
        /* backed out of the mode's screen: this screen again */
    }
}
/* A game-style menu screen: the title record (a copy of MODE SELECT's 0x39766 with our text) and
   the menu 0x23E96(count, row 11, current, list). Returns the choice, < 0 when backed out. */
static uint32_t menu_record(const char *title, const char *const *items, int n, uint32_t *list)
{
    uint32_t t = put_string(title), names[8], rec; int k;
    for (k = 0; k < n; k++) names[k] = put_string(items[k]);
    *list = put_long(names[0]); for (k = 1; k < n; k++) put_long(names[k]);
    rec = here;
    for (k = 0; k < 24; k++) wr8(here++, rd8(0x039766 + (uint32_t)k));
    wr32(rec + 8, t);
    return rec;
}
static int run_menu(M68K *c, uint32_t rec, uint32_t list, int n, int current)
{
    int choice;
    JSR(c, 0x0FD020, 0x020496); JSR(c, 0x0FD020, 0x01CE30);          /* wait a frame, clear the screen */
    c->a[7] -= 4; wr32(c->a[7], rec); JSR(c, 0x0FD020, 0x01CCFA); c->a[7] += 4;
    c->a[7] -= 4; wr32(c->a[7], list);
    c->a[7] -= 2; wr16(c->a[7], (uint16_t)current);
    c->a[7] -= 2; wr16(c->a[7], 0x0B);
    c->a[7] -= 2; wr16(c->a[7], (uint16_t)n);
    JSR(c, 0x0FD020, 0x023E96);
    choice = (int16_t)rd16(c->a[7]); c->a[7] += 10;
    return choice;
}
/* LOCAL GAME / ONLINE (user, 2026-11-22): in front of MODE SELECT (0x236A4, called by the main loop
   at 0x520 after the title; Z set = a game was played, clear = back to the title). LOCAL GAME is
   MODE SELECT as before (backing out of it comes back here); ONLINE is HOST GAME / JOIN GAME (the
   lobby, mods_lobby*). In an online session (the restarted copies) MODE SELECT comes straight away,
   and only the host's controller moves its cursor. */
/* online: while a game menu runs (0x23E96: MODE SELECT, SELECT GAME, ...), only the host's controller
   counts (g4host.c clears the other seats' buttons while this is set) */
int mods_menu_host_only;
static void (*orig_menu)(M68K *);
static void menu_wrap(M68K *c) { mods_menu_host_only++; orig_menu(c); mods_menu_host_only--; }
static void (*orig_mode_select)(M68K *);
static int play_choice_last, online_choice_last;
static int lobby_run(M68K *c, int hosting);            /* below: 1 = everyone is restarting */
static void mode_select(M68K *c)
{
    static uint32_t rec_play, list_play, rec_online, list_online;
    if (!mods_enabled || mods_online || rd8(0xFFE82C) || rd8(0xFFE871)) { orig_mode_select(c); return; }
    if (!rec_play) {
        static const char *const play[2] = {"LOCAL GAME", "ONLINE"}, *const online[2] = {"HOST GAME", "JOIN GAME"};
        rec_play = menu_record("SELECT PLAY", play, 2, &list_play);
        rec_online = menu_record("ONLINE", online, 2, &list_online);
    }
    wr8(0xFFE79B, 0xFF);                                /* as MODE SELECT starts (0x236A8..0x236D2) */
    JSR(c, 0x0FD020, 0x01E406); JSR(c, 0x0FD020, 0x01E43C); JSR(c, 0x0FD020, 0x01E576); JSR(c, 0x0FD020, 0x01E5AC);
    JSR(c, 0x0FD020, 0x01E99A); JSR(c, 0x0FD020, 0x01EA08);
    for (;;) {
        int choice = run_menu(c, rec_play, list_play, 2, play_choice_last);
        if (choice < 0) {                               /* backed out: to the title, as MODE SELECT does */
            wr8(0xFFE79B, 0); wr16(0xFFE838, 0); c->z = 0; return;
        }
        play_choice_last = choice;
        if (choice == 0) {
            orig_mode_select(c);
            if (c->z) return;                           /* a game was played */
            continue;                                   /* backed out of MODE SELECT: this screen again */
        }
        for (;;) {                                      /* ONLINE */
            int oc = run_menu(c, rec_online, list_online, 2, online_choice_last);
            if (oc < 0) break;
            online_choice_last = oc;
            if (lobby_run(c, oc == 0)) { c->z = 0; return; }   /* (the program restarts online) */
        }
    }
}
/* The lobby (user, 2026-11-22): HOST GAME listens (port 47100) and lists who has joined and in
   which colour; JOIN GAME asks for the host's address (typed; the last one is remembered), connects,
   and lets the player pick a free colour with left/right. The host is 1P red. The host's Start
   begins: every copy restarts the program online (host_relaunch), skips the title and lands in
   MODE SELECT. B leaves. The screen is a box drawn over the menu screen (mods_lobby_lines). */
uint8_t host_local_pressed(void); void host_text_begin(void); int host_text_state(void); const char *host_text(void);
void host_text_end(void); uint32_t host_rom_hash(void); void host_relaunch(int hosting, int players, int seat, const char *addr);
void launch_save_join(const char *base, const char *addr);
static char lobby_line[10][40]; static int lobby_nlines, lobby_on;
int mods_lobby_lines(const char **out, int max)
{
    int k;
    if (!lobby_on) return 0;
    for (k = 0; k < lobby_nlines && k < max; k++) out[k] = lobby_line[k];
    return k;
}
static void lobby_clear(void) { lobby_nlines = 0; memset(lobby_line, 0, sizeof lobby_line); }
static void lobby_text(int k, const char *t) { snprintf(lobby_line[k], sizeof lobby_line[k], "%s", t); if (k + 1 > lobby_nlines) lobby_nlines = k + 1; }
static const char *seat_name[4] = {"1P RED", "2P BLUE", "3P YELLOW", "4P GREEN"};
static void lobby_seat_lines(int hosting, int mine)
{
    int s2, mask = lobby_seats();
    for (s2 = 0; s2 < 4; s2++) {
        char t[40]; const char *who = s2 == 0 ? (hosting ? "HOST (YOU)" : "HOST") : !(mask & (1 << s2)) ? "OPEN" : (!hosting && s2 == mine) ? "YOU" : "PLAYER";
        snprintf(t, sizeof t, "%-10s %s", seat_name[s2], who);
        lobby_text(2 + s2, t);
    }
}
static int lobby_wait_b(M68K *c)                       /* a message until B / Start */
{
    for (;;) { JSR(c, 0x0FD020, 0x020496); if (host_local_pressed() & 0xB0) return 0; }   /* B, Start (0x20|0x80), A 0x10 */
}
extern char mods_base[1024];
static int lobby_run(M68K *c, int hosting)
{
    char addr[256] = "";
    lobby_on = 1; lobby_clear(); memset(lobby_line, 0, sizeof lobby_line);
    host_local_pressed();                              /* (forget the press that chose this) */
    if (hosting) {
        if (!lobby_host_open(47100, host_rom_hash())) {
            lobby_text(0, "HOST GAME"); lobby_text(2, "PORT 47100 IS IN USE"); lobby_text(4, "B: BACK");
            lobby_wait_b(c); lobby_on = 0; return 0;
        }
        for (;;) {
            uint8_t p; int n = 0, s2;
            JSR(c, 0x0FD020, 0x020496);
            lobby_poll();
            for (s2 = 1; s2 < 4; s2++) if (lobby_seats() & (1 << s2)) n++;
            lobby_clear();
            lobby_text(0, "HOST GAME  (PORT 47100)");
            lobby_seat_lines(1, 0);
            lobby_text(7, n ? "START: BEGIN   B: CANCEL" : "WAITING...   B: CANCEL");
            p = host_local_pressed();
            if (p & 0x20) { lobby_close(); lobby_on = 0; return 0; }          /* B */
            if ((p & 0x80) && n) {                                           /* Start */
                int players = lobby_go();
                lobby_close();
                host_relaunch(1, players, 0, NULL);                         /* (does not return) */
            }
        }
    }
    /* JOIN GAME: the address */
    host_text_begin();
    for (;;) {
        int st;
        JSR(c, 0x0FD020, 0x020496);
        lobby_clear();
        lobby_text(0, "JOIN GAME");
        lobby_text(2, "HOST ADDRESS:");
        {   char t[40]; const char *h = host_text(); size_t l = strlen(h);
            static int blink; blink++;
            snprintf(t, sizeof t, "%s%s", l > 26 ? h + l - 26 : h, (blink / 20) % 2 ? "_" : " "); lobby_text(3, t); }
        lobby_text(5, "TYPE IT, THEN ENTER");
        lobby_text(6, "ESC: BACK");
        st = host_text_state();
        if (st < 0) { host_text_end(); lobby_on = 0; return 0; }
        if (st > 0) { snprintf(addr, sizeof addr, "%s", host_text()); host_text_end(); break; }
    }
    lobby_clear(); lobby_text(0, "JOIN GAME"); lobby_text(2, "CONNECTING...");
    JSR(c, 0x0FD020, 0x020496);
    if (!addr[0] || !lobby_join_open(addr, 47100, host_rom_hash())) {
        lobby_clear(); lobby_text(0, "JOIN GAME"); lobby_text(2, "NO HOST AT THAT ADDRESS"); lobby_text(4, "B: BACK");
        lobby_wait_b(c); lobby_on = 0; return 0;
    }
    launch_save_join(mods_base, addr);
    for (;;) {
        uint8_t p; int r;
        JSR(c, 0x0FD020, 0x020496);
        r = lobby_poll();
        if (r == 1) host_relaunch(0, lobby_players(), lobby_my(), addr);    /* GO (does not return) */
        if (r < 0) {
            lobby_clear(); lobby_text(0, "JOIN GAME");
            lobby_text(2, r == -1 ? "THE HOST HAS ANOTHER ROM" : r == -3 ? "THE GAME IS FULL" : "THE HOST HAS LEFT");
            lobby_text(4, "B: BACK");
            lobby_close(); lobby_wait_b(c); lobby_on = 0; return 0;
        }
        lobby_clear();
        lobby_text(0, "JOIN GAME");
        lobby_seat_lines(0, lobby_my());
        lobby_text(7, "LEFT/RIGHT: COLOUR  B: LEAVE");
        lobby_text(8, "WAITING FOR THE HOST...");
        p = host_local_pressed();
        if (p & 0x20) { lobby_close(); lobby_on = 0; return 0; }
        if ((p & 0x0C) && lobby_my()) {                                    /* left 4 / right 8: the next free colour */
            int d = (p & 0x08) ? 1 : -1, s2 = lobby_my(), k;
            for (k = 0; k < 3; k++) { s2 += d; if (s2 > 3) s2 = 1; if (s2 < 1) s2 = 3; if (!(lobby_seats() & (1 << s2))) { lobby_want(s2); break; } }
        }
    }
}
static void arcade_screen(M68K *c) { game_choice_then(c, orig_arcade_screen); }
static void record_screen(M68K *c) { game_choice_then(c, orig_record_screen); }

/* draw the value under the label, like 0x24692: column D2 + 4, row D3 + 1, colour D1 */
static void level_draw(M68K *c)
{
    uint32_t d2 = c->d[2], d3 = c->d[3], a1 = c->a[1];
    SETW(c->d[2], W(c->d[2]) + 4);
    SETW(c->d[3], W(c->d[3]) + 1);
    c->a[1] = level_set_g3 ? (start_level8 ? str_g3_8 : str_g3_1) : level_set_g2 ? (start_level8 ? str_g2 : str_g4) : (start_level8 ? str_8 : str_1);
    JSR(c, FN_LEVEL_DRAW + 2, 0x01D0F2);
    c->d[2] = d2; c->d[3] = d3; c->a[1] = a1;
}

/* 0x244DE: draw every item, label then value, last item first; the selected one (D1) in
   colour 1, the others in 2. In: D2 items, D1 selected, A3 draw routines, D6/D7 labels.
   Same as the original except that ten items start on row 6 instead of 7. */
static void item_list(M68K *c)
{
    uint32_t save[16], i;
    int n = (int16_t)W(c->d[2]), sel = (int16_t)W(c->d[1]), k;
    for (i = 0; i < 8; i++) { save[i] = c->d[i]; save[8 + i] = c->a[i]; }
    for (k = n - 1; k >= 0; k--) {
        c->d[2] = 3;
        c->d[3] = (uint32_t)((n >= 10 ? 6 : 7) + 2 * k);
        c->d[1] = (k == sel) ? 1 : 2;
        c->d[0] = (uint32_t)(4 * k);
        c->a[1] = rd32((rd8(0xFFE791) ? save[7] : save[6]) + 4 * (uint32_t)k);
        JSR(c, 0x024516, 0x01D0F2);
        c->d[0] = (uint32_t)(4 * k);
        c->a[0] = rd32(save[8 + 3] + 4 * (uint32_t)k);
        JSR(c, 0x02451C, c->a[0]);
    }
    /* the original saves and restores d0-d5/a0-a1/a5 */
    for (i = 0; i <= 5; i++) c->d[i] = save[i];
    c->a[0] = save[8]; c->a[1] = save[9]; c->a[5] = save[13];
}

static int music_pending;
static int map8_pending;                 /* the next arcade_next goes to the real map 8 */

/* 0xF06 arcade_next. On reaching level 8 it takes map $FFE762 -- unless the countdown
   $FFE770 is still at its power-on 3, when it marks a hidden power-up level and picks a
   random map (0x890); $FFE762 itself holds the level the last Arcade game ended on (>= 8).
   For a Start Level 8 game: map 8, and the countdown left where a first visit leaves it (2). */
static void (*orig_next_level)(M68K *);
static void next_level(M68K *c)
{
    if (level_set_g2 && mods_enabled) { orig_next_level(c); wr16(0xFFE768, 99); return; }   /* Gauntlet II mode:
                                     its own treasure rooms instead of Gauntlet 4's countdown ($FFE768) */
    if (map8_pending) {
        map8_pending = 0;
        wr16(0xFFE762, 8);
        wr16(0xFFE770, 2);
        orig_next_level(c);
        wr16(0xFFE770, 2);
        return;
    }
    orig_next_level(c);
}                /* Arcade level-8 start: the first level still needs its song */

static void (*orig_setup_arcade)(M68K *);
static void setup_arcade(M68K *c)
{
    orig_setup_arcade(c);
    if (start_level8) {
        /* the game's own way into level 8: with the level counter at 7, arcade_next (0xF06)
           counts to 8 and starts the treasure-room countdown; map8_pending makes it take the
           real map 8 (see next_level). The difficulty ramp ($FFE782) starts fresh, as after
           the level 4 "exit to 8" */
        uint32_t d0 = c->d[0];
        wr16(0xFFE760, 7);
        map8_pending = 1;
        JSR(c, 0x000C80, 0x000F06);
        JSR(c, 0x000C80, 0x0040D8);                     /* level data pointer, as 0xC7C does */
        c->d[0] = d0;
        music_pending = 1;
    }
}

/* 0x288E, level-start music (Arcade: 0x28AE). Arcade starts its song only on level 1
   (counter $FFE760 <= 1, BGM on) and lets it play on; from level 8 on it starts none. On the
   first level of a level-8 start it runs once as if on level 1. */
static void (*orig_level_music)(M68K *);
static void level_music(M68K *c)
{
    if (music_pending && rd16(0xFFE838) == 0) {
        uint16_t counter = rd16(0xFFE760);
        music_pending = 0;
        wr16(0xFFE760, 1);
        orig_level_music(c);
        wr16(0xFFE760, counter);
        return;
    }
    music_pending = 0;
    orig_level_music(c);
}

static void (*orig_record_start)(M68K *);
static void record_start(M68K *c)                       /* 0x17B2C, reached only from the new-record path */
{
    orig_record_start(c);
    /* it chose the record ($FFE96C); 0x17A70 runs next and starts from the record's level
       counter (+$1AA, 0 for a new one): 7 makes it level 8. 0x17A70 then picks Record mode's
       random level-8 map (0x17ABC) and calls arcade_next, where map8_pending makes it map 8 */
    if (start_level8) { wr16(rd32(0xFFE96C) + 0x1AA, 7); map8_pending = 1; }
}

/* ------------------------------------------------------------------ level set: Gauntlet II */
/* --level-set gauntlet2: Arcade and Record play the arcade Gauntlet II mazes, converted by
   g2_convert.py into host/gauntlet2_levels.bin ("G4L2", u16 count, u16 0, 64-byte remap table,
   count x u32 offsets, records). The file is loaded above the cartridge at G2_BASE; the level
   pointer routine 0x40D8 hands out its records for maps 1..count (not treasure rooms, not
   Quest/Battle), and the map remap 0x19340 uses the file's table for them (Gauntlet II needs
   trap groups the Arcade table lacks). */


/* Gauntlet II pictures for the host to draw (g2_sprites.bin: "G2SP", u16 n, per picture u16 id,
   u8 w, u8 h, RGBA) */
static struct { int id, w, h; uint8_t *px; } g2pic[64];
static int n_g2pic;
static void g2_sprites_load(const char *path)
{
    FILE *f = fopen(path, "rb"); uint8_t hd[6];
    if (!f) return;
    if (fread(hd, 1, 6, f) == 6 && !memcmp(hd, "G2SP", 4)) {
        int n = hd[4] << 8 | hd[5], k;
        for (k = 0; k < n && n_g2pic < 64; k++) {
            uint8_t e[4]; int w, h;
            if (fread(e, 1, 4, f) != 4) break;
            w = e[2]; h = e[3];
            g2pic[n_g2pic].px = malloc((size_t)(w * h * 4));
            if (!g2pic[n_g2pic].px || fread(g2pic[n_g2pic].px, 1, (size_t)(w * h * 4), f) != (size_t)(w * h * 4)) break;
            g2pic[n_g2pic].id = e[0] << 8 | e[1]; g2pic[n_g2pic].w = w; g2pic[n_g2pic].h = h; n_g2pic++;
        }
        fprintf(stderr, "Gauntlet II pictures: %d from %s\n", n_g2pic, path);
    }
    fclose(f);
}
static int g2pic_find(int id) { int k; for (k = 0; k < n_g2pic; k++) if (g2pic[k].id == id) return k; return -1; }
/* graphics packs: the Gauntlet II picture or shape whose box the pixel mods_overlay_px was asked
   about lies in (mods_pic_id -1 = none; set for the WHOLE box, so a pack picture may use all of it):
   its number, the place in it and the brightness it was drawn at (256 = as is).
   Numbers below 100 = g2_sprites.bin pictures; 100 up = the port's own drawn shapes: */
enum { SH_IT = 100, SH_STUN = 101, SH_BEAM = 102,  /* beams: +0 green across, +1 green up-down, +2 red across,
                                                       +3 red up-down, +4 off across, +5 off up-down */
       SH_FIREBALL = 108, SH_AMULET = 110,          /* amulets +0..+4: extras 5-9 */
       SH_EXIT6 = 115, SH_LAST = 115 };
int mods_pic_id = -1, mods_pic_x, mods_pic_y, mods_pic_scale = 256;
static void pic_at(int id, int x, int y) { mods_pic_id = id; mods_pic_x = x; mods_pic_y = y; }
static void pic_pk(int pk, int x, int y)
{
    pic_at(g2pic[pk].id, x, y);
    if (pack_dumping) pack_dump_pic(g2pic[pk].id, g2pic[pk].w, g2pic[pk].h, g2pic[pk].px);
}
int mods_pic_size(int id, int *w, int *h)
{
    int k;
    if (id >= 100) { if (id > SH_LAST || id == 109) return 0; *w = *h = id == SH_FIREBALL ? 32 : 16; return 1; }
    if ((k = g2pic_find(id)) < 0) return 0;
    *w = g2pic[k].w; *h = g2pic[k].h; return 1;
}

/* The Gauntlet II narration (g2_speech.bin from g2_speech.py, next to the level file; optional):
   "G2VO", u16 count, u16 rate, count x (u32 offset, u32 samples), signed 8-bit clips. Phrases
   163-178 are "<colour> <hero>" (red, blue, yellow, green x Warrior, Valkyrie, Wizard, Elf). */
#include "g4audio.h"
#include "g4music.h"
#include "g4extract.h"
static uint8_t *g2_voice; static long g2_voice_size; static int g2_voice_n, g2_voice_rate;
static void g2_voice_load(const char *path)
{
    FILE *f = fopen(path, "rb"); long n;
    if (!f) return;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    free(g2_voice); g2_voice = (uint8_t *)malloc((size_t)n);
    if (!g2_voice || fread(g2_voice, 1, (size_t)n, f) != (size_t)n || n < 8 || memcmp(g2_voice, "G2VO", 4)) {
        free(g2_voice); g2_voice = NULL; fclose(f); fprintf(stderr, "narration: %s is not a G2VO file\n", path); return;
    }
    fclose(f);
    g2_voice_size = n; g2_voice_n = g2_voice[4] << 8 | g2_voice[5]; g2_voice_rate = g2_voice[6] << 8 | g2_voice[7];
    fprintf(stderr, "narration: %d Gauntlet II phrases from %s\n", g2_voice_n, path);
}
/* ---- Gauntlet II attract screens (user, 2026-11-21): g2_attract.py runs the arcade's own attract
   code on the player's gauntlet2.zip and stores the title (colour-cycling logo), the demo game,
   LEGEND, MONSTERS, credits and SCORE PER COIN as a "G2AT" file (version 2, format: write_g2at() in
   g2_attract.py): the tile pictures from the player's ROM, then per frame the changes to the
   arcade's display memory. g2at_draw() draws a frame from that memory as the arcade's video does
   (playfield, motion objects on the SLIP chains, alpha layer; same rules as snap_idx() there).
   They play after Gauntlet 4's own attract loop (its SCORE PER COIN page), before its title. */
#define G2AT_STATE 0x5786                /* REGIONS in g2_attract.py: playfield 0x2000, motion objects
                                            0x2000, alpha 0xF00, SLIP 0x80, palette 0x800, bank 2, scroll 4 */
#define G2_PF 0x0000
#define G2_MO 0x2000
#define G2_AL 0x4000
#define G2_SLIP 0x4F00
#define G2_PAL 0x4F80
#define G2_BANK 0x5780
#define G2_SCROLL 0x5782
static uint8_t *g2at;                    /* the whole file */
static long g2at_size, g2at_first, g2at_pos;
static int g2at_w, g2at_h, g2at_frames, g2at_next, g2at_showing;
static const uint8_t *g2at_tiles, *g2at_chars;   /* 32 bytes each: 8x8, two pixels per byte */
static uint8_t g2at_mem[G2AT_STATE];
static uint32_t *g2at_rgb;
int mods_online;                         /* set by the host: online copies skip these screens */
static unsigned g2at_u16(long o) { return (unsigned)g2at[o] | (unsigned)g2at[o + 1] << 8; }
static uint32_t g2at_u32(long o) { return g2at_u16(o) | (uint32_t)g2at_u16(o + 2) << 16; }
static unsigned g2m(int o) { return (unsigned)g2at_mem[o] << 8 | g2at_mem[o + 1]; }   /* arcade word (big-endian) */
static void g2at_load(const char *path)
{
    FILE *f = fopen(path, "rb"); long n; int ns;
    if (!f) return;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    free(g2at); g2at = (uint8_t *)malloc((size_t)n); g2at_size = 0;
    if (!g2at || n < 18 || fread(g2at, 1, (size_t)n, f) != (size_t)n || memcmp(g2at, "G2AT", 4) || g2at_u16(4) != 2) {
        free(g2at); g2at = NULL; fclose(f);
        fprintf(stderr, "attract: %s is not a G2AT version 2 file (make it again with make_data)\n", path); return;
    }
    fclose(f);
    g2at_size = n; g2at_w = (int)g2at_u16(6); g2at_h = (int)g2at_u16(8); g2at_frames = (int)g2at_u16(10);
    ns = (int)g2at_u16(12); g2at_first = 14 + 4L * ns;
    if (g2at_w != 336 || g2at_h != 240 || g2at_first + 4 > n || g2at_u16(g2at_first) != 0x3000 || g2at_u16(g2at_first + 2) != 1024
        || g2at_first + 4 + 32L * (0x3000 + 1024) > n) { free(g2at); g2at = NULL; fprintf(stderr, "attract: %s: bad header\n", path); return; }
    g2at_tiles = g2at + g2at_first + 4; g2at_chars = g2at_tiles + 32L * 0x3000;
    g2at_first += 4 + 32L * (0x3000 + 1024);
    g2at_rgb = (uint32_t *)realloc(g2at_rgb, sizeof(uint32_t) * (size_t)(g2at_w * g2at_h));
    fprintf(stderr, "attract: %d Gauntlet II attract frames from %s\n", g2at_frames, path);
}
static void g2at_rewind(void)
{
    g2at_pos = g2at_first; g2at_next = 0;
    memset(g2at_mem, 0, sizeof g2at_mem);
}
static int g2_px(const uint8_t *t, int y, int x) { uint8_t b = t[y * 4 + (x >> 1)]; return x & 1 ? b & 15 : b >> 4; }
static uint32_t g2_rgb(unsigned w)                    /* arcade colour word: intensity, R, G, B (4 bits each) */
{
    unsigned i = ((w >> 12) & 15) * 17;
    return (uint32_t)(((i * ((w >> 8) & 15) * 17) >> 8) << 16 | ((i * ((w >> 4) & 15) * 17) >> 8) << 8 | ((i * (w & 15) * 17) >> 8));
}
static void g2at_draw(void)
{
    static uint16_t img[240][336]; static uint8_t seen[1024];
    int W = 336, H = 240, sx, sy, band, k;
    unsigned bank = g2m(G2_BANK) & 3, xs = g2m(G2_SCROLL), ys = g2m(G2_SCROLL + 2);
    uint32_t pal[1024];
    for (k = 0; k < 1024; k++) pal[k] = g2_rgb(g2m(G2_PAL + 2 * k));
    for (sy = 0; sy < H; sy++) {                           /* playfield, scrolled */
        int Y = (int)((sy + ys) & 511);
        for (sx = 0; sx < W; sx++) {
            int X = (int)((sx + xs) & 511);
            unsigned w = g2m(G2_PF + 2 * ((X >> 3) * 64 + (Y >> 3)));
            const uint8_t *t = g2at_tiles + 32 * (((bank * 0x1000 + (w & 0xFFF)) ^ 0x800) % 0x3000);
            img[sy][sx] = (uint16_t)((0x28 + ((w >> 12) & 7)) * 16 + g2_px(t, Y & 7, (w >> 15) ? 7 - (X & 7) : X & 7));
        }
    }
    for (band = 0; band < 64; band++) {                    /* motion objects: per 8-line band, its SLIP chain */
        int top = (int)((band * 8 - ys) & 511), chain[1024], n = 0, i;
        if (top >= H && top + 8 <= 512) continue;
        memset(seen, 0, sizeof seen);
        for (i = (int)(g2m(G2_SLIP + 2 * band) & 0x3FF); !seen[i] && n < 1024; i = (int)(g2m(G2_MO + 0x1800 + 2 * i) & 0x3FF)) {
            seen[i] = 1; chain[n++] = i;
        }
        while (n--) {                                      /* earlier in the list = in front (drawn last) */
            int e = chain[n], r, c;
            unsigned code = g2m(G2_MO + 2 * e) & 0x7FFF, w1 = g2m(G2_MO + 0x800 + 2 * e), w2 = g2m(G2_MO + 0x1000 + 2 * e);
            int color = (int)(w1 & 15), wt = (int)((w2 >> 3) & 7) + 1, ht = (int)(w2 & 7) + 1, flip = (int)((w2 >> 6) & 1);
            int x0 = (int)(w1 >> 7) - (int)xs, y0 = 511 - (int)(w2 >> 7) - ht * 8 - (int)ys;
            for (r = 0; r < ht * 8; r++) {
                int Y = (y0 + r) & 511;
                if (((Y - top) & 511) >= 8 || Y >= H) continue;
                for (c = 0; c < wt * 8; c++) {
                    int X = (x0 + c) & 511, cc = flip ? wt * 8 - 1 - c : c, v;
                    if (X >= W) continue;
                    v = g2_px(g2at_tiles + 32 * (((code + (unsigned)((r >> 3) * wt + (cc >> 3))) ^ 0x800) % 0x3000), r & 7, cc & 7);
                    if (v) img[Y][X] = (uint16_t)(0x100 + color * 16 + v);
                }
            }
        }
    }
    for (sy = 0; sy < 30; sy++)                            /* alpha layer on top */
        for (sx = 0; sx < 42; sx++) {
            unsigned w = g2m(G2_AL + 2 * (sy * 64 + sx)), colr = ((w >> 10) & 0x0F) | ((w >> 9) & 0x20);
            const uint8_t *ch = g2at_chars + 32 * (w & 0x3FF);
            int yy, xx;
            for (yy = 0; yy < 8; yy++)
                for (xx = 0; xx < 8; xx++) {
                    int v = g2_px(ch, yy, xx);
                    if (v || (w & 0x8000)) img[sy * 8 + yy][sx * 8 + xx] = (uint16_t)(colr * 4 + v);
                }
        }
    for (sy = 0; sy < H; sy++)
        for (sx = 0; sx < W; sx++) g2at_rgb[sy * W + sx] = pal[img[sy][sx] & 1023];
}
static int g2at_step(void)                             /* apply the next frame's changes and draw it; 0 at the end */
{
    long o = g2at_pos, end; unsigned k, n;
    if (g2at_next >= g2at_frames || o + 4 > g2at_size) return 0;
    end = o + 4 + (long)g2at_u32(o); o += 4;
    if (end > g2at_size || o + 2 > end) return 0;
    n = g2at_u16(o); o += 2;
    for (k = 0; k < n; k++) {
        unsigned st, len;
        if (o + 4 > end) return 0;
        st = g2at_u16(o); len = g2at_u16(o + 2); o += 4;
        if (st > G2AT_STATE || len > G2AT_STATE - st || o + (long)len > end) return 0;
        memcpy(g2at_mem + st, g2at + o, len); o += len;
    }
    g2at_draw();
    g2at_pos = end; g2at_next++;
    return 1;
}
const uint32_t *mods_g2_attract_frame(int *w, int *h)
{
    if (!g2at_showing) return NULL;
    *w = g2at_w; *h = g2at_h; return g2at_rgb;
}
/* 0x2369A, called at 0x2290C when Gauntlet 4 leaves its SCORE PER COIN page (0x228DA shows it, then
   waits 0x1C0 frames for $FFE795 (Start) or 0x23628's buttons & 6 in d1). If that wait ran out,
   play the Gauntlet II screens first, one per frame, watching the same inputs; a press ends them
   and is passed on in d1 / $FFE795 exactly as if it had come during G4's own wait. */
static void (*orig_attract_clear)(M68K *);
/* Player-supplied music (user, 2026-11-21; g4music.c): MP3 files in a "music" folder next to the
   level file, decoded the first time they are needed. No file = the game's own music, as before.
   Slots:
     g4_XX.mp3        Gauntlet 4's track XX (two hex digits, the number the game asks 0x1F664 for):
                      played instead of the Mega Drive music whenever the game starts that track, so
                      the game's own order is kept (00 title; 06-0A the level tunes, Arcade picks one
                      at random on level 1 and lets it run; G4MUSICLOG=1 lists the requests).
     g2_level.mp3     Gauntlet II levels (from level 1, then on through the levels),
     g2_treasure.mp3  Gauntlet II treasure rooms, g2_secret.mp3 secret rooms (the music changes only
                      on entering / leaving a room, user's rule),
     g2_attract.mp3   the Gauntlet II attract screens. */
static char music_dir[1024];
enum { MUS_G4 = 0, MUS_G2_LEVEL = 0x100, MUS_G2_TREASURE, MUS_G2_SECRET, MUS_G2_ATTRACT, MUS_SLOTS };
static music_track mus_tab[MUS_SLOTS]; static uint8_t mus_tried[MUS_SLOTS];
static int mus_cur = -1;                                 /* the slot playing as MP3, -1 none */
static const music_track *music_slot(int id)
{
    if (id < 0 || id >= MUS_SLOTS) return NULL;
    if (!mus_tried[id]) {
        static const char *g2n[] = { "g2_level", "g2_treasure", "g2_secret", "g2_attract" };
        char p[1200]; mus_tried[id] = 1;
        if (id < 0x100) snprintf(p, sizeof p, "%smusic/g4_%02X.mp3", music_dir, id);
        else snprintf(p, sizeof p, "%smusic/%s.mp3", music_dir, g2n[id - 0x100]);
        if (!music_decode(p, &mus_tab[id]) && id < 0x100) {   /* no MP3: the track recorded by --extract-music */
            snprintf(p, sizeof p, "%sg4_music.bin", music_dir);
            music_extracted(p, id, &mus_tab[id]);
        }
    }
    return mus_tab[id].pcm ? &mus_tab[id] : NULL;
}
static int music_mp3(int id)                             /* start slot id (looped) unless it is playing; 0 = no file */
{
    const music_track *m = music_slot(id);
    if (!m) return 0;
    if (mus_cur != id || !audio_music_playing())
        audio_music_play_ex(m->pcm, m->frames, m->channels, m->rate, m->loop, m->loop_at, m->gain ? m->gain : audio_music_gain);
    mus_cur = id;
    return 1;
}
static void music_mp3_stop(void) { audio_music_stop(); mus_cur = -1; }
static void mus_follow(int what)                       /* the game's own music commands (see mcmd0..5) */
{
    if (mus_cur < 0) return;
    if (what == 4) audio_music_pause(1);
    else if (what == 5) audio_music_pause(0);
    else if (what == 3) { audio_music_fade(53267); mus_cur = -1; }   /* about a second */
    else if (what == 1) music_mp3_stop();
}
static void attract_clear(M68K *c)
{
    if (g2at && !mods_online && !extract_music_active && rd32(c->a[7]) == 0x022912 && !rd8(0xFFE795) && !(c->d[1] & 6)) {
        uint32_t keep[14], d1 = 0; int i;
        for (i = 0; i < 8; i++) keep[i] = c->d[i];
        for (i = 0; i < 6; i++) keep[8 + i] = c->a[i];             /* never A6/A7 (see CLAUDE.md) */
        uint32_t sr = c->sr;
        g4_move_sr(c, (uint16_t)c->d[4]);              /* G4 masked interrupts at 0x22902 (old SR in d4): the
                                                           frames need its vblank (pads, Start flag, sound) */
        g2at_rewind(); g2at_showing = 1;
        music_mp3(MUS_G2_ATTRACT);
        while (g2at_step()) {
            JSR(c, 0x0FD030, 0x020496);                             /* one frame: the host shows our picture */
            JSR(c, 0x0FD030, 0x023628);                             /* the buttons, as G4's own wait loop */
            d1 = c->d[1] & 6;
            if (rd8(0xFFE795) || d1) break;
        }
        g2at_showing = 0;
        if (mus_cur == MUS_G2_ATTRACT) music_mp3_stop();
        g4_move_sr(c, (uint16_t)sr);
        for (i = 0; i < 8; i++) c->d[i] = keep[i];
        for (i = 0; i < 6; i++) c->a[i] = keep[8 + i];
        c->d[1] = (c->d[1] & ~0xFFu) | d1;
    }
    orig_attract_clear(c);
}
static void g2_say(int phrase)                         /* queue one phrase (after what is playing) */
{
    const uint8_t *e; uint32_t off, len;
    if (level_set_g3 || !g2_voice || phrase < 0 || phrase >= g2_voice_n) return;
    e = g2_voice + 8 + 8 * phrase;
    off = (uint32_t)e[0] << 24 | (uint32_t)e[1] << 16 | (uint32_t)e[2] << 8 | e[3];
    len = (uint32_t)e[4] << 24 | (uint32_t)e[5] << 16 | (uint32_t)e[6] << 8 | e[7];
    if ((long)off + (long)len > g2_voice_size) return;
    audio_voice_queue((const int8_t *)(g2_voice + off), len, (uint32_t)g2_voice_rate);
    if (getenv("G4VOICELOG")) fprintf(stderr, "narration: phrase %d (%u samples)\n", phrase, len);
}
/* The Gauntlet II sound effects (g2_sfx.bin, made by the launcher from the player's gauntlet2.zip:
   g2sfx.c runs the arcade sound board): effect n = sound-test number n. rate_pct 100 = as recorded. */
static uint8_t *g2_fxdata; static long g2_fxsize; static int g2_fxn, g2_fxrate;
static void g2_fx_load(const char *path)
{
    FILE *f = fopen(path, "rb"); long n;
    if (!f) return;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    g2_fxdata = (uint8_t *)malloc((size_t)n);
    if (!g2_fxdata || fread(g2_fxdata, 1, (size_t)n, f) != (size_t)n || n < 8 || memcmp(g2_fxdata, "G2FX", 4)) { free(g2_fxdata); g2_fxdata = NULL; fclose(f); return; }
    fclose(f);
    g2_fxsize = n; g2_fxn = g2_fxdata[4] << 8 | g2_fxdata[5]; g2_fxrate = g2_fxdata[6] << 8 | g2_fxdata[7];
    fprintf(stderr, "sound effects: %d Gauntlet II effects from %s\n", g2_fxn, path);
}
static void g2_fx_pitch(int n, int rate_pct)
{
    const uint8_t *e; uint32_t off, len;
    if (level_set_g3 || !g2_fxdata || n <= 0 || n >= g2_fxn) return;
    e = g2_fxdata + 8 + 8 * n;
    off = (uint32_t)e[0] << 24 | (uint32_t)e[1] << 16 | (uint32_t)e[2] << 8 | e[3];
    len = (uint32_t)e[4] << 24 | (uint32_t)e[5] << 16 | (uint32_t)e[6] << 8 | e[7];
    if (!len || (long)off + 2L * len > g2_fxsize) return;
    audio_sfx_play((const int16_t *)(g2_fxdata + off), len, (uint32_t)(g2_fxrate * rate_pct / 100));
    if (getenv("G4VOICELOG")) fprintf(stderr, "effect %d (%d%%)\n", n, rate_pct);
}
static void g2_fx(int n) { g2_fx_pitch(n, 100); }
static void g2_say_hero(int slot)                      /* "Red Warrior", "Blue Elf", ... */
{
    extern int16_t g4_hero_class_tab[4];
    int k = g4_hero_class_tab[slot & 3] >= 0 ? g4_hero_class_tab[slot & 3] & 3 : slot & 3;
    g2_say(163 + 4 * (slot & 3) + k);
}

int mods_load_g2(const char *path)
{
    FILE *f = fopen(path, "rb"); long n;
    if (!f) { fprintf(stderr, "level set: cannot open %s\n", path); return 0; }
    n = (long)fread(g4_mem + G2_BASE, 1, 0x80000, f);
    fclose(f);
    if (n < 72 || memcmp(g4_mem + G2_BASE, "G4L2", 4)) { fprintf(stderr, "level set: %s is not a G4L2 file\n", path); return 0; }
    g2_count = rd16(G2_BASE + 4); g2_end = G2_BASE + (uint32_t)n;
    g2_versions = rd16(G2_BASE + 6) >= 2 ? 4 : 1;         /* version 2: four mirror versions per maze */
    lset[0].end = g2_end; lset[0].count = g2_count; lset[0].versions = g2_versions;
    fprintf(stderr, "level set: %d Gauntlet II mazes from %s\n", g2_count, path);
    {   /* Gauntlet II pictures (g2_sprites.py), next to the level file: optional */
        char sp[1024]; const char *slash = strrchr(path, '/'), *bs = strrchr(path, '\\');
        int dir = (int)((bs && (!slash || bs > slash) ? bs : slash) ? ((bs && (!slash || bs > slash) ? bs : slash) - path + 1) : 0);
        snprintf(sp, sizeof sp, "%.*sg2_sprites.bin", dir, path);
        g2_sprites_load(sp);
        snprintf(sp, sizeof sp, "%.*sg2_speech.bin", dir, path);
        g2_voice_load(sp);
        snprintf(sp, sizeof sp, "%.*sg2_sfx.bin", dir, path);
        g2_fx_load(sp);
        snprintf(sp, sizeof sp, "%.*sg2_attract.bin", dir, path);
        g2at_load(sp);
        snprintf(music_dir, sizeof music_dir, "%.*s", dir, path);
    }
    return 1;
}
uint32_t mods_g2_size(void) { return lset[0].end ? lset[0].end - lset[0].base : 0; }
uint32_t mods_g3_size(void) { return lset[1].end ? lset[1].end - lset[1].base : 0; }
/* GAUNTLET III (user, 2026-11-22): gauntlet3_levels.bin (g2levels.c data_g3_levels: the Atari 8-bit
   Gauntlet's Deeper Dungeons disk plus the Atari's own mazes) in the same format, at 0x300000. Played
   like Gauntlet (the Gauntlet II extras stay off) plus poisoned food; header byte 6 = the number of
   ordinary mazes (the rest are treasure rooms). */
void mods_use_g3(void) { if (lset[1].count) use_set(1); }
int mods_load_g3(const char *path)
{
    FILE *f = fopen(path, "rb"); long n; uint32_t b = lset[1].base;
    if (!f) return 0;
    n = (long)fread(g4_mem + b, 1, 0x80000, f);
    fclose(f);
    if (n < 72 || memcmp(g4_mem + b, "G4L2", 4)) { fprintf(stderr, "level set: %s is not a G4L2 file\n", path); lset[1].count = 0; return 0; }
    lset[1].count = rd16(b + 4); lset[1].end = b + (uint32_t)n; lset[1].versions = (rd8(b + 7) >= 2) ? 4 : 1;
    lset[1].normal = rd8(b + 6);
    if (lset[1].normal < 8 || lset[1].normal > lset[1].count) lset[1].normal = lset[1].count;
    fprintf(stderr, "level set: %d GAUNTLET III mazes (%d treasure rooms) from %s\n", lset[1].count, lset[1].count - lset[1].normal, path);
    return 1;
}

static int is_g2_record(uint32_t rec) { return level_set_g2 && rec >= G2_BASE && rec < g2_end; }

static void (*orig_level_ptr)(M68K *);
static int g2_active;                    /* the current level is a Gauntlet II maze (not a treasure room) */
static long play_frame = -100;           /* last frame the game ran its play logic (0x3D9A tick) */
extern int in_pause_loop;                /* g4host.c: the pause loop is running */
/* Gauntlet II's maze order (Arcade): from the exit 0x52DB2 and the step routine 0x52ECA. Mazes
   1-5 in turn; then each level moves on 1 maze, plus a skip (0-7, $90400E) once past maze 5; maze 6's
   slot is the "continue" maze ($904010: where the last game ended, if it got past level 6);
   past maze 102 it wraps to maze 6, and each time the order comes round to that slot the skip grows.
   The exit to 6 leads to the continue maze. (Mazes 103-115 are not in the order -- 104 is the
   legend -- and 116/117 are Gauntlet II's treasure rooms.) Tracked from Gauntlet 4's level counter. */
/* treasure rooms (Gauntlet II): the level's secret-trick goal, per-hero counters, the trick timer,
   the room, the bonus multiplier -- see the section "Treasure rooms" */
static int tr_goal;                                  /* this maze's goal (0 none) */
static int tc_food_shot[4], tc_wall_shot[4], tc_transport[4], tc_inv[4], tc_keypot[4], tc_food[4], tc_treasure[4];
static int trick_hero = -1, trick_timer;             /* a touch-trick performed: exit within the time */
static int room_pending = -1;                        /* the hero who earned a room (-1 none) */
static int in_room, room_goal, room_timer, room_started, room_open, room_done, room_treas[4], room_walls, room_transports;
/* room_kind: 1 a secret room (mazes 116/117, earned by a secret trick), 2 a treasure room (mazes
   105-115, every 3-5 levels from level 9-11 on, see troom_tick). Treasure rooms (Gauntlet II
   0x44E92): when the countdown $904B80 runs out, the level is room $904018 (0x68-0x72,
   from 105); then $904018 += step + 1 (step $904016, 0-3), wrapping round past 115, and the step
   grows each time it comes back to 105. 20/24/25/26 s by players (0x57358, 0x44F2C), the maze's
   own exits; out in time: 100 x coins x treasures (0x4D578, confirmed on the user's 2017 stream),
   else "NO BONUS !!". Secret rooms: 5,000 x coins each (0x4D748, by elimination). */
static int room_kind, room_maze, troom_count = -1, troom_next = 0x68, troom_step;
static int mult[4] = {1, 1, 1, 1}; static uint32_t last_score[4];
static void trick_performed(int p, long frame);
static int tc_hit[4], pushes_left;       /* secret tricks 9 and 0x0F (see exit_goal_met) */
static int dragon_intro_done;            /* see dragon_tick */
/* Magic only STUNS super sorcerers (arcade legend), but Gauntlet 4's magic kills every monster in
   view: the ones alive when the magic starts are noted, and any it killed come back in the same
   square two frames later as a sorcerer still counted super, unable to fire for 300 frames. */
static int magic_saved_n, magic_saved_cell[48], magic_saved_k[48]; static long magic_frame = -1;
/* Poison (Gauntlet II 0x441A8): from level 3, at a maze's start, 24 in 64 times one extra item on a
   free square -- a magic potion or (half the time) poison, the skull flask 0x25ED. Touching it:
   -50 health (0x51C3E), "POISONED: YOU ARE DIZZY": disoriented 10-20 s (user) -- Gauntlet 4's
   CONFUSE TILE effect; eating ordinary food cures it at once (user). Shooting it: monsters slowed for 600 frames (0x4B8B0, "SHOOTING POISON SLOWS
   MONSTERS"; here they move every other frame). Carried by a Gauntlet 4 food cell (0x22). */
static uint32_t poison_obj; static int poison_cell = -1, slow_timer, dizzy[4], item_pending;
static struct { uint32_t obj; int cell; } g3_poison[64]; static int n_g3_poison, g3_poison_pending;   /* GAUNTLET III */
static int poison_gone(void);
static void tr_level_reset(void);
static void set_cell(M68K *c, int cell, int code);
static int g2o_idx, g2o_last_count, g2o_skip, g2o_continue = 5, g2o_exit6;
static void g2_order_step(void)
{
    int steps = 1 + (g2o_idx >= 5 ? g2o_skip : 0);
    while (steps-- > 0) {
        g2o_idx++;
        if (g2o_idx == 5) g2o_idx = g2o_continue;
        if (g2o_idx > 101) g2o_idx = 5;
    }
    if (g2o_idx == 5) g2o_skip = (g2o_skip + 1) & 7;
}
/* The treasure-room countdown at a level change (Gauntlet II, traced 2026-11-22): rooms only after
   level 6 (0x44DCA, secret rooms too). At the START of level 6 (also after an exit to 6) the
   countdown $904B80 = 3 + random(3) (0x438E4); at the end of each normal maze from level 6 on it
   goes down by one (0x4A760: the next level number $904B52 > 6, not in a treasure/secret room;
   an exit to 6 makes $904B52 = 6, so it does not count); a level after 6 that starts with it at 0
   is a treasure room (0x44E92) and it restarts at 3 + random(3). First room: level 9, 10 or 11.
   The port's level number is Gauntlet 4's counter; after an exit to 6 the arcade's would be 6.
   Returns 1 (so it can sit in the secret-room test); sets troom_due. G4TROOM=n (test aid): the
   countdown starts at n at level 1 and rooms may come from level 2. */
static int troom_due;
static int troom_tick(int count)
{
    int lvl = g2o_exit6 ? 6 : count, from = getenv("G4TROOM") ? 1 : 6;
    if (!in_room && lvl > from && troom_count > 0) troom_count--;   /* the maze just finished */
    if (lvl == 6 && from == 6) troom_count = 3 + rd16(0xFFE7E8) % 3;
    if (lvl > from && troom_count < 0) troom_count = 3 + rd16(0xFFE7E8) % 3;   /* started past level 6 */
    troom_due = lvl > from && troom_count == 0;
    if (lvl <= from) room_pending = -1;                  /* no secret room before level 7 either */
    return 1;
}
static int g2_music_room = -1;                         /* G2 levels: the room kind the music is for */
static int g2_order_maze(int count)                       /* maze number (1-based) for this level */
{
    if (count <= 1 || count < g2o_last_count) {           /* a new game */
        int q; for (q = 0; q < 4; q++) { mult[q] = 1; last_score[q] = 0; }
        in_room = 0; room_pending = -1; room_kind = 0;
        dragon_intro_done = 0;
        g2_music_room = -1;                               /* set again by the game's level-1 music */
        troom_count = getenv("G4TROOM") ? atoi(getenv("G4TROOM")) : -1;   /* set at level 6 (troom_tick) */
        troom_due = 0;
        if (g2o_last_count >= 6) g2o_continue = g2o_idx;  /* where the last game ended */
        g2o_idx = count == 8 ? 7 : 0;                     /* "start on level 8" keeps maze 8 */
        g2o_exit6 = 0;
    } else if (count == g2o_last_count && in_room) {     /* asked again for the same level */
        return room_maze;
    } else if (count > g2o_last_count && troom_tick(count) && room_pending >= 0) {   /* a secret room, then on as before */
        room_goal = 0x50 + rd16(0xFFE7E8) % 14;
        in_room = 1; room_kind = 1; room_pending = -1;
        room_maze = room_goal >= 0x57 ? 117 : 116;
        g2o_last_count = count;
        if (getenv("G4MODDEBUG")) fprintf(stderr, "level %d -> secret room (maze %d)\n", count, room_maze);
        return room_maze;
    } else if (count > g2o_last_count && troom_due) {     /* a treasure room, then on as before */
        troom_due = 0;
        troom_count = 3 + rd16(0xFFE7E8) % 3;
        in_room = 1; room_kind = 2; room_goal = 0;
        room_maze = troom_next + 1;
        troom_next += troom_step + 1;
        if (troom_next > 0x72) {
            troom_next = 0x68 + (troom_next - 0x72) - 1;
            if (troom_next == 0x68) troom_step = (troom_step + 1) & 3;
        }
        g2o_last_count = count;
        if (getenv("G4MODDEBUG")) fprintf(stderr, "level %d -> treasure room (maze %d; next in %d levels)\n", count, room_maze, troom_count);
        return room_maze;
    } else if (count > g2o_last_count) {
        in_room = 0; room_kind = 0;
        if (g2o_exit6) { g2o_idx = g2o_continue; g2o_exit6 = 0; }
        else { int d; for (d = g2o_last_count; d < count; d++) g2_order_step(); }
    }
    g2o_last_count = count;
    if (getenv("G4MODDEBUG")) fprintf(stderr, "level %d -> Gauntlet II maze %d (skip %d, continue %d)\n", count, g2o_idx + 1, g2o_skip, g2o_continue + 1);
    return g2o_idx + 1;
}

static int g2_level_features(void);
static uint8_t g2_cell[1024];
static uint32_t g2_cell_rec;
/* GAUNTLET III's maze order (the Atari program, $1245/$11FB): levels 1-7 play mazes 1-7; from level 8
   the others in a shuffled order ($0700, $1483), and a treasure room (a random one of the last mazes)
   when the countdown $12A6 (6 at the start; 4-7 after each room, counted down once per level from
   level 8) is 0: the first on level 14. Shuffled from the game's own random number (online: the same
   everywhere). */
static int g3_perm[256], g3_pos, g3_last, g3_cur, g3_count_down, g3_troom;
static int g3_order_maze(int count)
{
    int normal = lset[1].normal, n = lset[1].count, i;
    if (count == g3_last && g3_last) return g3_cur;    /* asked again for the same level */
    if (count <= 1 || count < g3_last || !g3_last) {      /* a new game */
        uint32_t s = (uint32_t)rd16(0xFFE7E8) * 2654435761u + rd16(0xFFE7E4) + 1;
        for (i = 0; i < normal - 7 && i < 256; i++) g3_perm[i] = 8 + i;
        for (i = normal - 8; i > 0; i--) { int j, t; s = s * 1103515245u + 12345u; j = (int)((s >> 16) % (uint32_t)(i + 1)); t = g3_perm[i]; g3_perm[i] = g3_perm[j]; g3_perm[j] = t; }
        g3_pos = 0; g3_count_down = 6; g3_last = 0;
        if (getenv("G4TROOM")) g3_count_down = atoi(getenv("G4TROOM"));   /* test aid */
    }
    if (count == g3_last) return g3_cur;                  /* asked again for the same level */
    g3_last = count; g3_troom = 0;
    if (count <= 7 && !getenv("G4TROOM")) g3_cur = count;
    else {
        if (g3_count_down <= 0 && n > normal) {
            g3_count_down = 4 + rd16(0xFFE7E8) % 4;
            g3_cur = normal + 1 + rd16(0xFFE7E8) % (n - normal); g3_troom = 1;
        } else g3_cur = normal > 7 ? g3_perm[g3_pos++ % (normal - 7)] : count;
        g3_count_down--;
    }
    in_room = g3_troom; room_kind = g3_troom ? 2 : 0; room_goal = 0;   /* the Atari's treasure room rules: room_tick */
    if (getenv("G4MODDEBUG")) fprintf(stderr, "level %d -> GAUNTLET III maze %d%s\n", count, g3_cur, g3_troom ? " (treasure room)" : "");
    return g3_cur;
}
/* Random rules (Gauntlet II 0x436FE). Keyed to the level number L (Gauntlet II $904004: +1 per
   exit) in mazes 6-102: r = L mod 400; r 104-200 wrap-around, 201-297 one random rule, 298-399
   both (no wrap in a maze with rule bit 0x04). Mazes 105-115: r = L mod 160; 41-80 wrap, 81-120
   "players can go off screen", 121-159 both. The random rule is table 0x57012[random(13)]:
   none, off screen (0x80; with bit 0x04 it becomes 0x02), shots stun others (0x01), shots hurt
   others (0x02), fake exits (0x40), and 8 rules with no message (0x10000-0x400000, 0x1000000).
   The 7 bits 0x10000 << k are FAST MONSTERS (0x40EFA: each picks the movement step of one kind
   of monster, 0x100 instead of 0x80 -- twice as fast; read by kind at 0x4126A); kind k assumed in
   Gauntlet II's code order: ghost, grunt, demon, lobber, sorcerer, super sorcerer, Death. Here
   that kind's Gauntlet 4 routine runs twice a frame (monster_step). Off screen 0x80: Gauntlet 4's own
   per-maze free-camera flag (record byte 7 bit 6), see level_ptr. 0x1000000 (it only patches the
   list's top bytes) acts as no rule. Test aids: G4RULELEVEL=L, G4RULEPICK=0..12. */
static uint32_t random_rule(int maze, int level, int no_wrap)
{
    static const uint32_t table[13] = {0, 0x80, 0x01, 0x02, 0x40, 0x1000000, 0x10000, 0x20000, 0x40000,
                                       0x80000, 0x100000, 0x200000, 0x400000};
    const char *fl = getenv("G4RULELEVEL"), *fp = getenv("G4RULEPICK");
    uint32_t pick, out = 0; int r;
    if (fl) level = atoi(fl);
    pick = table[fp ? atoi(fp) % 13 : (int)(((rd16(0xFFE7E4) >> 7) ^ (unsigned)level) % 13)];
    if (no_wrap && pick == 0x80) pick = 0x02;
    if (maze >= 6 && maze <= 102) {
        r = level % 400;
        if (r > 297) out = pick | (no_wrap ? 0 : 0x30);
        else if (r > 200) out = pick;
        else if (r > 103 && !no_wrap) out = 0x30;
    } else if (maze >= 105 && maze <= 115) {
        r = level % 160;
        out = r > 120 ? 0xB0 : r > 80 ? 0x80 : r > 40 ? 0x30 : 0;
    }
    out &= 0x01 | 0x02 | 0x40 | 0x30 | 0x80 | 0x7F0000; /* the ones the port has (0x1000000 -- no visible
                                                          effect found -- is dropped) */
    rnd_msg = (out & 0x01) ? "PLAYER SHOTS STUN OTHERS" : (out & 0x02) ? "PLAYER SHOTS HURT OTHERS" :
              (out & 0x40) ? "THERE ARE FAKE EXITS" : (out & 0x80) ? "PLAYERS CAN GO OFF SCREEN" : NULL;
    if (getenv("G4MODDEBUG")) fprintf(stderr, "maze %d level %d: random rule %X%s\n", maze, level, out, (out & 0x7F0000) ? " (fast monsters)" : "");
    return out;
}
static void g2_music_level(M68K *c, int kind);
static void level_ptr(M68K *c)                           /* 0x40D8 */
{
    play_frame = -10000;                                 /* a new level: its screen comes first */
    uint16_t lv, mode = rd16(0xFFE838);
    if (getenv("G4LEVELCOUNT") && rd16(0xFFE760) < atoi(getenv("G4LEVELCOUNT")))   /* test aid: start at level N */
        wr16(0xFFE760, (uint16_t)atoi(getenv("G4LEVELCOUNT")));
    lv = rd16(0xFFE758);
    orig_level_ptr(c);
    if (level_set_g3 && g2_count && mode == 0 && !rd8(0xFFE799)) lv = (uint16_t)g3_order_maze(rd16(0xFFE760));
    else if (level_set_g2 && g2_count && mode == 0 && !rd8(0xFFE799)) { lv = (uint16_t)g2_order_maze(rd16(0xFFE760)); g2_music_level(c, room_kind); }
    {   const char *ls = getenv("G4G2LIST");                /* test aid: level n plays the n-th maze of a list */
        if (ls && !in_room) {                                   /* (G4G2LIST=7,14,18: then round again) */
            int m[128], n = 0;
            while (*ls && n < 128) { m[n++] = atoi(ls); while (*ls && *ls != ',') ls++; if (*ls) ls++; }
            if (n) lv = (uint16_t)m[(rd16(0xFFE760) + n - 1) % n];
        }
    }
    {   const char *sh = getenv("G4G2SHIFT");               /* test aid: level n plays maze n + shift */
        if (sh && !in_room) lv = (uint16_t)(lv + atoi(sh)); }
    if (level_set_g2 && g2_count && (mode == 0 || mode == 3) && !rd8(0xFFE799) && lv >= 1 && lv <= g2_count) {
        /* Gauntlet II mirrors every maze at random each time it is played (0x436FE): one of the four
           versions in the level file, picked once per level from the game's own random number (the
           same on every machine online). G4MIRROR=0..3 forces one (test aid). */
        static int last_count = -1, last_lv = -1, version, fresh;
        static uint32_t patched; static uint8_t patched_opts;
        uint32_t rec;
        if (rd16(0xFFE760) != last_count || lv != last_lv) {
            const char *fm = getenv("G4MIRROR");
            last_count = rd16(0xFFE760); last_lv = lv; fresh = 1;
            version = fm ? atoi(fm) & 3 : (int)((rd16(0xFFE7E4) >> 4) & 3);
        }
        rec = G2_BASE + rd32(G2_BASE + 72 + 4u * (uint32_t)((lv - 1) * g2_versions + (g2_versions > 1 ? version : 0)));
        wr32(0xFFE75C, rec);
        g2_mirror = g2_versions > 1 ? version : 0;
        if (fresh) {
            fresh = 0;
            if (patched) { wr8(patched + 7, patched_opts); patched = 0; }   /* undo the last random wrap */
            g2_cell_rec = 0; g2_level_features();            /* this maze's extras (rule markers), fresh */
            rnd_rule = level_set_g3 ? 0 : random_rule(lv, last_count, g2_cell[11] == 33);
            if (rnd_rule & 0xB0) {                       /* wrap: record byte 7 bits 2/3; off screen: bit 6 =
                                                              Gauntlet 4's own free camera ($FFE7B5, set from it at
                                                              0x243E): the camera follows the group and nothing holds
                                                              a hero on screen (0x3F2C, 0x5D68...) -- as the arcade's
                                                              rule 0x80, which skips its own screen-edge checks */
                patched = rec; patched_opts = rd8(rec + 7);
                wr8(rec + 7, (uint8_t)(patched_opts | ((rnd_rule & 0x30) ? 0x0C : 0) | ((rnd_rule & 0x80) ? 0x40 : 0)));
            }
        }
    }
}

static int g2_level_features(void);
static uint8_t g2_cell[1024];
/* Invisible walls (Gauntlet II rules 0x800000 "ALL WALLS ARE INVISIBLE", mazes 6, 23, 101, and
   0x80000000 "TRAP WALLS ARE INVISIBLE", mazes 94, 97, 101, 115). Gauntlet II simply does not draw
   them (0x5EB2E, 0x5F048) nor their shadows (0x5E96C); they still block. Here the wall drawer
   0x19660 runs as usual (records, collision) and then the cell's four tiles are drawn as floor
   (as 0x19B7A does); the floor and wall drawers see hidden neighbours as floor, so no shadow or
   wall joint gives them away. */
static int inv_rule;                                   /* 1 all walls, 2 trap walls (cycling ones too) */
static int wall_hidden(int cell)
{
    int k = rd8(0xFFE246 + (uint32_t)cell);
    if (!inv_rule || !level_set_g2 || !is_g2_record(rd32(0xFFE75C))) return 0;
    if (inv_rule & 1) return k >= 0x09 && k < 0x13;
    return (k >= 0x0A && k <= 0x0C) || (k == 0x09 && g2_cell[cell] >= 10 && g2_cell[cell] <= 12);
}
static int hide_neighbours(int x, int y, int *cells, uint8_t *keep)   /* map them as floor for a moment */
{
    int dx, dy, n = 0;
    for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++) {
        int cell = ((y + dy) & 31) * 32 + ((x + dx) & 31);
        if ((dx || dy) && wall_hidden(cell)) { cells[n] = cell; keep[n] = rd8(0xFFE246 + (uint32_t)cell); wr8(0xFFE246 + (uint32_t)cell, 0); n++; }
    }
    return n;
}
static void (*orig_floor_draw)(M68K *), (*orig_wall_draw)(M68K *);
static void floor_draw(M68K *c)                        /* 0x19B7A(d6 x, d7 y) */
{
    int cells[8], n, i; uint8_t keep[8];
    if (!level_set_g2 || !g2_level_features() || !inv_rule) { orig_floor_draw(c); return; }
    n = hide_neighbours((int)(c->d[6] & 31), (int)(c->d[7] & 31), cells, keep);
    orig_floor_draw(c);
    for (i = 0; i < n; i++) wr8(0xFFE246 + (uint32_t)cells[i], keep[i]);
}
static void wall_draw(M68K *c)                         /* 0x19660(d6 x, d7 y): walls 09-12 */
{
    int x = (int)(c->d[6] & 31), y = (int)(c->d[7] & 31), cells[8], n, i, k, shade = 0; uint8_t keep[8];
    M68K save;
    if (!level_set_g2 || !g2_level_features() || !inv_rule) { orig_wall_draw(c); return; }
    n = hide_neighbours(x, y, cells, keep);
    orig_wall_draw(c);
    if (wall_hidden(y * 32 + x)) {                    /* draw floor over it, as 0x19B7A does */
        static const int nb[3][3] = {{-1, 0, 0x08}, {-1, 1, 0x10}, {0, 1, 0x20}};   /* its frame -$A, -$6, -$B */
        for (i = 0; i < 3; i++) {
            int kk = rd8(0xFFE246 + (uint32_t)(((y + nb[i][1]) & 31) * 32 + ((x + nb[i][0]) & 31)));
            if (kk >= 0x09 && kk < 0x13) shade |= nb[i][2];
        }
        save = *c;
        c->d[4] = (uint32_t)shade; c->d[0] = 0;
        JSR(c, 0x019BBC, 0x019D42);                    /* the floor variant (random in Arcade/Record) */
        shade = (int)(c->d[4] & 0xFFFF);
        for (k = 0; k < 4; k++) {
            c->d[6] = (uint32_t)(2 * x + (k & 1)); c->d[7] = (uint32_t)(2 * y + (k >> 1)); c->d[1] = 3;
            c->d[0] = (uint32_t)((rd16(0x019C42 + (uint32_t)shade + 2u * (uint32_t)k) + 0x34) & 0xFFFF);
            JSR(c, 0x019BD4, 0x01DFB6);
        }
        save.a[7] = c->a[7]; save.a[6] = c->a[6]; *c = save;
    }
    for (i = 0; i < n; i++) wr8(0xFFE246 + (uint32_t)cells[i], keep[i]);
}
static void (*orig_map_remap)(M68K *);
static void map_remap(M68K *c)                           /* 0x19340: cells at A4 through a table */
{
    uint32_t i, a = c->a[4];
    if (!is_g2_record(rd32(0xFFE75C))) { orig_map_remap(c); return; }
    for (i = 0; i < 0x400; i++) wr8(a + i, rd8(G2_BASE + 8 + rd8(a + i)));
    for (i = 32; i < 0x400; i++) if (g2_cell[i] == 18 && rd8(a + i) == 0x04) wr8(a + i, 0x06);   /* exit to 6: the
                                                       "EXIT TO 8" tile; the overlay turns its 8 into a 6 */
    if (g2_level_features()) {                           /* Gauntlet II maze rules on traps (0x439B0, 0x43BA0) */
        int r = rd16(0xFFE7E8), shift = g2_cell[2] == 17 ? r % 3 : 0, g = (r / 3) % 3, open = 0;
        if (shift)                                      /* rule 0x08: trap n opens group (n + shift) mod 3 */
            for (i = 0; i < 0x400; i++) {
                int t = rd8(a + i);
                if (t >= 0x14 && t <= 0x16) wr8(a + i, (uint8_t)(0x14 + (t - 0x14 + shift) % 3));
            }
        if (g2_cell[0] == 13) open |= 1 << g;            /* rule 0x1000: one group starts open */
        if (g2_cell[1] == 16) { int h = (r / 9) % 3; open |= 1 << h | 1 << (h + 1) % 3; }   /* 0x2000: two */
        for (i = 0; i < 0x400; i++) {
            int t = rd8(a + i);
            if ((t >= 0x0A && t <= 0x0C && open >> (t - 0x0A) & 1) || (t >= 0x14 && t <= 0x16 && open >> (t - 0x14) & 1))
                wr8(a + i, 0x00);
        }
        if (getenv("G4MODDEBUG")) fprintf(stderr, "maze rules: trap shift %d, open groups mask %d\n", shift, open);
    }
}

/* ------------------------------------------------------------------ Gauntlet II features run by the host */
/* Stun tiles and force fields: Gauntlet 4 has neither. The level file lists them per maze
   (version 1 "extras": 1 stun tile, 2 force-field post -- itself a wall in the map -- 3/4 cells
   of a horizontal/vertical beam). The cells are floor in Gauntlet 4's map; the host checks the
   players after each VBlank (pads read, logic not yet run) and draws the features itself.
   From Gauntlet II's code: a stun tile freezes the player for 120/45/120/60 frames (Warrior,
   Valkyrie, Wizard, Elf; 0x51304); a live beam costs 2/2/6/4 health (0x4AA84) with a 16-frame
   flash between hits; posts pair across open floor (0x53398). Approximated: the beams' on/off
   cycle (1 s each), re-stunning only after stepping off. Only game state is used, so every
   online copy does the same. */
                                         /* g2_cell: current maze, feature per cell (0 none) */
static uint32_t g2_cell_rec;             /* record they belong to */
static int stun_timer[4], ff_timer[4], on_stun[4];
/* amulets (Gauntlet II pickups 0x517BA-0x518CE): timers in frames, or for the rest of the level */
static int invuln[4], repulse[4], reflect[4], transport[4], super_shots[4];
static const int repulse_frames[4] = {908, 1208, 608, 908};
static const char *hero_name[4] = {"WARRIOR", "VALKYRIE", "WIZARD", "ELF"};
static char banner[48]; static long banner_until;
static int g2_give_pending;              /* test aid G4G2GIVE=5..9: player 1 gets that amulet once alive */
static long cur_frame, cycle_frame = -1; /* frame number from mods_after_vblank; last cycling step */
static int cycle_timer, cycle_phase, random_timer;
static int teleport_to[4];
/* acid puddles: pixel position, direction of travel (0 none), touch cool-down per hero */
static struct { int x, y, dx, dy, stun; } acid[64];   /* stun: frames left frozen by magic */
static int n_acid, acid_cool[4];
/* IT: objects chasing the heroes; it_player = the hero who is IT (-1 none) */
static struct { int x, y, dx, dy, stun; } it_obj[16];   /* stun: frames left, frozen by a shot */
static int n_it, it_player = -1, it_cool;
/* the thief / mugger: state 0 not yet, 1 hunting, 2 fleeing with loot, 3 gone */
static struct { int state, x, y, dx, dy, target, loot, mugger, timer; long play; } thief;
static int shot_flying[4];
static int drop_item;

/* exits (Gauntlet II rules 0x8000/0x4000/0x40): the exit spots, which is real, the move timer */
static short exit_pos[256]; static int n_exits, exit_real = -1, exit_timer, exits_done, exit_moves, fake_cool;
/* the dragon: bottom-left cell of its 2x2 block (-1 none), head hits left, breath cool-down */
static struct { int cell, hp, cool, placed, breath; uint32_t fb; int fb_x0, fb_y0; } dragon;   /* fb: its fireball's slot */
static int dragon_pic = 1, dragon_awake, dragon_aim, dragon_face;   /* the dragon's look (see dragon_look); face 0 W 1 E 2 S 3 N */
static int dragon_state, dragon_count, dragon_pat, dragon_step, dragon_turn, dragon_turn_to, dragon_stun;   /* see dragon_tick */                    /* (cell << 8 | code): an item for the game to place (needs its context) */
/* super sorcerers: the sorcerer objects (type 0x10) that started on marked cells, and a cool-down */
#define TINT_DX 0
#define TINT_DY 0
static uint32_t super_sorc[48]; static int super_cool[48], n_super, super_found;               /* the hero's shot has flown (super-shot counting) */               /* pending lone-transporter move: cell + 1 */     /* Gauntlet II $90401A / $90401C */
static const int stun_frames[4] = {120, 45, 120, 60}, ff_damage[4] = {2, 2, 6, 4};

static int g2_level_features(void)
{
    uint32_t rec = rd32(0xFFE75C); int k, n, i;
    if (!is_g2_record(rec) || rd8(G2_BASE + 7) < 1) return 0;
    if (rec == g2_cell_rec) return 1;
    memset(g2_cell, 0, sizeof g2_cell); g2_cell_rec = rec;
    for (k = 0; k < g2_count * g2_versions; k++)
        if (G2_BASE + rd32(G2_BASE + 72 + 4u * (uint32_t)k) == rec) {
            uint32_t ex = G2_BASE + rd32(G2_BASE + 72 + 4u * (uint32_t)(g2_count * g2_versions + k));
            n = rd16(ex);
            for (i = 0; i < n; i++) g2_cell[rd16(ex + 2 + 4u * (uint32_t)i) & 0x3FF] = rd8(ex + 4 + 4u * (uint32_t)i);
            break;
        }
    inv_rule = (g2_cell[9] == 31 ? 1 : 0) | (g2_cell[10] == 32 ? 2 : 0); g2_cell[9] = g2_cell[10] = 0;
    n_super = 0; super_found = 0;                         /* found on the first frame of play */
    memset(&thief, 0, sizeof thief);
    dragon.cell = -1; dragon.placed = 0;
    n_exits = 0; exit_real = -1; exits_done = 0; exit_moves = 0;
    tr_level_reset();
    n_acid = 0; memset(acid_cool, 0, sizeof acid_cool);
    n_it = 0; it_player = -1; it_cool = 0;
    for (i = 32; i < 1024; i++)
        if (g2_cell[i] == 21) {
            if (n_it < 16) { it_obj[n_it].x = (i & 31) * 16; it_obj[n_it].y = (i >> 5) * 16; it_obj[n_it].dx = it_obj[n_it].dy = 0; it_obj[n_it].stun = 0; n_it++; }
            g2_cell[i] = 0;
        }
    for (i = 32; i < 1024; i++)
        if (g2_cell[i] == 20) {
            if (n_acid < 64) { acid[n_acid].x = (i & 31) * 16; acid[n_acid].y = (i >> 5) * 16; acid[n_acid].dx = acid[n_acid].dy = 0; acid[n_acid].stun = 0; n_acid++; }
            g2_cell[i] = 0;
        }
    memset(stun_timer, 0, sizeof stun_timer); memset(ff_timer, 0, sizeof ff_timer); memset(on_stun, 0, sizeof on_stun);
    memset(reflect, 0, sizeof reflect); memset(transport, 0, sizeof transport);   /* these last one level */
    memset(super_shots, 0, sizeof super_shots); memset(shot_flying, 0, sizeof shot_flying);   /* so do super
                                         shots (user; G2 clears $905F68 at 0x48318) -- and IT (it_player) */
    cycle_timer = cycle_phase = 0; random_timer = 119;
    g2_give_pending = getenv("G4G2GIVE") != NULL;       /* test aid: see mods_after_vblank */
    return 1;
}

static long mods_overlay_calls, mods_overlay_hits;       /* debug (G4MODDEBUG) */
/* force-field beams (user's 2017 arcade footage): green, off, red, off -- about half a second each;
   lit beams fill their squares with crackling speckles, unlit ones are dark with faint dots */
static int beams_on(long frame) { return (frame / 30) % 2 == 0; }
static int beams_red(long frame) { return (frame / 60) % 2 == 1; }

/* Acid puddles (Gauntlet II 0x19, "ACID PUDDLES MOVE RANDOMLY"; shots cannot kill them). They
   wander over open floor a pixel a frame, turning at random at each cell. Touching one costs
   60 health and stuns the hero for a quarter of their stun-tile time (user, from the arcade), unless
   invulnerable; the puddle is used up. Randomness from the game's frame counter: the same in
   every online copy. */
#define ACID_DAMAGE 60
static const int acid_stun[4] = {30, 11, 30, 15};     /* stun tile 120/45/120/60 frames, / 4 */
static int acid_free(int cx, int cy)            /* open for puddles and IT: not a wall or door
                                                   (items on the floor do not block them) */
{
    int cell = (cy & 31) * 32 + (cx & 31), code;
    if (cell < 32) return 0;
    code = rd8(0xFFE246 + (uint32_t)cell);
    return !(code >= 0x07 && code <= 0x12);
}
static void acid_step(long frame)
{
    static const int dir[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int i, p;
    for (i = 0; i < n_acid; i++) {
        if (acid[i].stun > 0) { acid[i].stun--; continue; }   /* frozen by magic */
        if (!(acid[i].x & 15) && !(acid[i].y & 15)) {         /* on a cell: choose the next one */
            int cx = acid[i].x >> 4, cy = acid[i].y >> 4, k, r = (int)((frame * 7 + i * 13) & 0xFFFF), tries;
            r = r * 1103 + 12345;
            if (!acid[i].dx && !acid[i].dy) k = (r >> 4) & 3;
            else k = ((r >> 4) & 3) == 0 ? (r >> 6) & 3 : -1;   /* mostly keep going, sometimes turn */
            if (k < 0) { for (k = 0; k < 4; k++) if (dir[k][0] == acid[i].dx && dir[k][1] == acid[i].dy) break; }
            for (tries = 0; tries < 4 && !acid_free(cx + dir[k][0], cy + dir[k][1]); tries++) k = (k + 1) & 3;
            if (tries == 4) { acid[i].dx = acid[i].dy = 0; continue; }
            acid[i].dx = dir[k][0]; acid[i].dy = dir[k][1];
        }
        acid[i].x = (acid[i].x + acid[i].dx) & 0x1FF; acid[i].y = (acid[i].y + acid[i].dy) & 0x1FF;
    }
    for (p = 0; p < 4; p++) {
        uint32_t pl = 0xFF0040 + 0x2C4u * (uint32_t)p;
        int hp = (int16_t)rd16(pl + 0xB8);
        if (acid_cool[p] > 0) { acid_cool[p]--; continue; }
        if (hp <= 0) continue;
        for (i = 0; i < n_acid; i++) {
            int dx = (((int)rd16(pl + 2) - acid[i].x) + 256) & 511, dy = (((int)rd16(pl + 4) - acid[i].y) + 256) & 511, k;
            dx -= 256; dy -= 256;
            if (dx <= -12 || dx >= 12 || dy <= -12 || dy >= 12) continue;
            if (acid[i].stun > 0) continue;                    /* a frozen puddle does no harm (assumed) */
            if (!invuln[p]) {
                hp -= ACID_DAMAGE; if (hp < 0) hp = 0; wr16(pl + 0xB8, (uint16_t)hp);
                g2_fx(54);                               /* acid (sound test 054) */
                if (stun_timer[p] < acid_stun[p]) stun_timer[p] = acid_stun[p];
            }
            if (tr_goal == 1) trick_performed(p, frame);
            acid[i] = acid[--n_acid];                          /* the puddle is used up */
            (void)k;
            if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld acid: player %d health %d\n", frame, p, hp);
            break;
        }
    }
}

/* IT (Gauntlet II 0x1B, touch 0x4967A: the toucher becomes IT ($9049DC), is held 16 frames, the
   IT object goes; "MONSTERS FOLLOW PLAYER WHO IS IT"). The IT object heads for the nearest hero a
   pixel a frame over open floor. While a hero is IT every monster chases that hero (monster_step).
   With several heroes, IT passes on by touching another (tag) -- confirmed by the user. Lasts until
   the level ends. */
static int hero_alive(int p) { return (int16_t)rd16(0xFF0040 + 0x2C4u * (uint32_t)p + 0xB8) > 0; }
static int wrapd(int a, int b) { int d = ((a - b) + 256) & 511; return d - 256; }
static void become_it(int p, long frame)
{
    static const char *msg = "%s IS IT";
    it_player = p; stun_timer[p] = 16;
    g2_fx(53);                                           /* touches IT (sound test 053) */
    snprintf(banner, sizeof banner, msg, hero_name[p]); banner_until = frame + 150;
    g2_say_hero(p); g2_say(186);                       /* narration: "... is now IT" */
    if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld player %d is IT\n", frame, p);
}
static short it_dist[1024];
static void it_paths(void)                     /* distances (in cells) from the heroes over open floor */
{
    static short queue[1024];
    int head = 0, tail = 0, p, k;
    for (k = 0; k < 1024; k++) it_dist[k] = 0x7FFF;
    for (p = 0; p < 4; p++) {
        uint32_t pl = 0xFF0040 + 0x2C4u * (uint32_t)p; int cell;
        if (!hero_alive(p)) continue;
        cell = (((rd16(pl + 4) + 8) >> 4) & 31) * 32 + (((rd16(pl + 2) + 8) >> 4) & 31);
        if (it_dist[cell]) { it_dist[cell] = 0; queue[tail++] = (short)cell; }
    }
    while (head < tail) {
        int c = queue[head++], cx = c & 31, cy = c >> 5;
        static const int d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (k = 0; k < 4; k++) {
            int nx = (cx + d4[k][0]) & 31, ny = (cy + d4[k][1]) & 31, n = ny * 32 + nx;
            if (it_dist[n] != 0x7FFF || !acid_free(nx, ny)) continue;
            it_dist[n] = (short)(it_dist[c] + 1); queue[tail++] = (short)n;
        }
    }
}
static void it_step(long frame)
{
    static const int dir[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int i, p, q;
    if (n_it) it_paths();
    for (i = 0; i < n_it; i++) {
        if (it_obj[i].stun > 0) { it_obj[i].stun--; continue; }   /* shot: stunned (arcade legend) */
        if (!(it_obj[i].x & 15) && !(it_obj[i].y & 15)) {     /* on a cell: next step on the shortest
                                                                 path to the nearest hero */
            int cx = it_obj[i].x >> 4, cy = it_obj[i].y >> 4, k, best = -1, bestd = 0x7FFF;
            for (k = 0; k < 4; k++) {
                int nx = (cx + dir[k][0]) & 31, ny = (cy + dir[k][1]) & 31, d = it_dist[ny * 32 + nx];
                if (d < bestd) { bestd = d; best = k; }
            }
            if (best < 0 || bestd == 0x7FFF) {                /* no way to a hero: wander */
                int r = (int)((frame * 7 + i * 29) & 0xFFFF) * 1103 + 12345, tries;
                k = (r >> 4) & 3;
                if ((it_obj[i].dx || it_obj[i].dy) && ((r >> 7) & 3)) {
                    for (k = 0; k < 4; k++) if (dir[k][0] == it_obj[i].dx && dir[k][1] == it_obj[i].dy) break;
                }
                for (tries = 0; tries < 4 && !acid_free(cx + dir[k & 3][0], cy + dir[k & 3][1]); tries++) k++;
                if (tries == 4) { it_obj[i].dx = it_obj[i].dy = 0; continue; }
                best = k & 3;
            }
            it_obj[i].dx = dir[best][0]; it_obj[i].dy = dir[best][1];
        }
        it_obj[i].x = (it_obj[i].x + it_obj[i].dx) & 0x1FF; it_obj[i].y = (it_obj[i].y + it_obj[i].dy) & 0x1FF;
    }
    for (p = 0; p < 4; p++) {                                  /* touching an IT object */
        uint32_t pl = 0xFF0040 + 0x2C4u * (uint32_t)p;
        if (!hero_alive(p)) continue;
        for (i = 0; i < n_it; i++)
            if (abs(wrapd(rd16(pl + 2), it_obj[i].x)) < 12 && abs(wrapd(rd16(pl + 4), it_obj[i].y)) < 12) {
                it_obj[i] = it_obj[--n_it]; become_it(p, frame); break;
            }
    }
    if (it_cool > 0) it_cool--;
    else if (it_player >= 0 && hero_alive(it_player)) {         /* tag: IT passes to a hero touched */
        uint32_t a = 0xFF0040 + 0x2C4u * (uint32_t)it_player;
        for (q = 0; q < 4; q++) {
            uint32_t b = 0xFF0040 + 0x2C4u * (uint32_t)q;
            if (q == it_player || !hero_alive(q)) continue;
            if (abs(wrapd(rd16(a + 2), rd16(b + 2))) < 16 && abs(wrapd(rd16(a + 4), rd16(b + 4))) < 16) {
                become_it(q, frame); it_cool = 60; break;
            }
        }
    }
    if (it_player >= 0 && !hero_alive(it_player)) it_player = -1;
    if (getenv("G4MODDEBUG") && frame % 60 == 0)
        for (i = 0; i < n_it; i++) fprintf(stderr, "f%ld IT %d at (%d,%d)+(%d,%d) dir %d,%d\n", frame, i, it_obj[i].x >> 4, it_obj[i].y >> 4, it_obj[i].x & 15, it_obj[i].y & 15, it_obj[i].dx, it_obj[i].dy);
}

/* The thief (Gauntlet II: one roaming figure, $904BA0; 0x4E1FE on contact). From the code: it
   goes for the hero with the most valuable belongings and steals an amulet if one is held, else a
   potion or a key (potions x 3 against keys x 2; 0x4E288); a mugger (flag 0x80) takes 100 health
   instead ("KILL MUGGER TO ..."); "KILL THIEF TO RECOVER STOLEN ITEM", "THIEF ESCAPED".
   Thieves keep coming, one after another, any number per level (user). Assumed (not traced): the
   next comes after 30 s of play, 12+ cells from every hero,
   moves 2 px a frame, flees for 10 s with its loot, dies to one shot (dropping the loot), and is
   a mugger from level 50 on. */
static short thief_dist[1024];
static void bfs_from(short *dist, int only)          /* distances from the living heroes (or one) */
{
    static short queue[1024];
    static const int d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int head = 0, tail = 0, p, k;
    for (k = 0; k < 1024; k++) dist[k] = 0x7FFF;
    for (p = 0; p < 4; p++) {
        uint32_t pl = 0xFF0040 + 0x2C4u * (uint32_t)p; int cell;
        if (!hero_alive(p) || (only >= 0 && p != only)) continue;
        cell = (((rd16(pl + 4) + 8) >> 4) & 31) * 32 + (((rd16(pl + 2) + 8) >> 4) & 31);
        if (dist[cell]) { dist[cell] = 0; queue[tail++] = (short)cell; }
    }
    while (head < tail) {
        int c = queue[head++], cx = c & 31, cy = c >> 5;
        for (k = 0; k < 4; k++) {
            int nx = (cx + d4[k][0]) & 31, ny = (cy + d4[k][1]) & 31, n = ny * 32 + nx;
            if (dist[n] != 0x7FFF || !acid_free(nx, ny)) continue;
            dist[n] = (short)(dist[c] + 1); queue[tail++] = (short)n;
        }
    }
}
static int hero_wealth(int p)
{
    uint32_t pl = 0xFF0040 + 0x2C4u * (uint32_t)p;
    int w = 3 * rd16(pl + 0xCE) + 2 * rd16(pl + 0xCC);
    if (invuln[p] || super_shots[p] || reflect[p] || transport[p] || repulse[p]) w += 100;
    return w;
}
static const char *thief_take(int p)                   /* returns what was taken (NULL: nothing) */
{
    uint32_t pl = 0xFF0040 + 0x2C4u * (uint32_t)p;
    int pot = rd16(pl + 0xCE), key = rd16(pl + 0xCC);
    if (invuln[p]) { invuln[p] = 0; thief.loot = 9; return "INVULNERABILITY"; }
    if (super_shots[p]) { super_shots[p] = 0; thief.loot = 8; return "SUPER SHOTS"; }
    if (reflect[p]) { reflect[p] = 0; thief.loot = 6; return "REFLECTING SHOTS"; }
    if (transport[p]) { transport[p] = 0; thief.loot = 7; return "TRANSPORTABILITY"; }
    if (repulse[p]) { repulse[p] = 0; thief.loot = 5; return "REPULSIVENESS"; }
    if (pot && pot * 3 >= key * 2) { wr16(pl + 0xCE, (uint16_t)(pot - 1)); thief.loot = 0x23; return "A POTION"; }
    if (key) { wr16(pl + 0xCC, (uint16_t)(key - 1)); thief.loot = 0x1B; return "A KEY"; }
    thief.loot = 0; return NULL;
}
static void thief_step(long frame)
{
    static const int dir[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int p, i, step;
    if (thief.state == 0) {                               /* waiting to come */
        if (++thief.play < (getenv("G4THIEFAT") ? atoi(getenv("G4THIEFAT")) : 1800)) return;   /* test aid */
        bfs_from(thief_dist, -1);
        for (i = 0; i < 1024; i++) {                      /* a reachable cell far from everyone */
            int c = (int)((frame * 37 + i * 101) & 1023);
            if (c >= 32 && thief_dist[c] >= 12 && thief_dist[c] < 0x7FFF && rd8(0xFFE246 + (uint32_t)c) == 0 && !rd32(0xFFC246 + 4u * (uint32_t)c)) {
                int best = -1, bw = -1;
                for (p = 0; p < 4; p++) if (hero_alive(p) && hero_wealth(p) > bw) { bw = hero_wealth(p); best = p; }
                thief.state = 1; thief.x = (c & 31) * 16; thief.y = (c >> 5) * 16; thief.dx = thief.dy = 0;
                thief.target = best; thief.mugger = rd16(0xFFE758) >= 50; thief.timer = 0;
                snprintf(banner, sizeof banner, thief.mugger ? "A MUGGER IS ABOUT" : "A THIEF IS ABOUT"); banner_until = frame + 150;
                if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld thief appears at (%d,%d), after player %d\n", frame, c & 31, c >> 5, best);
                return;
            }
        }
        thief.play = 1500;                                /* try again in 5 s */
        return;
    }
    if (thief.state == 3) { thief.state = 0; thief.play = 0; return; }   /* gone: another comes later
                                                                         (any number per level, user) */
    if (thief.state != 1 && thief.state != 2) return;
    if (getenv("G4MODDEBUG") && frame % 60 == 0) fprintf(stderr, "f%ld thief state %d at (%d,%d)+(%d,%d)\n", frame, thief.state, thief.x >> 4, thief.y >> 4, thief.x & 15, thief.y & 15);
    if (thief.state == 1 && (thief.target < 0 || !hero_alive(thief.target))) {
        int bw = -1; thief.target = -1;
        for (p = 0; p < 4; p++) if (hero_alive(p) && hero_wealth(p) > bw) { bw = hero_wealth(p); thief.target = p; }
        if (thief.target < 0) return;
    }
    for (step = 0; step < 2; step++) {                    /* 2 px a frame */
        if (!(thief.x & 15) && !(thief.y & 15)) {
            int cx = thief.x >> 4, cy = thief.y >> 4, k, best = -1, bestd;
            bfs_from(thief_dist, thief.state == 1 ? thief.target : -1);
            bestd = thief.state == 1 ? 0x7FFF : -1;
            for (k = 0; k < 4; k++) {
                int nx = (cx + dir[k][0]) & 31, ny = (cy + dir[k][1]) & 31, d = thief_dist[ny * 32 + nx];
                if (!acid_free(nx, ny) || d == 0x7FFF) continue;
                if (thief.state == 1 ? d < bestd : d > bestd) { bestd = d; best = k; }
            }
            if (best < 0) { thief.dx = thief.dy = 0; break; }
            thief.dx = dir[best][0]; thief.dy = dir[best][1];
        }
        thief.x = (thief.x + thief.dx) & 0x1FF; thief.y = (thief.y + thief.dy) & 0x1FF;
    }
    for (p = 0; p < 4; p++) {                             /* shot by a hero: it dies, the loot drops */
        uint32_t pl = 0xFF0040 + 0x2C4u * (uint32_t)p;
        if (!rd8(pl + 0x69) || (int16_t)rd16(pl + 0x76) < 1) continue;
        if (abs(wrapd(rd16(pl + 0x6C) >> 6, thief.x)) < 12 && abs(wrapd(rd16(pl + 0x6E) >> 6, thief.y)) < 12) {
            int cell = (((thief.y + 8) >> 4) & 31) * 32 + (((thief.x + 8) >> 4) & 31);
            if (thief.state == 2 && thief.loot) {
                if (thief.loot >= 5 && thief.loot <= 9) g2_cell[cell] = (uint8_t)thief.loot;   /* an amulet to pick up */
                else drop_item = (cell << 8) | thief.loot;                                      /* placed by the game */
            }
            if (thief.state == 2 && thief.mugger && thief.loot == 100 && hero_alive(thief.target)) {
                uint32_t v = 0xFF0040 + 0x2C4u * (uint32_t)thief.target;
                wr16(v + 0xB8, (uint16_t)((int16_t)rd16(v + 0xB8) + 100));
            }
            snprintf(banner, sizeof banner, thief.mugger ? "MUGGER KILLED" : "THIEF KILLED"); banner_until = frame + 120;
            thief.state = 3;
            if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld thief shot by player %d\n", frame, p);
            return;
        }
    }
    if (thief.state == 1) {                               /* reaches a hero */
        for (p = 0; p < 4; p++) {
            uint32_t pl = 0xFF0040 + 0x2C4u * (uint32_t)p;
            if (!hero_alive(p)) continue;
            if (abs(wrapd(rd16(pl + 2), thief.x)) < 12 && abs(wrapd(rd16(pl + 4), thief.y)) < 12) {
                const char *what;
                thief.target = p;
                if (thief.mugger) {
                    int hp = (int16_t)rd16(pl + 0xB8) - 100; if (hp < 0) hp = 0;
                    if (!invuln[p]) wr16(pl + 0xB8, (uint16_t)hp);
                    thief.loot = 100; what = "100 HEALTH";
                } else what = thief_take(p);
                if (what) snprintf(banner, sizeof banner, "%s LOST %s", hero_name[p], what);
                else snprintf(banner, sizeof banner, "THE THIEF FOUND NOTHING");
                banner_until = frame + 150;
                thief.state = 2; thief.timer = 600;
                if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld thief robs player %d: %s\n", frame, p, what ? what : "nothing");
                break;
            }
        }
    } else if (--thief.timer <= 0) {
        snprintf(banner, sizeof banner, thief.mugger ? "MUGGER ESCAPED" : "THIEF ESCAPED"); banner_until = frame + 150;
        thief.state = 3;
    }
}

/* ------------------------------------------------------------------ Treasure rooms (Gauntlet II)
   From level 7 each maze has a secret-trick goal (header byte 0, set at 0x4393E). Some are met by a
   touch (1: an acid puddle, 2: Death; 0x50C30) -- then "YOU HAVE PERFORMED A SECRET TRICK / YOU HAVE
   nn SECONDS TO EXIT" (20/24/25/26 s by players, table 0x57358); the rest are judged at the exit
   (0x52B40): 5 shot food at most once, 6 shot secret walls at most once, 7 still 11 super shots,
   8 not "picked invulnerability and unhurt since", 0x0B no transporter used, 0x0C keys + potions,
   0x0D food eaten, 0x0E treasure -- in multiples of 8 / 4 / 8 (at least one: our reading), 0x10 being
   IT, 0x11 no shot at another player. The rest, from the hint table 0x597D8 (index = goal - 1;
   the NES version shows the same hints) and the code: 3/4 "TRY TRANSPORTABILITY" (here: go
   through a wall with the amulet -- condition not traced), 9 "DON'T GET HIT" (hits by other
   players' shots, a multiple of 4), 0x0A "TRY PUSHING A WALL" (a push, 0x42846), 0x0F "BE PUSHY"
   (two pushes, $904BF2). 0x0B's hint is "DON'T BE FOOLED" (kept as: no transporter used).
   The hero then goes to a treasure room (mazes 116/117 by its own condition 0x50 + random(14),
   0x44E0C): nn seconds (tables 0x57360/0x5737C) to meet the condition ("AFTER COLLECTING 6
   TREASURES" ...) -- then every wall becomes an exit (0x5E80C) -- and get out: BONUS = players x 100
   x coins (1 here) x treasures (0x4D578), else "NO BONUS !!". In Gauntlet 4 the whole party enters.
   Bonus multiplier (2+ players, 0x51A16): a treasure raises its taker's by 2 (cap 2 x players) and
   lowers every other hero's by 1; every score gain is multiplied by it (0x5214C). */
static const uint16_t room_base[14] = {1200, 1140, 1200, 1080, 900, 840, 1020, 960, 1320, 660, 1080, 1140, 960, 1260};
static const uint8_t room_rand[14] = {6, 4, 6, 4, 6, 4, 6, 4, 6, 5, 5, 9, 4, 5};
static const char *room_text(int g)
{
    switch (g) {
    case 0x50: return "COLLECT 6 TREASURES";
    case 0x51: case 0x5D: return "COLLECT ALL POTIONS";
    case 0x52: case 0x5B: return "SHOOT 3 SECRET WALLS";
    case 0x56: return "USE 5 TRANSPORTERS";
    case 0x5A: return "TAKE ALL THE TREASURE";
    case 0x5C: return "WHILE YOU ARE IT";
    }
    return NULL;
}
static int players_in(void) { int p, n = 0; for (p = 0; p < 4; p++) n += hero_alive(p); return n ? n : 1; }
static int goal_active(void)
{
    int count = rd16(0xFFE760);
    if (!tr_goal || in_room || (count <= 6 && !getenv("G4TRICKANY"))) return 0;   /* G4TRICKANY: test aid */
    if (tr_goal == 9 && count < 12) return 0;
    return 1;
}
static void trick_performed(int p, long frame)
{
    static const int exit_time[4] = {1200, 1440, 1500, 1560};
    if (!goal_active() || trick_hero >= 0 || room_pending >= 0) return;
    trick_hero = p; trick_timer = exit_time[players_in() - 1];
    snprintf(banner, sizeof banner, "SECRET TRICK! EXIT IN %d SECONDS", trick_timer / 60); banner_until = frame + 180;
    if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld secret trick by player %d (goal %d)\n", frame, p, tr_goal);
}
static int exit_goal_met(int p)
{
    switch (tr_goal) {
    case 5:    return tc_food_shot[p] <= 1;
    case 6:    return tc_wall_shot[p] <= 1;
    case 7:    return super_shots[p] >= 11;
    case 8:    return tc_inv[p] != 1;
    case 9:    return !(tc_hit[p] & 3);                /* "DON'T GET HIT": hits by other players' shots, a
                                                          multiple of 4 (none counts; 0x4B2A2, 0x52BE0) */
    case 0x0F: return pushes_left <= 0;               /* "BE PUSHY": push walls (G2 $904BF2 from 2 down) */
    case 0x0B: return tc_transport[p] == 0;
    case 0x0C: return tc_keypot[p] > 0 && !(tc_keypot[p] & 7);
    case 0x0D: return tc_food[p] > 0 && !(tc_food[p] & 3);
    case 0x0E: return tc_treasure[p] > 0 && !(tc_treasure[p] & 7);
    case 0x10: return it_player == p;
    case 0x11: return 1;                               /* no player-shoots-player in Gauntlet 4 */
    }
    return 0;
}
/* Shots between players (Gauntlet II rule bits 0x01 / 0x02, 0x4B06A): a hero's shot that reaches
   another hero (not invulnerable) stuns them 40 frames more, at most 90 ("PLAYER SHOTS STUN
   OTHERS"), or takes 2 health ("PLAYER SHOTS HURT OTHERS"); either way the shot ends there.
   The idle rule (G2 $9048C6, 0x5E80C): after 21000 frames with no shot fired and no score made,
   every wall becomes an exit, so a maze can never hold the players for ever. */
static int pvp_rule, idle_frames, idle_done, rule_say, tc_hit[4], pushes_left = 2;
static uint32_t idle_score;
static void room_open_exits(M68K *c);
static void idle_tick(M68K *c)
{
    int p; uint32_t total = 0, i;
    if (idle_done || in_room) return;
    for (p = 0; p < 4; p++) {
        uint32_t pl = 0xFF0040 + 0x2C4u * (uint32_t)p;
        total += rd32(pl + 0xC6);
        if (rd8(pl + 0x69)) idle_frames = 0;             /* a shot in the air */
    }
    if (total != idle_score) { idle_score = total; idle_frames = 0; }   /* a kill (or other points) */
    {   const char *t = getenv("G4IDLE");                  /* test aid: a shorter idle time, in frames */
        if (++idle_frames < (t ? atoi(t) : 21000)) return; }
    idle_done = 1;
    for (i = 32; i < 1024; i++) {
        int k = rd8(0xFFE246 + i);
        if (k >= 0x09 && k <= 0x0C) set_cell(c, (int)i, 0x04);
    }
    snprintf(banner, sizeof banner, "ALL WALLS ARE NOW EXITS"); banner_until = cur_frame + 150;
    if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld idle rule: walls -> exits\n", cur_frame);
}
static void tr_level_reset(void)                       /* a new maze */
{
    memset(tc_food_shot, 0, sizeof tc_food_shot); memset(tc_wall_shot, 0, sizeof tc_wall_shot);
    memset(tc_transport, 0, sizeof tc_transport); memset(tc_inv, 0, sizeof tc_inv);
    memset(tc_keypot, 0, sizeof tc_keypot); memset(tc_food, 0, sizeof tc_food); memset(tc_treasure, 0, sizeof tc_treasure);
    trick_hero = -1; trick_timer = 0;
    tr_goal = (g2_cell[6] & 0x80) ? (g2_cell[6] & 0x7F) : 0; g2_cell[6] = 0;
    pvp_rule = (g2_cell[7] == 29 ? 1 : 0) | (g2_cell[8] == 30 ? 2 : 0); g2_cell[7] = g2_cell[8] = 0;
    pvp_rule |= (int)(rnd_rule & 3);                   /* the random rule (level 201 on) */
    if (pvp_rule == 3) pvp_rule = 1;
    if ((rnd_rule & 0x40) && g2_cell[3] == 26) g2_cell[5] = 28;   /* fake exits: only where one exit is real */
    else if (rnd_rule & 0x40) rnd_msg = NULL;
    g2_cell[11] = 0;
    idle_frames = 0; idle_done = 0; rule_say = 1;
    item_pending = 1; poison_obj = 0; poison_cell = -1; slow_timer = 0; memset(dizzy, 0, sizeof dizzy);
    n_g3_poison = 0; g3_poison_pending = 1;
    memset(tc_hit, 0, sizeof tc_hit); pushes_left = 2;
    memset(room_treas, 0, sizeof room_treas); room_walls = room_transports = 0;
    room_started = room_open = room_done = 0;
}
static void treasure_taken(int p, long frame)          /* the multiplier (normal levels, 2+ players) */
{
    int q, n = players_in();
    if (in_room || n < 2) return;
    mult[p] += 2; if (mult[p] > 2 * n) mult[p] = 2 * n;
    for (q = 0; q < 4; q++) if (q != p && hero_alive(q) && mult[q] > 1) mult[q]--;
    snprintf(banner, sizeof banner, "%s SCORES x%d", hero_name[p], mult[p]); banner_until = frame + 120;
}
static void scale_scores(void)                         /* every score gain times the multiplier */
{
    int p;
    for (p = 0; p < 4; p++) {
        uint32_t pl = 0xFF0040 + 0x2C4u * (uint32_t)p, sc = rd32(pl + 0xC6);
        if (sc > last_score[p] && mult[p] > 1) { sc += (sc - last_score[p]) * (uint32_t)(mult[p] - 1); wr32(pl + 0xC6, sc); }
        last_score[p] = sc;
    }
}
static void room_open_exits(M68K *c)                   /* every wall becomes an exit (0x5E80C) */
{
    int i;
    room_open = 1;
    for (i = 32; i < 1024; i++) if (rd8(0xFFE246 + (uint32_t)i) == 0x09) set_cell(c, i, 0x04);
    snprintf(banner, sizeof banner, "THE WALLS ARE EXITS - GET OUT!"); banner_until = cur_frame + 120;
}
static int room_condition_met(void)
{
    int i, p, t = 0;
    switch (room_goal) {
    case 0x50: for (p = 0; p < 4; p++) t += room_treas[p]; return t >= 6;
    case 0x51: case 0x5D: for (i = 32; i < 1024; i++) { int k = rd8(0xFFE246 + (uint32_t)i); if (k == 0x23 || k == 0x24 || (k >= 0x26 && k <= 0x2B)) return 0; } return 1;
    case 0x52: case 0x5B: return room_walls >= 3;
    case 0x56: return room_transports >= 5;
    case 0x5A: for (i = 32; i < 1024; i++) { int k = rd8(0xFFE246 + (uint32_t)i); if (k == 0x1E || k == 0x1C) return 0; } return 1;
    case 0x5C: return it_player >= 0;
    }
    return 1;
}
static void room_tick(M68K *c)                         /* from play_tick, in a treasure room */
{
    const char *t;
    if (!room_started && room_kind == 2) {             /* a treasure room */
        static const int troom_time[4] = {1201, 1441, 1501, 1561};   /* 0x57358 + 1 */
        room_started = 1; room_open = 1;               /* its own exits: nothing to open */
        room_timer = troom_time[players_in() - 1];
        if (level_set_g3) {                            /* the Atari (0x3932): 20-30, from a table of 16 by a random
                                                          number, shown as seconds */
            static const uint8_t a8_time[16] = {20, 22, 24, 26, 28, 30, 21, 23, 25, 27, 29, 20, 21, 22, 23, 24};
            room_timer = a8_time[rd16(0xFFE7E8) & 15] * 60 + 1;
        }
        snprintf(banner, sizeof banner, "TREASURE ROOM: %d SECONDS", room_timer / 60);   /* fits the play area */
        banner_until = cur_frame + 180;
        g2_say(15);                                    /* narration: "welcome to the treasure room" */
        if (getenv("G4MODDEBUG")) fprintf(stderr, "treasure room, %d frames\n", room_timer);
        return;
    }
    if (!room_started) {
        room_started = 1;
        room_timer = room_base[room_goal - 0x50] + (rd16(0xFFE7E8) % room_rand[room_goal - 0x50]) * 60;
        t = room_text(room_goal);
        snprintf(banner, sizeof banner, t ? "SECRET ROOM: %d SECONDS - %s" : "SECRET ROOM: %d SECONDS", room_timer / 60, t);
        banner_until = cur_frame + 180;
        if (!t) room_open_exits(c);
        if (getenv("G4MODDEBUG")) fprintf(stderr, "secret room, goal %02X, %d frames\n", room_goal, room_timer);
        return;
    }
    if (room_done) return;
    if (!room_open && room_kind == 1 && room_condition_met()) room_open_exits(c);
    if (room_kind == 2 && room_timer == 301) {         /* 5 seconds left in a treasure room, sometimes (user) */
        static const short hurry[4] = {128, 129, 130, 131};   /* "better hurry!", "time's running out!",
                                                                  "time's on my side.", "can you make it?" */
        unsigned r = (unsigned)(rd16(0xFFE7E8) ^ (uint16_t)cur_frame) % 6u;
        if (r < 4) g2_say(hurry[r]);
    }
    if (--room_timer <= 0) {                           /* out of time: no bonus, the room ends */
        int p;
        room_done = 1;
        snprintf(banner, sizeof banner, "NO BONUS !!"); banner_until = cur_frame + 150;
        g2_say(127);                                   /* narration: "better luck next time" */
        for (p = 0; p < 4; p++)
            if (hero_alive(p)) {                       /* leave through the game's own exit */
                uint32_t pl = 0xFF0040 + 0x2C4u * (uint32_t)p; M68K save = *c;
                int cx = ((rd16(pl + 2) + 8) >> 4) & 31, cy = ((rd16(pl + 4) + 8) >> 4) & 31;
                c->a[0] = pl; c->d[1] = 0x10; c->d[4] = (uint32_t)cx; c->d[5] = (uint32_t)(cy << 5);
                JSR(c, 0x003D9A, 0x006BFE);
                save.a[7] = c->a[7]; save.a[6] = c->a[6]; *c = save;
                break;
            }
        return;
    }
    if (room_timer % 60 == 0 && cur_frame >= banner_until) {
        t = room_text(room_goal);
        snprintf(banner, sizeof banner, room_open || !t ? "%d SECONDS TO GET OUT" : "%d SECONDS - %s", room_timer / 60, t);
        banner_until = cur_frame + 61;
    }
}
static void room_exit(int p)                           /* a hero leaves the room in time */
{
    int t = room_treas[0] + room_treas[1] + room_treas[2] + room_treas[3];
    if (room_kind == 1) {                              /* secret room: 5,000 x coins for each hero who gets
                                                          out (0x4D748; one coin each here; the whole party
                                                          leaves together in Gauntlet 4) */
        int q;
        room_done = 1;
        for (q = 0; q < 4; q++) if (hero_alive(q)) {
            uint32_t ql = 0xFF0040 + 0x2C4u * (uint32_t)q;
            wr32(ql + 0xC6, rd32(ql + 0xC6) + 5000); last_score[q] = rd32(ql + 0xC6);
        }
        snprintf(banner, sizeof banner, "5,000 x COINS = 5000"); banner_until = cur_frame + 180;
        if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld player %d leaves the secret room in time\n", cur_frame, p);
        return;
    }
    if (level_set_g3) {                                /* GAUNTLET III (the Atari, 0x39AF): each hero who gets out in
                                                          time scores 100 for each treasure they took there */
        int q; uint32_t total = 0;
        room_done = 1;
        for (q = 0; q < 4; q++) if (hero_alive(q)) {
            uint32_t ql = 0xFF0040 + 0x2C4u * (uint32_t)q, b = 100u * (uint32_t)room_treas[q];
            wr32(ql + 0xC6, rd32(ql + 0xC6) + b); last_score[q] = rd32(ql + 0xC6); total += b;
        }
        if (total) snprintf(banner, sizeof banner, "TREASURES x%d  BONUS = %u", t, total);
        else snprintf(banner, sizeof banner, "NO BONUS !!");
        banner_until = cur_frame + 180;
        if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld the party leaves the treasure room: %d treasures, bonus %u\n", cur_frame, t, total);
        return;
    }
    /* treasure room: 100 x coins x treasures (0x4D578; seen on the user's 2017 stream: "100 x COINS
       5100 / TREASURES x 45 / BONUS = 229500"); coins = one per player here */
    uint32_t pl = 0xFF0040 + 0x2C4u * (uint32_t)p, bonus = (uint32_t)(players_in() * 100 * t);
    room_done = 1;
    wr32(pl + 0xC6, rd32(pl + 0xC6) + bonus); last_score[p] = rd32(pl + 0xC6);   /* not multiplied */
    if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld player %d leaves the treasure room: %d treasures, bonus %u\n", cur_frame, p, t, bonus);
    if (t) snprintf(banner, sizeof banner, "TREASURES x%d  BONUS = %u", t, bonus);
    else snprintf(banner, sizeof banner, "NO BONUS !!");
    banner_until = cur_frame + 180;
}

static void move_hero(uint32_t pl, int cell);
static void transport_step(int p);
static void class_fields(int slot);
static int lowhp_said[4], die_said[4];
void mods_after_vblank(long frame)
{
    int p;
    {   extern int16_t g4_hero_class_tab[4]; uint16_t m = rd16(0xFFE838);   /* Quest and Battle: no character select */
        if ((m == 1 || m == 2) && !getenv("G4CLASS")) for (p = 0; p < 4; p++) if (g4_hero_class_tab[p] >= 0) { g4_hero_class_tab[p] = -1; class_fields(p); } }
    if (getenv("G4JUMPAT") && frame >= atol(getenv("G4JUMPAT")) && (frame - atol(getenv("G4JUMPAT"))) % 300 == 0 && g2_active) {   /* test aid: player 1 jumps 3 cells, every 5 s */
        uint32_t q = 0xFF0040; int cx = ((rd16(q + 2) + 8) >> 4) & 31, cy = ((rd16(q + 4) + 8) >> 4) & 31, k;
        static const int dd[4][2] = {{3, 0}, {-3, 0}, {0, 3}, {0, -3}};   /* onto free floor, as a teleport lands */
        for (k = 0; k < 4; k++) {
            int nx = cx + dd[k][0], ny = cy + dd[k][1], cell = ny * 32 + nx;
            if (nx < 1 || nx > 30 || ny < 1 || ny > 31) continue;
            if (rd8(0xFFE246 + (uint32_t)cell) || rd32(0xFFC246 + 4u * (uint32_t)cell)) continue;
            move_hero(q, cell);
            if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld test jump to (%d,%d)\n", frame, nx, ny);
            break;
        }
    }
    if (getenv("G4HINTAT")) {                          /* test aid: a hint-box pause at that frame, and a log */
        long at = atol(getenv("G4HINTAT"));
        if (frame == at) wr16(0xFFE7EA, 200);
        if (frame >= at - 20 && frame <= at + 240 && frame % 20 == 0)
            fprintf(stderr, "f%ld hint %d acid0 %d,%d obj %04X,%04X\n", frame, rd16(0xFFE7EA), n_acid ? acid[0].x : -1, n_acid ? acid[0].y : -1, rd16(0xFF0040 + 2), rd16(0xFF7002));
    }
    if (getenv("G4MODDEBUG") && frame % 60 == 0) {
        int i, n[5] = {0, 0, 0, 0, 0};
        for (i = 0; i < 1024; i++) if (g2_cell[i] < 5) n[g2_cell[i]]++;
        fprintf(stderr, "f%ld rec %06X g2 %d: stun %d post %d beam %d/%d; overlay calls %ld feature px %ld; ba reg2 %02X\n",
                frame, rd32(0xFFE75C), is_g2_record(rd32(0xFFE75C)), n[1], n[2], n[3], n[4], mods_overlay_calls, mods_overlay_hits, 0);
        mods_overlay_calls = mods_overlay_hits = 0;
    }
    cur_frame = frame;
    g2_active = mods_enabled && level_set_g2 && g2_level_features();
    if (getenv("G4SELLOG") && frame >= atol(getenv("G4SELLOG")) && frame <= atol(getenv("G4SELLOG")) + 400 && frame % 20 == 0) {   /* test aid: hero selection */
        int r; fprintf(stderr, "f%ld 79b=%d", frame, rd8(0xFFE79B));
        for (r = 0; r < 4; r++) { uint32_t q = rd32(0xFFEAFC + 4u * (uint32_t)r);
            fprintf(stderr, " | %06X b4=%d d4=%d d0=%d da=%d 22=%02X 24=%02X", q, rd16(q + 0xB4), rd16(q + 0xD4), rd16(q + 0xD0), rd16(q + 0xDA), rd8(q + 0x22), rd8(q + 0x24)); }
        fprintf(stderr, "\n");
    }
    if (getenv("G4POSLOG") && frame % 50 == 0)           /* test aid: player 1's position */
        fprintf(stderr, "f%ld p1 at %d,%d (cell %d,%d)\n", frame, rd16(0xFF0042), rd16(0xFF0044), rd16(0xFF0042) >> 4, rd16(0xFF0044) >> 4);
    if (!g2_active) return;
    if (n_acid && play_frame == frame - 1 && !in_pause_loop) acid_step(frame);   /* only while the maze is played */
    if ((n_it || it_player >= 0) && play_frame == frame - 1 && !in_pause_loop) it_step(frame);
    /* (the host-drawn Gauntlet II thief, thief_step, is retired: Gauntlet 4's own thief is used --
       see g4_thief_tick) */
    if (play_frame == frame - 1 && !in_pause_loop && trick_timer > 0 && --trick_timer == 0) trick_hero = -1;   /* too late */
    if (tr_goal == 2 && goal_active())                   /* goal 2: fight Death (touch it) */
        for (p = 0; p < 4; p++) {
            uint32_t pl = 0xFF0040 + 0x2C4u * (uint32_t)p; int k;
            if (!hero_alive(p)) continue;
            for (k = 0; k < 9; k++) {
                int cx = ((rd16(pl + 2) >> 4) + k % 3 - 1) & 31, cy = ((rd16(pl + 4) >> 4) + k / 3 - 1) & 31;
                uint32_t e = rd32(0xFFC246 + 4u * (uint32_t)(cy * 32 + cx)), o = e & 0xFFFFFF;
                if (e && !(e & 0x80000000u) && rd16(o) == 0x0018 && abs(wrapd(rd16(o + 2), rd16(pl + 2))) < 18 && abs(wrapd(rd16(o + 4), rd16(pl + 4))) < 18)
                    { trick_performed(p, frame); break; }
            }
        }
    scale_scores();
    for (p = 0; p < 4 && (int16_t)rd16(0xFF0040 + 0x2C4u * (uint32_t)p + 0xB8) <= 0; p++) ;
    if (p == 4) play_frame = -10000;                    /* nobody alive: game over or back to the menus */
    for (p = 0; p < 4; p++)
        if (teleport_to[p]) {
            move_hero(0xFF0040 + 0x2C4u * (uint32_t)p, teleport_to[p] - 1); teleport_to[p] = 0;
        }
    if (getenv("G4G2WARP") && cur_frame - play_frame <= 2) {   /* test aid: G4G2WARP=x,y puts player 1 there once */
        static uint32_t warped_rec;
        int x = 0, y = 0;
        if (warped_rec != g2_cell_rec && sscanf(getenv("G4G2WARP"), "%d,%d", &x, &y) == 2) {
            int cx = x * 16 - 0x64, cy = y * 16 - 0x68;     /* camera on the hero, as 0x3D9A aims it */
            warped_rec = g2_cell_rec; move_hero(0xFF0040, (y & 31) * 32 + (x & 31));
            if (getenv("G4G2KEYS")) wr16(0xFF0040 + 0xCC, (uint16_t)atoi(getenv("G4G2KEYS")));   /* test aid */
            if (!rd8(0xFFE7B6)) cx = cx < 5 ? 5 : cx > 0x134 ? 0x134 : cx;
            if (!rd8(0xFFE7B7)) cy = cy < 1 ? 1 : cy > 0x127 ? 0x127 : cy;
            wr16(0xFFE73C, (uint16_t)(cx & 0x1FF)); wr16(0xFFE73E, (uint16_t)(cy & 0x1FF));
            wr16(0xFFE740, (uint16_t)(cx & 0x1FF)); wr16(0xFFE742, (uint16_t)(cy & 0x1FF));
        }
    }
    {   static int done;                                   /* test aid: G4HEALTH=n sets player 1's health once in play */
        if (getenv("G4HEALTH") && !done && play_frame == frame - 1 && (int16_t)rd16(0xFF0040 + 0xB8) > 0) { wr16(0xFF0040 + 0xB8, (uint16_t)atoi(getenv("G4HEALTH"))); done = 1; } }
    for (p = 0; p < 4; p++) {                            /* narration: health falling; said again after a recovery */
        int hp = (int16_t)rd16(0xFF0040 + 0x2C4u * (uint32_t)p + 0xB8);
        if (hp > 600) lowhp_said[p] = die_said[p] = 0;
        else if (hp > 0 && hp <= 100 && !die_said[p] && level_set_g2 && g2_active && play_frame == frame - 1) {
            die_said[p] = 1; g2_say_hero(p); g2_say(31);    /* "... is about to die" */
        }
    }
    if (rd16(0xFFE7EA)) return;                          /* a hint box holds the game: timers wait too */
    for (p = 0; p < 4; p++) {
        uint32_t pl = 0xFF0040 + 0x2C4u * (uint32_t)p;
        int hp = (int16_t)rd16(pl + 0xB8), cell, f;
        if (hp <= 0) {
            stun_timer[p] = ff_timer[p] = on_stun[p] = 0;
            invuln[p] = repulse[p] = reflect[p] = transport[p] = super_shots[p] = 0;
            continue;
        }
        cell = (((int)rd16(pl + 4) + 8) >> 4 & 31) * 32 + (((int)rd16(pl + 2) + 8) >> 4 & 31);
        f = g2_cell[cell];
        if (p == 0 && g2_give_pending) { f = atoi(getenv("G4G2GIVE")); g2_give_pending = 0; cell = -1; }
        if (f >= 5 && f <= 9) {                                         /* an amulet: pick it up */
            static const char *what[5] = {"REPULSIVENESS", "REFLECTING SHOTS", "TRANSPORTABILITY", "10 SUPER SHOTS", "INVULNERABILITY"};
            switch (f) {
            case 5: repulse[p] = repulse_frames[p]; break;
            case 6: reflect[p] = 1; break;
            case 7: transport[p] = 1; break;
            case 8: super_shots[p] += 11; break;
            case 9: invuln[p] = 900; tc_inv[p] = 1; break;
            }
            {   static const short said[5] = {183, 181, 182, -1, -1};   /* narration: repulsiveness, reflective
                                                                          shot, transportability (labels unsure) */
                if (said[f - 5] >= 0) g2_say(said[f - 5]); }
            if (cell >= 0) g2_cell[cell] = 0;
            snprintf(banner, sizeof banner, "%s: %s", hero_name[p], what[f - 5]);   /* fits the play area */
            banner_until = frame + 150;
            f = 0;
        }
        wr8(pl + 0xEC, (reflect[p] || rd16(0xFFE838) == 2) ? 0xFF : 0x00);   /* bouncing shots: the amulet (Gauntlet 4
                                                    itself sets +$EC only in Battle mode, 0x8402) */
        if (super_shots[p] > 0) {                          /* one super shot used per shot that flew */
            if (rd8(pl + 0x69) && (int16_t)rd16(pl + 0x76) >= 1) shot_flying[p] = 1;
            else if (!rd8(pl + 0x69) && shot_flying[p]) {
                shot_flying[p] = 0; super_shots[p]--;
                if (getenv("G4MODDEBUG")) fprintf(stderr, "player %d: super shot used, %d left\n", p, super_shots[p]);
            }
        }
        if (invuln[p] > 0) invuln[p]--;
        if (repulse[p] > 0) repulse[p]--;
        if (f == 1 && !on_stun[p]) {
            stun_timer[p] = stun_frames[p]; g2_cell[cell] = 0; f = 0;
            g2_fx(46);                                   /* electric floor (sound test 046) */
            if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld player %d stunned at (%d,%d)\n", frame, p, cell & 31, cell >> 5);
        }   /* a stun tile
                                                                    stuns once, then is gone */
        on_stun[p] = f == 1;
        if (dizzy[p] > 0) {                            /* poisoned: Gauntlet 4's own CONFUSE TILE effect
                                                          (0x6B56: each new direction pressed is swapped for a
                                                          random neighbour, table 0x6BAE), slot 0x14 of the tile
                                                          effects 0x6942 -- the hero's routine 0x5C0A runs it from
                                                          +$138 at its start, before clearing it (0x5EDE) */
            dizzy[p]--;
            wr16(pl + 0x138, 0x14);
        }
        if (stun_timer[p] > 0) { stun_timer[p]--; wr8(pl + 0x22, 0xFF); wr8(pl + 0x23, 0xFF); }   /* no buttons */
        else if (transport[p]) transport_step(p);
        if (ff_timer[p] > 0) ff_timer[p]--;
        if ((f == 3 || f == 4) && beams_on(frame) && !ff_timer[p] && !invuln[p]) {
            hp -= ff_damage[p]; if (hp < 0) hp = 0;
            wr16(pl + 0xB8, (uint16_t)hp); ff_timer[p] = 16;
        }
    }
}

/* Cycling walls (Gauntlet II rule 0x800: 0x52F26 sets up, 0x5E62A runs): walls of phase 1-3.
   Every 120 frames the phase steps 1 -> 2 -> 3 -> 1; the walls of the old phase vanish and those
   of the new one return (not onto a player). At the start all are up, so the first two steps
   leave two groups, then one. Done inside the game's per-player routine 0x67BE so the game's
   own set-cell routine 0x18FAE (map $FFE246, redraws the wall and its neighbours) can be used. */
static void set_cell(M68K *c, int cell, int code)
{
    M68K save = *c;
    c->d[0] = (uint32_t)code; c->d[6] = (uint32_t)(cell & 31); c->d[7] = (uint32_t)(cell >> 5); c->a[4] = 0xFFE246;
    JSR(c, 0x0067BE, 0x018FAE);
    /* A6 is the game's free-list pointer for cell/object records (the drawers take records with
       move.l (a6)+ and give them back with -(a6)): it must keep its new value, or a record ends up
       both in use and free -- walls and monsters then share one (stale collision cells, monsters
       running wild) */
    save.a[7] = c->a[7]; save.a[6] = c->a[6]; *c = save;
}
static int player_on(int cell)          /* any hero or object overlapping the cell (G2 0x5E722 checks
                                           its playfield word: no wall returns onto a monster either) */
{
    int p, k;
    for (p = 0; p < 4; p++) {
        uint32_t pl = 0xFF0040 + 0x2C4u * (uint32_t)p;
        int x = rd16(pl + 2), y = rd16(pl + 4), cx, cy;
        if ((int16_t)rd16(pl + 0xB8) <= 0) continue;
        for (cy = (y >> 4) & 31; cy <= ((y + 15) >> 4 & 31); cy++)          /* every cell the hero overlaps */
            for (cx = (x >> 4) & 31; cx <= ((x + 15) >> 4 & 31); cx++)
                if (cy * 32 + cx == cell) return 1;
    }
    for (k = 0; k < 9; k++) {                           /* objects in the collision grid ($FFC246, a long
                                                           per cell -> object; position at +2/+4) */
        int nx = ((cell & 31) + k % 3 - 1) & 31, ny = ((cell >> 5) + k / 3 - 1) & 31, dx, dy;
        uint32_t e = rd32(0xFFC246 + 4u * (uint32_t)(ny * 32 + nx)), o = e & 0xFFFFFF;
        if (!e || o < 0xFF0000) continue;
        dx = ((int)rd16(o + 2) - (cell & 31) * 16) & 0x1FF; if (dx > 0xFF) dx -= 0x200;
        dy = ((int)rd16(o + 4) - (cell >> 5) * 16) & 0x1FF; if (dy > 0xFF) dy -= 0x200;
        if (dx > -16 && dx < 16 && dy > -16 && dy < 16) return 1;
    }
    return 0;
}
static void cycle_walls(M68K *c)
{
    int i, old = cycle_phase, now;
    if (cycle_timer-- > 0) return;
    cycle_timer = 119;
    now = old + 1 > 3 ? 1 : old + 1; cycle_phase = now;
    if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld cycling walls: phase %d -> %d\n", cur_frame, old, now);
    for (i = 32; i < 1024; i++) {
        int f = g2_cell[i];
        if (f < 10 || f > 12) continue;
        if (f - 9 == old && rd8(0xFFE246 + (uint32_t)i) == 0x09) set_cell(c, i, 0x00);
        else if (f - 9 == now && rd8(0xFFE246 + (uint32_t)i) == 0x00 && !player_on(i)) set_cell(c, i, 0x09);
    }
}
/* 0x88E8: a shot hit a wall cell (d0 = cell index, from the dispatcher at 0x8800). Gauntlet II's
   secret walls (04: "FIND SECRET WALLS FOR EXTRA FOOD/POTIONS") look like walls; a shot removes
   one, leaving food or a magic potion (Gauntlet 4 codes 22 / 23), chosen from the game's frame
   counter so every online copy agrees. */
static void (*orig_shot_wall)(M68K *);
static void dragon_hit(M68K *c, int cell, long frame);
static void shot_wall(M68K *c)
{
    int cell = (int)(c->d[0] & 0x3FF), f = level_set_g2 && g2_active ? g2_cell[cell] : 0;
    orig_shot_wall(c);
    if (f == 25) { dragon_hit(c, cell, cur_frame); return; }
    if (f == 14) {
        uint32_t sh = c->a[0];
        if (sh >= 0xFF0040 && sh < 0xFF0040 + 4 * 0x2C4) tc_wall_shot[(sh - 0xFF0040) / 0x2C4]++;
        room_walls++;
        g2_cell[cell] = 0;
        set_cell(c, cell, (rd16(0xFFE7E8) + cell) & 1 ? 0x23 : 0x22);
    }
}

/* Random walls (Gauntlet II 06, 0x5E41A): every 120 frames each flips between wall and floor
   with even chances (never closing onto a hero). The game's frame counter decides. */
static void random_walls(M68K *c)
{
    int i, r = rd16(0xFFE7E8);
    if (random_timer-- > 0) return;
    random_timer = 119;
    for (i = 32; i < 1024; i++) {
        int now;
        if (g2_cell[i] != 15) continue;
        r = r * 1103 + 12345 + i;                        /* a hash of the counter: same everywhere */
        if (((r >> 5) & 31) <= 15) continue;
        now = rd8(0xFFE246 + (uint32_t)i);
        if (now == 0x09) set_cell(c, i, 0x00);
        else if (now == 0x00 && !player_on(i)) set_cell(c, i, 0x09);
    }
}

/* Put a hero on a cell the way Gauntlet 4's own placement does (0x22E8): position +2/+4, and the
   collision grid $FFC246 (long per cell: record | 0x80000000, back-pointer at +$12) moved along,
   so no stale solid cell stays behind. */
static void move_hero(uint32_t pl, int cell)
{
    uint32_t old = rd32(pl + 0x12), now = 0xFFC246 + 4u * (uint32_t)cell;
    if (old >= 0xFFC246 && old < 0xFFD246 && rd32(old) == (pl | 0x80000000u)) wr32(old, 0);
    wr16(pl + 2, (uint16_t)((cell & 31) * 16)); wr16(pl + 4, (uint16_t)((cell >> 5) * 16));
    if (!rd32(now)) wr32(now, pl | 0x80000000u);
    wr32(pl + 0x12, now);
}

/* Transportability (Gauntlet II 0x42744 -> 0x4FEB2): a hero holding it who walks into a wall comes
   out on the cell beyond, in the direction pushed (diagonals too), as often as wanted until the
   level ends -- unless that cell is the top row, not floor, or taken by anything. Checked after
   the VBlank (pads read, logic not yet run): the hero must stand square on a cell, against it. */
static int is_wall(int code) { return code >= 0x09 && code <= 0x12; }
static void transport_step(int p)
{
    uint32_t pl = 0xFF0040 + 0x2C4u * (uint32_t)p;
    int x = rd16(pl + 2), y = rd16(pl + 4), pad = ~rd8(pl + 0x22) & 0x0F, dx = 0, dy = 0, cx, cy, wall, beyond;
    if (pad & 0x01) dy = -1; if (pad & 0x02) dy = 1; if (pad & 0x04) dx = -1; if (pad & 0x08) dx = 1;
    static uint32_t last[4];
    uint32_t pos = (uint32_t)x << 16 | (uint32_t)y, was = last[p];
    last[p] = pos;
    if (!dx && !dy) return;
    if (pos != was) return;                                    /* only when the wall stopped the hero */
    if ((dx && (x & 15)) || (dy && (y & 15))) return;          /* not yet square against the wall */
    cx = (x >> 4) & 31; cy = (y >> 4) & 31;
    if ((!dx && (x & 15)) || (!dy && (y & 15))) return;        /* half across a cell sideways */
    if (!rd8(0xFFE7B6) && (cx + 2 * dx < 0 || cx + 2 * dx > 31)) return;   /* not across the maze's edge */
    if (!rd8(0xFFE7B7) && (cy + 2 * dy < 1 || cy + 2 * dy > 31)) return;   /* unless it wraps that way */
    wall = ((cy + dy) & 31) * 32 + ((cx + dx) & 31);
    beyond = ((cy + 2 * dy) & 31) * 32 + ((cx + 2 * dx) & 31);
    if (!is_wall(rd8(0xFFE246 + (uint32_t)wall))) return;
    if (beyond < 32 || rd8(0xFFE246 + (uint32_t)beyond) != 0x00 || rd32(0xFFC246 + 4u * (uint32_t)beyond)) return;
    if (g2_cell[beyond] == 2 || g2_cell[beyond] == 18) return;  /* posts, exits */
    move_hero(pl, beyond);
    if (tr_goal == 3 || tr_goal == 4) trick_performed(p, cur_frame);   /* "TRY TRANSPORTABILITY" (hint table
                                                          0x597D8; the exact condition is not traced) */
    if (getenv("G4MODDEBUG")) fprintf(stderr, "transport: player %d through (%d,%d) to (%d,%d)\n", p, wall & 31, wall >> 5, beyond & 31, beyond >> 5);
}

/* 0x6D92 transporter touch (A0 = player). Gauntlet 4 looks for another transporter; with only one
   (Gauntlet II maze 96) Gauntlet II puts the hero on a cell next to the same transporter (0x4FAD4:
   neighbour table 0x5B64A, diagonals too). Taken here: the free neighbour nearest an exit. */
static void (*orig_transporter)(M68K *);
static void transporter(M68K *c)
{
    uint32_t pl = c->a[0];
    if (pl >= 0xFF0040 && pl < 0xFF0040 + 4 * 0x2C4) {        /* count a use once per visit */
        static long last[4]; int p = (int)((pl - 0xFF0040) / 0x2C4);
        if (cur_frame - last[p] > 30) { tc_transport[p]++; room_transports++; }
        last[p] = cur_frame;
    }
    int i, n = 0, me = -1, best = -1, bestd = 1 << 30;
    if (!g2_active || pl < 0xFF0040 || pl >= 0xFF0040 + 4 * 0x2C4) { orig_transporter(c); return; }
    for (i = 32; i < 1024; i++) if (rd8(0xFFE246 + (uint32_t)i) == 0x13) { n++; me = i; }
    if (n != 1) { orig_transporter(c); return; }
    for (i = 0; i < 9; i++) {
        int x = ((me & 31) + i % 3 - 1) & 31, y = ((me >> 5) + i / 3 - 1) & 31, cell = y * 32 + x, e, d;
        if (i == 4 || y == 0 || rd8(0xFFE246 + (uint32_t)cell) != 0x00) continue;
        for (e = 32; e < 1024; e++) {
            int t = rd8(0xFFE246 + (uint32_t)e), dx = (e & 31) - x, dy = (e >> 5) - y;
            if (t != 0x04) continue;                                      /* exits (G2 10/11 -> 04) */
            d = dx * dx + dy * dy;
            if (d < bestd) { bestd = d; best = cell; }
        }
    }
    wr16(pl + 0x138, 0x2C);
    if (getenv("G4MODDEBUG")) fprintf(stderr, "lone transporter: player %d -> cell (%d,%d)\n", (int)((pl - 0xFF0040) / 0x2C4), best & 31, best >> 5);
    if (best >= 0) teleport_to[(pl - 0xFF0040) / 0x2C4] = best + 1;   /* applied after the next VBlank: the
                                                                     movement code writes the position back */
}

/* 0x6BFE exit touch (A0 = player, D1 = tile code x 4, D4/D5 = cell x / y*32): Gauntlet 4 sends
   exits 05/06 to level 4/8 by setting the level counter to D1 - 0x11. Gauntlet II's "exit to 6"
   (maze 1) is an exit 04 the level file marks; D1 = 0x10 + 6 makes it lead to level 6. */
static void (*orig_exit)(M68K *);
static void exit_touch(M68K *c)
{
    int cell = (int)((c->d[5] & 0x3E0) | (c->d[4] & 31));
    if (g2_active && g2_cell[cell] != 29 && c->a[0] >= 0xFF0040 && c->a[0] < 0xFF0040 + 4 * 0x2C4) {
        int p = (int)((c->a[0] - 0xFF0040) / 0x2C4);
        if (in_room) { if (!room_done) room_exit(p); }
        else if (room_pending < 0 && goal_active() && ((trick_hero == p && trick_timer > 0) || exit_goal_met(p))) {
            room_pending = p;
            snprintf(banner, sizeof banner, "SECRET TRICK! SECRET ROOM NEXT"); banner_until = cur_frame + 150;
            if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld player %d earns a treasure room (goal %d)\n", cur_frame, p, tr_goal);
        }
    }
    if (g2_active && g2_cell[cell] == 29) {               /* a fake exit: nothing happens */
        if (!fake_cool) {
            snprintf(banner, sizeof banner, "FAKE EXIT"); banner_until = cur_frame + 90;
            if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld fake exit at (%d,%d)\n", cur_frame, cell & 31, cell >> 5);
        }
        fake_cool = 30;
        return;
    }
    if (getenv("G4MODDEBUG")) fprintf(stderr, "exit touch: d1=%X d4=%X d5=%X cell (%d,%d) feature %d\n", c->d[1] & 0xFFFF, c->d[4] & 0xFFFF, c->d[5] & 0xFFFF, cell & 31, cell >> 5, g2_cell[cell]);
    if (g2_active && g2_cell[cell] == 18) { c->d[1] = (c->d[1] & 0xFFFF0000u) | 0x16; g2o_exit6 = 1; }   /* (drawn with
                                                       Gauntlet 4's "EXIT TO 8" tile, code 06: see map_remap) */
    orig_exit(c);
}

/* Reflective shots: Gauntlet 4 already bounces the Wizard's shots off walls (record +$EC, set for
   hero 2 at 0x8406; on a wall 0x88F6 bounces when it is set, counting in +$F2, else the shot dies).
   A hero holding the amulet gets +$EC too (mods_after_vblank); as in Gauntlet II (0x4B4B0, counter
   $9048BE = 4 per shot) such a shot bounces at most 4 times. */
static void (*orig_shot_bounce)(M68K *);
static void shot_bounce(M68K *c)
{
    uint32_t pl = c->a[0];
    if (level_set_g2 && pl >= 0xFF0040 && pl < 0xFF0040 + 4 * 0x2C4) {
        int p = (int)((pl - 0xFF0040) / 0x2C4), bounces = (int16_t)rd16(pl + 0xF2);
        /* end the shot instead of bouncing: after 4 bounces, or when it meets a wall before it has
           moved at all (fired point-blank: Gauntlet 4's bounce would leave it stuck there for ~2 s,
           blocking the next shot) */
        int stop = reflect[p] && p != 2 && (bounces >= 4 || (bounces == 0 && (int16_t)rd16(pl + 0x76) <= 0));
        if (getenv("G4MODDEBUG")) fprintf(stderr, "shot of player %d hits a wall: bounces so far %d%s\n", p, bounces, stop ? " -> ends" : "");
        if (stop) { wr8(pl + 0xEC, 0); orig_shot_bounce(c); wr8(pl + 0xEC, 0xFF); return; }
        if (reflect[p]) g2_fx(44);                       /* shot reflects (sound test 044) */
    }
    orig_shot_bounce(c);
}

/* Super shots (Gauntlet II: 11 per amulet at $905F68, one used per shot 0x537F6; full power and the
   shot flies on through what it destroys, 0x4B000 / 0x4BD94 -- Death excepted). Gauntlet 4 checks
   a hero's shot against the objects around it in 0x8FF0 (A0 = hero, D2/D3 = shot position, A3 = the
   collision grid, +$224 = shot size) and, on a hit, takes the hero's damage from the target's
   health byte +$16 (0x9310), the shot then ending (D1 = -1). With super shots: every monster or
   generator the shot touches gets health 0 first, so the hit destroys it (normal score); those not
   hit get their health back; after a kill the shot carries on (D1 = 0). */
static int is_super_target(uint32_t o)
{
    int t = rd16(o);
    if (!t || o < 0xFF0000) return 0;
    if (t & 0x8000) return 1;                            /* generators */
    t &= 0xFF;
    return t >= 0x04 && t <= 0x14;                       /* monsters; not Death (0x18) or items (0x20+) */
}
static void (*orig_shot_objects)(M68K *);
static void shot_objects_g2(M68K *c);
static void shot_players(uint32_t pl, int sx, int sy, M68K *c)   /* shots between players (see pvp_rule) */
{
    int p = (int)((pl - 0xFF0040) / 0x2C4), q, size = (int16_t)rd16(pl + 0x224);
    for (q = 0; q < 4; q++) {
        uint32_t ql = 0xFF0040 + 0x2C4u * (uint32_t)q; int dx, dy, hp;
        if (q == p || (hp = (int16_t)rd16(ql + 0xB8)) <= 0 || invuln[q]) continue;
        dx = (((int)rd16(ql + 2) - sx) + 256) & 511; dx -= 256;
        dy = (((int)rd16(ql + 4) - sy) + 256) & 511; dy -= 256;
        if (dx >= size || dx <= -16 || dy >= size || dy <= -16) continue;
        tc_hit[q]++;                                   /* secret trick 9 ("DON'T GET HIT") */
        if (pvp_rule & 1) { stun_timer[q] += 40; if (stun_timer[q] > 90) stun_timer[q] = 90; }
        else { hp -= 2; if (hp < 0) hp = 0; wr16(ql + 0xB8, (uint16_t)hp); }
        c->d[1] = 0xFFFFFFFFu;                           /* the shot ends */
        if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld player %d's shot %s player %d\n", cur_frame, p, pvp_rule & 1 ? "stuns" : "hurts", q);
        return;
    }
}
static void shot_objects(M68K *c)
{
    uint32_t pl = c->a[0]; int sx = (int)(c->d[2] & 0x1FF), sy = (int)(c->d[3] & 0x1FF), k;
    shot_objects_g2(c);
    if (level_set_g2 && g2_active && pl >= 0xFF0040 && pl < 0xFF0040 + 4 * 0x2C4)   /* the arcade legend: */
        for (k = 0; k < n_it; k++) {                   /* shooting IT stuns it (and the shot ends) */
            int dx = ((it_obj[k].x + 8 - sx + 256) & 511) - 256, dy = ((it_obj[k].y + 8 - sy + 256) & 511) - 256;
            if (dx > -12 && dx < 12 && dy > -12 && dy < 12) {
                it_obj[k].stun = 120; c->d[1] = 0xFFFFFFFFu;
                if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld IT %d stunned by a shot\n", cur_frame, k);
                break;
            }
        }
    if (pvp_rule && level_set_g2 && g2_active && pl >= 0xFF0040 && pl < 0xFF0040 + 4 * 0x2C4)
        shot_players(pl, sx, sy, c);
}
static void shot_objects_g2(M68K *c)
{
    uint32_t pl = c->a[0], obj[9]; uint8_t hp[9]; int n = 0, k, p, killed = 0;
    if (!level_set_g2 || !g2_active || pl < 0xFF0040 || pl >= 0xFF0040 + 4 * 0x2C4) { orig_shot_objects(c); return; }
    p = (int)((pl - 0xFF0040) / 0x2C4);
    if (super_shots[p] <= 0) {                           /* food shot? (secret trick 5) */
        uint32_t food[9]; int nf = 0, sx = (int)(c->d[2] & 0x1FF), sy = (int)(c->d[3] & 0x1FF), cx = sx >> 4, cy = sy >> 4;
        for (k = 0; k < 9; k++) {
            int x = (cx + k % 3 - 1) & 31, y = (cy + k / 3 - 1) & 31, code = rd8(0xFFE246 + (uint32_t)(y * 32 + x));
            uint32_t e = rd32(0xFFC246 + 4u * (uint32_t)(y * 32 + x)), o = e & 0xFFFFFF;
            if (e && !(e & 0x80000000u) && rd16(o) == 0x0020 && (code == 0x22 || code == 0x25)) food[nf++] = o;
        }
        if (getenv("G4SHOTLOG")) fprintf(stderr, "f%ld shot at (%d,%d): %d food nearby, poison %06X\n", cur_frame, cx, cy, nf, poison_obj);
        orig_shot_objects(c);
        if (poison_obj && poison_gone()) {             /* shot the poison: monsters slowed */
            slow_timer = 600;
            snprintf(banner, sizeof banner, "SHOOTING POISON SLOWS MONSTERS"); banner_until = cur_frame + 150;
            if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld poison shot: monsters slowed\n", cur_frame);
            return;
        }
        for (k = 0; k < nf; k++) if (!rd16(food[k])) { tc_food_shot[p]++; g2_say_hero(p); g2_say(113); }   /* "... shot the food" */
        return;
    }
    {
        int sx = (int)(c->d[2] & 0x1FF), sy = (int)(c->d[3] & 0x1FF), size = (int16_t)rd16(pl + 0x224);
        int cx = sx >> 4, cy = sy >> 4;
        for (k = 0; k < 9; k++) {
            int x = (cx + k % 3 - 1) & 31, y = (cy + k / 3 - 1) & 31, dx, dy;
            uint32_t e = rd32(0xFFC246 + 4u * (uint32_t)(y * 32 + x)), o = e & 0xFFFFFF;
            if (!e || (e & 0x80000000u) || !is_super_target(o)) continue;
            dx = (((int)rd16(o + 2) - sx) + 256) & 511; dx -= 256;    /* the game's own test (0x9012) */
            dy = (((int)rd16(o + 4) - sy) + 256) & 511; dy -= 256;
            if (dx >= size || dx <= -16 || dy >= size || dy <= -16) continue;
            obj[n] = o; hp[n] = rd8(o + 0x16); wr8(o + 0x16, 0); n++;
        }
    }
    orig_shot_objects(c);
    for (k = 0; k < n; k++) {
        if (!rd16(obj[k])) killed = 1;                   /* destroyed: its record was freed */
        else wr8(obj[k] + 0x16, hp[k]);                  /* not the one hit: health back */
    }
    if (killed && (c->d[1] & 0xFF)) c->d[1] &= 0xFFFFFF00u; /* the super shot flies on */
}

/* Repulsiveness (Gauntlet II: timer $905F38, 908/1208/608/908 frames by hero; flag 0x200 of
   $9048E0): monsters run from the hero. Gauntlet 4's monster routines (one per kind) each pick the
   nearest hero in the active list $FF0B50 (skipping heroes with +$A6 set) and steer towards it
   (0xAAD0: distance -> direction byte +$18). While such a routine runs for a monster whose nearest
   hero is repulsive, that hero is shown at the point mirrored through the monster, so it steers
   away; the real position is put back at once. */
/* Super sorcerers (Gauntlet II 0x1A, "SHOOT SUPER SORCEROR"): Gauntlet 4 sorcerers (they blink in
   and out) that also throw fireballs. Gauntlet 4's only thrower is the demon (0xC8E4: a free slot of
   the first 4 in the projectile list $FFA834, 0x1C bytes each; direction 0-7 = down up left right,
   down-left down-right up-left up-right; start offset tables 0xC994/0xC9A4, speed 0xC9B4); the
   same is done for a super sorcerer after its own routine, every 90 frames (assumed) when a hero
   is within 8 cells, aimed in the nearest of the 8 directions. */
/* Their look: in the arcade a super sorcerer is the plain sorcerer picture (0x13A2^0x800, colour 11).
   The port gives it a light maroon tint (user's choice, 2026-11-21) so it is just distinguishable:
   the screen boxes of the living super sorcerers, for the host to tint the
   monster pixels inside (display only). World position +2/+4 minus the camera, like 0x1BA06. */
/* The mugger's look (user, 2026-11-22: the arcade's own pictures): while a mugger is out, the host
   draws g2_sprites.bin picture 90 + 3 * direction + frame in a 24x24 box over Gauntlet 4's thief
   (whose sprite is hidden there). Direction from its movement (N NE E SE S SW W NW), walking
   frames 0 1 2 1 while it moves. Returns 1 and the box (x0, y0 on screen) and the picture's pixels. */
static int mugger;                                     /* (defined with the thief code below) */
int mods_mugger_box(int *x0, int *y0, const uint8_t **rgba)
{
    static int lastx = -1, lasty = -1, dir = 4, step; static long seen = -1;
    int sx, sy, x, y, k;
    uint32_t t = 0xFFA362;
    if (!mugger || !level_set_g2 || !g2_active || !rd16(t) || cur_frame - play_frame > 600) { lastx = -1; return 0; }
    x = rd16(t + 2); y = rd16(t + 4);
    if (seen != cur_frame) {                           /* once a frame: its direction and walking frame */
        seen = cur_frame;
        if (lastx >= 0 && (x != lastx || y != lasty)) {
            int dx = ((x - lastx + 0x100) & 0x1FF) - 0x100, dy = ((y - lasty + 0x100) & 0x1FF) - 0x100;
            static const int d8[3][3] = {{7, 0, 1}, {6, 4, 2}, {5, 4, 3}};   /* [dy+1][dx+1] */
            dir = d8[(dy > 0) - (dy < 0) + 1][(dx > 0) - (dx < 0) + 1];
            step++;
        }
        lastx = x; lasty = y;
    }
    {   static const int cyc[4] = {0, 1, 2, 1};
        if ((k = g2pic_find(90 + 3 * dir + cyc[(step >> 3) & 3])) < 0) return 0; }
    sx = ((x - (int)rd16(0xFFE740)) & 0x1FF) + TINT_DX;
    sy = ((y - (int)rd16(0xFFE742)) & 0x1FF) + TINT_DY;
    if (sx > 0x180) sx -= 0x200;
    if (sy > 0x180) sy -= 0x200;
    *x0 = sx - 4; *y0 = sy - 8;                         /* 24x24 over its 16x16 square, feet at the bottom */
    *rgba = g2pic[k].px;
    if (getenv("G4THIEFLOG") && cur_frame % 10 == 0 && seen == cur_frame) fprintf(stderr, "f%ld mugger drawn: picture %d at %d,%d\n", cur_frame, g2pic[k].id, *x0, *y0);
    return 1;
}
int mods_tint_boxes(int *box, int max)
{
    int k, n = 0;
    if (!level_set_g2 || !g2_active || cur_frame - play_frame > 600) return 0;
    for (k = 0; k < n_super && n < max; k++) {
        uint32_t o = super_sorc[k]; int sx, sy;
        if (rd16(o) != 0x0010) continue;               /* gone (the record is now something else) */
        sx = (((int)rd16(o + 2) - (int)rd16(0xFFE740)) & 0x1FF) + TINT_DX;
        sy = (((int)rd16(o + 4) - (int)rd16(0xFFE742)) & 0x1FF) + TINT_DY;
        if (sx > 0x180) sx -= 0x200;
        if (sy > 0x180) sy -= 0x200;
        box[4 * n] = sx - 4; box[4 * n + 1] = sy - 4; box[4 * n + 2] = sx + 28; box[4 * n + 3] = sy + 28; n++;
    }
    return n;
}
static void find_super_sorcerers(void)
{
    int cell;
    super_found = 1;
    for (cell = 32; cell < 1024; cell++) {
        uint32_t e = rd32(0xFFC246 + 4u * (uint32_t)cell), o = e & 0xFFFFFF;
        if (g2_cell[cell] != 24) continue;
        g2_cell[cell] = 0;
        if (e && !(e & 0x80000000u) && rd16(o) == 0x0010 && n_super < 48) { super_cool[n_super] = 60 + 7 * n_super; super_sorc[n_super++] = o; }
    }
}
/* launch a fireball from (mx,my) at the nearest hero within `range` pixels, starting `push`
   pixels out in its direction (clear of a big thrower); 0 if none was sent */
static int fireball_at_hero(int mx, int my, int range, int push)
{
    static const int dirs[8][2] = {{0, 1}, {0, -1}, {-1, 0}, {1, 0}, {-1, 1}, {1, 1}, {-1, -1}, {1, -1}};
    int p, bestd = 1 << 30, tx = 0, ty = 0, d, slot, best = 0, i;
    uint32_t b;
    for (p = 0; p < 4; p++) {
        uint32_t pl = 0xFF0040 + 0x2C4u * (uint32_t)p; int dx, dy;
        if ((int16_t)rd16(pl + 0xB8) <= 0) continue;
        dx = ((rd16(pl + 2) - mx + 256) & 511) - 256; dy = ((rd16(pl + 4) - my + 256) & 511) - 256;
        d = abs(dx) + abs(dy);
        if (d < bestd) { bestd = d; tx = dx; ty = dy; }
    }
    if (bestd > range) return 0;
    for (slot = 0; slot < 4 && rd8(0xFFA834 + 0x1Cu * (uint32_t)slot); slot++) ;
    if (slot == 4) return 0;
    {   double bestdot = -1e9;                            /* nearest of the 8 directions */
        for (i = 0; i < 8; i++) {
            double l = (dirs[i][0] && dirs[i][1]) ? 1.41421 : 1.0, dot = (dirs[i][0] * tx + dirs[i][1] * ty) / l;
            if (dot > bestdot) { bestdot = dot; best = i; }
        }
    }
    b = 0xFFA834 + 0x1Cu * (uint32_t)slot;                /* as 0xC914-0xC96A */
    wr8(b, 0xFF); wr16(b + 0x12, 0); wr16(b + 0x18, 0x600); wr16(b + 0x16, 0); wr16(b + 2, 0);
    wr16(b + 0x10, (uint16_t)best);
    wr16(b + 4, (uint16_t)(((mx + dirs[best][0] * push + (int16_t)rd16(0xC994 + 2u * (uint32_t)best)) & 0x1FF) << 6));
    wr16(b + 6, (uint16_t)(((my + dirs[best][1] * push + (int16_t)rd16(0xC9A4 + 2u * (uint32_t)best)) & 0x1FF) << 6));
    wr32(b + 8, rd32(0xC9B4 + 4u * (uint32_t)best));
    wr16(b + 0xC, 2); wr16(b + 0xE, 2);
    return slot + 1;                                     /* which slot (the dragon follows its fireball) */
}
static void super_fire(uint32_t mon, int k)
{
    if (super_cool[k] > 0) { super_cool[k]--; return; }
    if (fireball_at_hero(rd16(mon + 2), rd16(mon + 4), 8 * 16, 0)) {
        super_cool[k] = 90;
        if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld super sorcerer %06X fires\n", cur_frame, mon);
    }
}

/* Gauntlet 4's pickup handlers (touch table 0x67F0; A0 = hero): counted for the secret tricks,
   the treasure room and the bonus multiplier. 0: key, 1/2: treasure, 3: food, 4: potion, 5: power potion */
static const uint32_t pickup_fn[6] = {0x00752E, 0x00765C, 0x0076CA, 0x007926, 0x007A5C, 0x007AF4};
static void (*orig_pickup_fn[6])(M68K *);
static int poison_gone(void)                           /* the poison's food object was just taken or shot */
{
    int k;
    for (k = 0; k < n_g3_poison; k++) {                 /* GAUNTLET III: one of its poisoned foods */
        uint32_t o = g3_poison[k].obj; int cell = g3_poison[k].cell;
        if (rd16(o) == 0x0020 && (rd32(0xFFC246 + 4u * (uint32_t)cell) & 0xFFFFFF) == o) continue;
        if (g2_cell[cell] == 26) g2_cell[cell] = 0;
        g3_poison[k] = g3_poison[--n_g3_poison];
        return 1;
    }
    if (!poison_obj || (rd16(poison_obj) == 0x0020 && (rd32(0xFFC246 + 4u * (uint32_t)poison_cell) & 0xFFFFFF) == poison_obj)) return 0;
    if (poison_cell >= 0 && g2_cell[poison_cell] == 26) g2_cell[poison_cell] = 0;
    poison_obj = 0; poison_cell = -1;
    return 1;
}
static void pickup_step(M68K *c, int which)
{
    uint32_t pl = c->a[0];
    int hp_before = (pl >= 0xFF0040 && pl < 0xFF0040 + 4 * 0x2C4) ? (int16_t)rd16(pl + 0xB8) : 0;
    uint32_t score_before = (pl >= 0xFF0040 && pl < 0xFF0040 + 4 * 0x2C4) ? rd32(pl + 0xC6) : 0;
    orig_pickup_fn[which](c);
    if (level_set_g3 && in_room && (which == 1 || which == 2) && pl >= 0xFF0040 && pl < 0xFF0040 + 4 * 0x2C4)
        wr32(pl + 0xC6, score_before);                  /* the Atari's treasure room (0x18F4): only counted, paid at the exit */
    if (which == 3 && level_set_g2 && pl >= 0xFF0040 && pl < 0xFF0040 + 4 * 0x2C4 && poison_gone()) {
        int p = (int)((pl - 0xFF0040) / 0x2C4), hp = hp_before - 50;
        wr16(pl + 0xB8, (uint16_t)(hp < 0 ? 0 : hp));     /* poison: no food, 50 health lost */
        dizzy[p] = 600 + (int)(rd16(0xFFE7E8) % 601);   /* 10-20 seconds (user, Gauntlet II) */
        snprintf(banner, sizeof banner, "POISONED: YOU ARE DIZZY"); banner_until = cur_frame + 150;
        g2_fx(55);                                       /* poisoned (sound test 055) */
        if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld player %d is poisoned\n", cur_frame, p);
        return;
    }
    if (!level_set_g2 || !g2_active || pl < 0xFF0040 || pl >= 0xFF0040 + 4 * 0x2C4) return;
    {
        int p = (int)((pl - 0xFF0040) / 0x2C4);
        if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld player %d picks up %s\n", cur_frame, p, which == 0 ? "a key" : which <= 2 ? "treasure" : which == 3 ? "food" : "a potion");
        switch (which) {
        case 0: case 4: case 5: tc_keypot[p]++; break;
        case 1: case 2: tc_treasure[p]++; if (in_room) room_treas[p]++; else treasure_taken(p, cur_frame); break;
        case 3: tc_food[p]++; if (dizzy[p]) { dizzy[p] = 0; if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld player %d: food cures the dizziness\n", cur_frame, p); } break;
        }
    }
}
#define PICKUP_WRAP(i) static void pickup_step_##i(M68K *c) { pickup_step(c, i); }
PICKUP_WRAP(0) PICKUP_WRAP(1) PICKUP_WRAP(2) PICKUP_WRAP(3) PICKUP_WRAP(4) PICKUP_WRAP(5)
static void (*const pickup_wrap[6])(M68K *) = {pickup_step_0, pickup_step_1, pickup_step_2, pickup_step_3, pickup_step_4, pickup_step_5};

static const uint32_t monster_fn[] = {0x00AA82, 0x00ADE0, 0x00B692, 0x00BFDE, 0x00CB82, 0x00D4DC,
                                      0x00DE46, 0x00F41C, 0x0134D0, 0x013BA6, 0x0145C4};
#define N_MONSTER_FN (sizeof monster_fn / sizeof monster_fn[0])
static void (*orig_monster_fn[N_MONSTER_FN])(M68K *);
static void monster_step_inner(M68K *c, int which);
static void monster_step(M68K *c, int which)
{
    uint32_t mon = c->a[0]; int k;
    uint16_t type = rd16(mon);
    if (monster_fn[which] == 0x00F41C) { orig_monster_fn[which](c); return; }   /* the thief's level set-up,
                                                       not a monster routine */
    if (slow_timer > 0 && (cur_frame & 1) && level_set_g2 && g2_active) return;   /* poison shot: half speed */
    monster_step_inner(c, which);
    if (level_set_g2 && g2_active && (rnd_rule & 0x7F0000) && rd16(mon) == type && mon >= 0xFF0000) {
        /* a fast-monsters random rule (see random_rule): this kind moves twice per frame */
        static const int8_t bit[N_MONSTER_FN] = {-1, 0, 1, 2, 4, 3, 6, -1, -1, -1, -1};   /* ghost grunt demon
                                                       sorcerer lobber Death, by routine */
        int b = bit[which], super = 0;
        if (which == 4) for (k = 0; k < n_super; k++) if (super_sorc[k] == mon) super = 1;
        if (super) b = 5;
        if (b >= 0 && (rnd_rule >> (16 + b)) & 1) {
            uint32_t a0 = c->a[0];                     /* a second step, as if the game called it again */
            c->a[0] = mon;
            monster_step_inner(c, which);
            c->a[0] = a0;
        }
    }
    if (which == 4 && level_set_g2 && g2_active && n_super && rd16(mon) == 0x0010)   /* 0xCB82: sorcerers (type
                                                       0x10; the routine table 0xA89E is indexed by type) */
        for (k = 0; k < n_super; k++) if (super_sorc[k] == mon) { super_fire(mon, k); break; }
}
static void monster_step_inner(M68K *c, int which)
{
    uint32_t mon = c->a[0], best = 0; int k, n, bestd = 0x7FFF, mx, my;
    if (!level_set_g2 || !g2_active || mon < 0xFF0000) { orig_monster_fn[which](c); return; }
    mx = rd16(mon + 2); my = rd16(mon + 4);
    n = (int16_t)rd16(0xFF0B60);
    for (k = 0; k <= n && k < 4; k++) {                  /* the hero this monster would chase */
        uint32_t h = rd32(0xFF0B50 + 4u * (uint32_t)k); int dx, dy, d;
        if (h < 0xFF0040 || h >= 0xFF0040 + 4 * 0x2C4 || rd16(h + 0xA6)) continue;
        dx = (((int)rd16(h + 2) - mx) + 256) & 511; dx -= 256;
        dy = (((int)rd16(h + 4) - my) + 256) & 511; dy -= 256;
        d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (d <= bestd) { bestd = d; best = h; }
    }
    if (it_player >= 0 && hero_alive(it_player)) {        /* someone is IT: monsters see only that hero */
        uint16_t save[4]; int q;
        for (q = 0; q < 4; q++) {
            uint32_t h = 0xFF0040 + 0x2C4u * (uint32_t)q;
            save[q] = rd16(h + 0xA6);
            if (q != it_player) wr16(h + 0xA6, 1);
        }
        best = 0xFF0040 + 0x2C4u * (uint32_t)it_player;
        if (repulse[it_player] > 0) {
            uint16_t hx = rd16(best + 2), hy = rd16(best + 4);
            wr16(best + 2, (uint16_t)((2 * mx - hx) & 0x1FF)); wr16(best + 4, (uint16_t)((2 * my - hy) & 0x1FF));
            orig_monster_fn[which](c);
            wr16(best + 2, hx); wr16(best + 4, hy);
        } else orig_monster_fn[which](c);
        for (q = 0; q < 4; q++) wr16(0xFF0040 + 0x2C4u * (uint32_t)q + 0xA6, save[q]);
        return;
    }
    if (best && repulse[(best - 0xFF0040) / 0x2C4] > 0) {
        uint16_t hx = rd16(best + 2), hy = rd16(best + 4);
        wr16(best + 2, (uint16_t)((2 * mx - hx) & 0x1FF)); wr16(best + 4, (uint16_t)((2 * my - hy) & 0x1FF));
        orig_monster_fn[which](c);
        wr16(best + 2, hx); wr16(best + 4, hy);
        return;
    }
    orig_monster_fn[which](c);
}
#define MONSTER_WRAP(i) static void monster_step_##i(M68K *c) { monster_step(c, i); }
MONSTER_WRAP(0) MONSTER_WRAP(1) MONSTER_WRAP(2) MONSTER_WRAP(3) MONSTER_WRAP(4) MONSTER_WRAP(5)
MONSTER_WRAP(6) MONSTER_WRAP(7) MONSTER_WRAP(8) MONSTER_WRAP(9) MONSTER_WRAP(10)
static void (*const monster_wrap[N_MONSTER_FN])(M68K *) = {monster_step_0, monster_step_1, monster_step_2,
    monster_step_3, monster_step_4, monster_step_5, monster_step_6, monster_step_7, monster_step_8,
    monster_step_9, monster_step_10};

/* Locked chests (Gauntlet II 0x2F: "SAVE KEYS FOR CLOSED TREASURE CHESTS"). In the map they are
   Gauntlet 4 door cells (they block heroes and shots); the host draws a chest over them. Gauntlet
   4's door bump (0x671A: key count +$CC, opens the whole run of doors through $FFE7F0) is taken
   over for them: with a key, one key is used and the chest opens into treasure, food, a red potion,
   a Death, or rarely a hidden (power) potion. Chances (user's memory): treasure 40, food 25,
   red potion 15, Death 15, hidden potion 5 %, from the game's frame counter. */
static int chest_contents(int cell)
{
    int r = getenv("G4CHEST") ? atoi(getenv("G4CHEST")) : (rd16(0xFFE7E8) * 37 + cell * 11) % 100;   /* G4CHEST: test aid */
    if (r < 40) return 0x1E;                             /* treasure */
    if (r < 65) return 0x22;                             /* food */
    if (r < 80) return 0x24;                             /* red potion */
    if (r < 95) return -1;                               /* Death */
    return 0x26 + (rd16(0xFFE7E8) + cell) % 6;           /* hidden potion (rare): a power-up */
}
/* Create a monster from a Gauntlet 4 map code at a cell, as the level loader does (0x18ED2:
   D0 = code 1tttt f ss, D6/D7 = cell x/y; record from the A6 pool, collision grid, monster lists) */
static void spawn_monster(M68K *c, int cell, int code)
{
    M68K save = *c;
    c->d[0] = (uint32_t)code; c->d[6] = (uint32_t)(cell & 31); c->d[7] = (uint32_t)(cell >> 5);
    JSR(c, 0x0067AC, 0x018ED2);
    save.a[7] = c->a[7]; save.a[6] = c->a[6]; *c = save;     /* A6: the record pool, see set_cell */
}
static void (*orig_door)(M68K *);
static void door_touch(M68K *c)              /* 0x671A: a hero runs into a door cell (D0 = cell; returns
                                                D1 = 0 passable now, -1 blocked) */
{
    uint32_t pl = c->a[0]; int cell = (int)(c->d[0] & 0x3FF), what;
    if (!level_set_g2 || !g2_active || g2_cell[cell] != 22 || pl < 0xFF0040 || pl >= 0xFF0040 + 4 * 0x2C4) { orig_door(c); return; }
    if (!rd16(pl + 0xCC)) {                                                    /* no key: blocked */
        static long said_at = -1000;
        if (cur_frame - said_at > 240) { said_at = cur_frame; g2_say(184); }     /* "this treasure chest is locked"
                                                                                    (sound test 210), at most every 4 s */
        c->d[1] = 0xFFFFFFFFu; return;
    }
    wr16(pl + 0xCC, (uint16_t)(rd16(pl + 0xCC) - 1));
    g2_cell[cell] = 0;
    what = chest_contents(cell);
    if (getenv("G4MODDEBUG")) fprintf(stderr, "chest at (%d,%d) opened: %s\n", cell & 31, cell >> 5, what < 0 ? "DEATH" : "item");
    g2_fx(42);                                           /* chest opens (sound test 042) */
    set_cell(c, cell, what < 0 ? 0x00 : what);
    if (what < 0) spawn_monster(c, cell, 0xA8);          /* a Death comes out */
    c->d[1] = 8; JSR(c, 0x0067AC, 0x01F616);              /* the door sound */
    c->d[1] = 0;
}

/* Moveable walls (Gauntlet II 0x03, "TRY PUSHING A WALL"; 0x41C84 -> 0x4280E / 0x4293A): a hero
   stopped square against one while pushing moves it a cell further, if that cell is open floor
   with nothing on it. Holding on keeps pushing. Transportability, if held, goes first (through). */
static void push_wall(M68K *c, int p)
{
    static uint32_t last[4];
    uint32_t pl = 0xFF0040 + 0x2C4u * (uint32_t)p, pos;
    int x = rd16(pl + 2), y = rd16(pl + 4), pad = ~rd8(pl + 0x22) & 0x0F, dx = 0, dy = 0, wall, beyond, cx, cy;
    pos = (uint32_t)x << 16 | (uint32_t)y;
    if (pos != last[p]) { last[p] = pos; return; }              /* only when stopped */
    if (pad & 0x01) dy = -1; else if (pad & 0x02) dy = 1; else if (pad & 0x04) dx = -1; else if (pad & 0x08) dx = 1;
    if (!dx && !dy) return;
    if ((x & 15) || (y & 15)) return;
    cx = x >> 4; cy = y >> 4;
    wall = ((cy + dy) & 31) * 32 + ((cx + dx) & 31);
    beyond = ((cy + 2 * dy) & 31) * 32 + ((cx + 2 * dx) & 31);
    if (g2_cell[wall] != 23 || rd8(0xFFE246 + (uint32_t)wall) != 0x09) return;
    if (beyond < 32 || rd8(0xFFE246 + (uint32_t)beyond) != 0x00 || rd32(0xFFC246 + 4u * (uint32_t)beyond)) return;
    if (g2_cell[beyond]) return;                                /* not onto a stun tile, amulet, ... */
    set_cell(c, wall, 0x00); set_cell(c, beyond, 0x09);
    g2_cell[wall] = 0; g2_cell[beyond] = 23;
    if (getenv("G4MODDEBUG")) fprintf(stderr, "player %d pushes a wall from (%d,%d) to (%d,%d)\n", p, wall & 31, wall >> 5, beyond & 31, beyond >> 5);
    pushes_left--;
    g2_fx(43);                                           /* moving walls (sound test 043) */                                     /* secret trick 0x0F ("BE PUSHY"), judged at the exit */
    if (tr_goal == 0x0A) trick_performed(p, cur_frame);   /* "TRY PUSHING A WALL": at once (0x42846) */
}

/* The dragon (Gauntlet II 0x3C, in 18 mazes; placed only from level 12 on, 0x45E6A; set-up
   0x5496E takes the cell, the one to its right and the two above, all solid 0x8002, 0x462AE;
   "SHOOT DRAGON'S HEAD"). Here: on the first frame of play, when the level count ($FFE760) is 12
   or more, the 2x2 block becomes wall cells with the dragon drawn over them; it breathes a fireball
   at the nearest hero within 10 cells about once a second; shots at its head (the top two cells)
   wear it down -- 20 hits, assumed -- and it leaves treasure where it lay. */
static int dragon_block(int k)                         /* k = 0..3: base, right, above, above-right */
{
    int x = dragon.cell & 31, y = dragon.cell >> 5;
    return (((y - (k >> 1)) & 31) * 32) + ((x + (k & 1)) & 31);
}
static void dragon_place(M68K *c)
{
    int i, k;
    dragon.placed = 1;
    for (i = 32; i < 1024; i++) if (g2_cell[i] == 25) break;
    if (i == 1024) return;
    g2_cell[i] = 0;
    if (rd16(0xFFE760) < 12 && !getenv("G4DRAGONALWAYS")) return;   /* Gauntlet II: none before level 12
                                                                       (G4DRAGONALWAYS: test aid) */
    dragon.cell = i; dragon.hp = 20; dragon.cool = 0; dragon.breath = 0; dragon_awake = 0; dragon.fb = 0;
    dragon_state = 0; dragon_count = 0; dragon_step = 0; dragon_turn = 0; dragon_stun = 0;
    for (k = 0; k < 4; k++) { set_cell(c, dragon_block(k), 0x09); g2_cell[dragon_block(k)] = 25; }
    {   /* its facing: the side with the most open floor up to 4 cells out from its 2x2 block
           (matches the arcade in the mazes the user checked: 18, 29 E, 36 N, 40, 42 W); ties
           in the order N S W E, swapped in a mirrored maze so the choice mirrors too */
        static const int order[4] = {3, 2, 0, 1};      /* N, S, W, E as facing numbers */
        int x = i & 31, y = i >> 5, f, best = -1, o, kk, side;
        dragon_face = 0;
        for (o = 0; o < 4; o++) {
            int d = 0;
            f = order[o];
            if (g2_mirror & 1) f = f == 0 ? 1 : f == 1 ? 0 : f;
            if (g2_mirror & 2) f = f == 2 ? 3 : f == 3 ? 2 : f;
            for (kk = 1; kk <= 4; kk++) for (side = 0; side < 2; side++) {
                int cx = f == 0 ? x - kk : f == 1 ? x + 1 + kk : x + side;
                int cy = f == 2 ? y + kk : f == 3 ? y - 1 - kk : y - side;
                if (acid_free(cx, cy)) d++;
            }
            if (d > best) { best = d; dragon_face = f; }
        }
    }
    if (getenv("G4MODDEBUG")) fprintf(stderr, "dragon at (%d,%d) facing %c\n", i & 31, i >> 5, "WESN"[dragon_face]);
}
/* The dragon's look (Gauntlet II's frames, g2_sprites.bin), from its state (dragon_tick): asleep
   curled up; waking: curled, half, half, awake (16 frames each, table 0x5D508); awake: the head at
   one of four positions, back or out (breathing), from its breath pattern; turning: half-curled.
   Picture ids per facing (W E S N): head sets 2/20/30/40 (+2 x aim + out), curled 1/11/1/11, half
   10/12/10/12. The arcade's head position p (0-3) is aim p for W/E and 3 - p for S/N (their sets
   run the other way). */
static void dragon_look(int v)                         /* v: pattern value (head position << 1 | out) */
{
    static const int head[4] = {2, 20, 30, 40}, curl[4] = {1, 11, 1, 11}, half[4] = {10, 12, 10, 12};
    int pos = v >> 1;
    dragon_aim = dragon_face >= 2 ? 3 - pos : pos;
    if (dragon_state == 0) dragon_pic = curl[dragon_face];
    else if (dragon_state == 1) dragon_pic = dragon_count >= 48 ? curl[dragon_face] : dragon_count >= 16 ? half[dragon_face] : head[dragon_face] + 2 * dragon_aim;
    else if (dragon_turn > 0) dragon_pic = half[dragon_face];
    else dragon_pic = head[dragon_face] + 2 * dragon_aim + (v & 1);
}

/* The dragon's head is in one square of its 2x2 block, by facing and aim (user, from the arcade
   footage: shots hit only where the head is, and the fire comes from it). Block squares
   (dragon_block): 0 bottom-left, 1 bottom-right, 2 top-left, 3 top-right. West/east: head high
   (aim 0-1) or low (2-3) on that side; south/north: left (0-1) or right (2-3) on that edge. */
static int dragon_head(void)
{
    int low_or_right = dragon_aim >= 2;
    switch (dragon_face) {
    case 0:  return low_or_right ? 0 : 2;              /* west */
    case 1:  return low_or_right ? 1 : 3;              /* east */
    case 2:  return low_or_right ? 1 : 0;              /* south */
    default: return low_or_right ? 3 : 2;              /* north */
    }
}
static int dragon_intro_done;                          /* "SHOOT DRAGON'S HEAD", once per game */
/* Breath patterns (Gauntlet II 0x5D578): 16 steps of 8 frames; each step is head position << 1 |
   breath. One of the five is picked at random each time the dragon wakes (0x5451E). */
static const uint8_t dragon_pats[5][16] = {
    {0, 1, 0, 2, 4, 6, 7, 6, 4, 2, 3, 2, 4, 5, 4, 2}, {0, 2, 4, 6, 7, 5, 3, 1, 0, 2, 4, 6, 7, 5, 3, 1},
    {0, 1, 3, 2, 4, 6, 7, 5, 4, 2, 3, 5, 4, 2, 3, 1}, {0, 2, 4, 7, 5, 3, 4, 7, 4, 2, 1, 0, 2, 4, 5, 2},
    {0, 3, 4, 7, 6, 5, 2, 1, 2, 4, 7, 5, 4, 2, 3, 1}};
/* the nearest living hero as seen from the dragon: offsets from (mx, my), in pixels */
static int dragon_nearest(int mx, int my, int *ox, int *oy)
{
    int p, best = 1 << 30, found = 0;
    for (p = 0; p < 4; p++) {
        uint32_t pl = 0xFF0040 + 0x2C4u * (uint32_t)p; int dx, dy, d;
        if ((int16_t)rd16(pl + 0xB8) <= 0) continue;
        dx = ((((int)rd16(pl + 2) + 8 - mx) + 256) & 511) - 256; dy = ((((int)rd16(pl + 4) + 8 - my) + 256) & 511) - 256;
        d = abs(dx) + abs(dy);
        if (d < best) { best = d; *ox = dx; *oy = dy; found = 1; }
    }
    return found;
}
/* The dragon (Gauntlet II 0x54454, traced 2026-11-22). Asleep, curled up, until a hero comes
   within 4 squares of its 2x2 block (0x549EA, called as heroes move or appear) -- then it wakes
   (49 frames) and stays awake for good. Awake it runs its breath pattern: each 8 frames the
   next step (head position, breath or not); on a breath step it breathes fire (here: at a hero
   within 3 squares, user's footage), and while that hero is in its line of fire it holds the
   pose and keeps breathing (0x545AA). Each 8 frames it also looks for the nearest hero and turns
   to face them (16 frames) if the two squares on that side are open (0x53E4A: up/down if the hero
   is further off that way than sideways, else left/right). Magic stuns it: no fire, 300 frames. */
static void dragon_tick(void)
{
    int x = dragon.cell & 31, y = dragon.cell >> 5, v, cx = (dragon.cell & 31) * 16 + 16, cy = ((dragon.cell >> 5) & 31) * 16;
    if (dragon.cell < 0) return;
    if (dragon.breath > 0) dragon.breath--;
    if (dragon.cool > 0) dragon.cool--;
    if (dragon.fb) {                                   /* its fireball dies away after 3 squares (user) */
        int fx = rd16(dragon.fb + 4) >> 6, fy = rd16(dragon.fb + 6) >> 6;
        int dx = ((fx - dragon.fb_x0 + 256) & 511) - 256, dy = ((fy - dragon.fb_y0 + 256) & 511) - 256;
        if (rd8(dragon.fb) != 0xFF) dragon.fb = 0;
        else if (abs(dx) > 48 || abs(dy) > 48) { wr8(dragon.fb, 0); dragon.fb = 0; }
    }
    if (!dragon_intro_done) {                          /* the arcade's hint, the first time one is in view */
        int ex = (((dragon.cell & 31) * 16 + 8 - (int)rd16(0xFFE740) - 112) + 256 & 511) - 256;
        int ey = ((((dragon.cell >> 5) & 31) * 16 - (int)rd16(0xFFE742) - 112) + 256 & 511) - 256;
        if (ex > -112 && ex < 112 && ey > -112 && ey < 112) {
            dragon_intro_done = 1;
            snprintf(banner, sizeof banner, "SHOOT DRAGON'S HEAD"); banner_until = cur_frame + 240;
        }
    }
    v = dragon_pats[dragon_pat][(dragon_step >> 3) & 15];
    if (dragon_state == 0) {                           /* asleep: a hero within 4 squares of the block wakes it */
        int p;
        for (p = 0; p < 4; p++) {
            uint32_t pl = 0xFF0040 + 0x2C4u * (uint32_t)p; int hx, hy;
            if ((int16_t)rd16(pl + 0xB8) <= 0) continue;
            hx = (((int)rd16(pl + 2) + 8) >> 4) & 31; hy = (((int)rd16(pl + 4) + 8) >> 4) & 31;
            if (((hx - (x - 4)) & 31) <= 9 && ((hy - (y - 5)) & 31) <= 9) {
                dragon_state = 1; dragon_count = 49;
                g2_say(188);                           /* narration: "Dragon" (sound test 213: the dragon wakes) */
                if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld the dragon wakes (hero %d at (%d,%d))\n", cur_frame, p, hx, hy);
                break;
            }
        }
        dragon_look(0); return;
    }
    if (dragon_state == 1) {                           /* waking */
        if (--dragon_count <= 0) { dragon_state = 2; dragon_pat = rd16(0xFFE7E8) % 5; dragon_step = 0; }
        dragon_look(0); return;
    }
    if (dragon_stun > 0) { dragon_stun--; dragon_look(v & ~1); return; }   /* stunned by magic: holds still */
    if (dragon_turn > 0) {                             /* turning */
        if (--dragon_turn == 0) dragon_face = dragon_turn_to;
        dragon_look(v); return;
    }
    if ((dragon_step & 7) == 0) {
        int ox = 0, oy = 0, held = 0;
        if (v & 1) {                                   /* a breath step: fire from the mouth */
            int hc, mx, my;
            static const int out[4][2] = {{-1, 0}, {1, 0}, {0, 1}, {0, -1}};
            dragon_look(v);
            hc = dragon_block(dragon_head());
            mx = (hc & 31) * 16 + 8 + 8 * out[dragon_face][0]; my = (hc >> 5) * 16 + 8 + 8 * out[dragon_face][1];
            if (!dragon.cool) {
                int sl = fireball_at_hero(mx & 0x1FF, my & 0x1FF, 3 * 16 + 8, 8);   /* within 3 squares (user, footage) */
                if (sl) {
                    dragon.fb = 0xFFA834 + 0x1Cu * (uint32_t)(sl - 1); dragon.fb_x0 = mx; dragon.fb_y0 = my;
                    dragon.cool = 8; dragon.breath = 8;
                    if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld the dragon breathes fire\n", cur_frame);
                }
            }
            if (dragon_nearest(mx, my, &ox, &oy)) {    /* the hero in its line of fire: hold the pose */
                int along = dragon_face == 0 ? -ox : dragon_face == 1 ? ox : dragon_face == 2 ? oy : -oy;
                int side = dragon_face >= 2 ? ox : oy;
                held = along >= -8 && along <= 3 * 16 + 8 && abs(side) <= 17;
            }
        }
        if (!held && dragon_nearest(cx, cy, &ox, &oy)) {   /* face the nearest hero, if that side is open */
            int f = abs(ox) < abs(oy) ? (oy > 0 ? 2 : 3) : abs(ox) > abs(oy) ? (ox > 0 ? 1 : 0) : dragon_face;
            int ax = f == 0 ? x - 1 : f == 1 ? x + 2 : x, ay = f == 2 ? y + 1 : f == 3 ? y - 2 : y;
            int bx = f >= 2 ? ax + 1 : ax, by = f >= 2 ? ay : ay - 1;
            if (getenv("G4DRAGONLOG") && f != dragon_face) fprintf(stderr, "f%ld dragon: hero to the %c, squares (%d,%d) %s (%d,%d) %s\n", cur_frame, "WESN"[f], ax & 31, ay & 31, acid_free(ax, ay) ? "open" : "blocked", bx & 31, by & 31, acid_free(bx, by) ? "open" : "blocked");
            if (f != dragon_face && acid_free(ax, ay) && acid_free(bx, by)) {
                dragon_turn = 16; dragon_turn_to = f;
                if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld the dragon turns %c\n", cur_frame, "WESN"[f]);
            }
        }
        if (held) { dragon_look(v); return; }
    }
    dragon_step = (dragon_step + 1) & 127;
    dragon_look(v);
}

static void dragon_hit(M68K *c, int cell, long frame)
{
    int k;
    if (getenv("G4DRAGONLOG") && dragon.cell >= 0) fprintf(stderr, "f%ld shot at (%d,%d), head square (%d,%d)\n", frame, cell & 31, cell >> 5, dragon_block(dragon_head()) & 31, dragon_block(dragon_head()) >> 5);
    if (dragon.cell < 0 || cell != dragon_block(dragon_head())) return;   /* only where its head is now */
    {   static long last; if (frame == last) return; last = frame; }   /* one hit per frame (a shot is
                                                          checked against the cell more than once) */
    if (getenv("G4MODDEBUG")) fprintf(stderr, "dragon hit in the head: %d left\n", dragon.hp - 1);
    g2_fx_pitch(58, 85 + rand() % 31);                   /* its head struck: the pitch changes each time (user;
                                                            sound only -- online play is not affected) */
    if (--dragon.hp > 0) return;
    for (k = 0; k < 4; k++) { g2_cell[dragon_block(k)] = 0; set_cell(c, dragon_block(k), 0x00); }
    /* it leaves a hidden (power) potion and treasure or cider (user, from the arcade footage) */
    set_cell(c, dragon_block(0), (rd16(0xFFE7E8) & 1) ? 0x1E : 0x25);
    set_cell(c, dragon_block(1), 0x26 + rd16(0xFFE7E8) % 6);
    dragon.fb = 0;
    snprintf(banner, sizeof banner, "THE DRAGON IS SLAIN"); banner_until = frame + 180;
    if (getenv("G4MODDEBUG")) fprintf(stderr, "dragon slain\n");
    dragon.cell = -1;
}

/* Exit rules (Gauntlet II, set up at level start by 0x43D8C(0) when the maze has rule 0x8000 or
   0x4000): of several exits one, picked at random, is real; the others are removed -- or with
   rule 0x40 left as fakes (they look like exits but do nothing: "FAKE EXIT", "FIND REAL EXIT").
   With 0x4000 the real exit moves ("THE EXIT WILL MOVE", 0x5287C): every 300 frames it jumps to
   another of the original spots, index + step (table 0x5B7FC by the number of spots). */
static const uint8_t exit_step[24] = {0, 1, 1, 2, 3, 3, 5, 3, 3, 5, 7, 5, 5, 5, 5, 7, 7, 7, 7, 7, 7, 5, 7, 7};
static void exits_init(M68K *c)
{
    int i, fake = g2_cell[5] == 28;
    exits_done = 1;
    if (g2_cell[3] != 26) return;
    for (i = 32; i < 1024 && n_exits < 256; i++)
        if (rd8(0xFFE246 + (uint32_t)i) == 0x04 && g2_cell[i] != 18) exit_pos[n_exits++] = (short)i;
    if (n_exits < 2) return;
    exit_real = (int)((rd16(0xFFE7E8) * 7 + 3) % n_exits);
    for (i = 0; i < n_exits; i++) {
        if (i == exit_real) continue;
        if (fake) g2_cell[exit_pos[i]] = 29;               /* a fake: stays drawn as an exit */
        else set_cell(c, exit_pos[i], 0x00);
    }
    exit_timer = 300;
    if (getenv("G4MODDEBUG")) fprintf(stderr, "exits: %d spots, real (%d,%d)%s%s\n", n_exits, exit_pos[exit_real] & 31, exit_pos[exit_real] >> 5, fake ? ", others fake" : "", g2_cell[4] == 27 ? ", moving" : "");
}
static void exits_tick(M68K *c)
{
    int k, step, next, cell;
    if (fake_cool > 0) fake_cool--;
    if (exit_real < 0 || g2_cell[4] != 27 || n_exits < 2) return;
    if (--exit_timer > 0) return;
    exit_timer = 300;
    step = exit_step[n_exits < 24 ? n_exits : 23]; if (!step) step = 1;
    for (k = 0; k < n_exits; k++) {                       /* the next free spot by the step */
        next = (exit_real + step * (k + 1)) % n_exits;
        cell = exit_pos[next];
        if (next != exit_real && rd8(0xFFE246 + (uint32_t)cell) == 0x00 && !rd32(0xFFC246 + 4u * (uint32_t)cell)) break;
    }
    if (k == n_exits) return;
    set_cell(c, exit_pos[exit_real], 0x00);
    set_cell(c, cell, 0x04);
    exit_real = next; exit_moves++;
    g2_fx(49);                                           /* moving exit (sound test 049) */
    if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld the exit moves to (%d,%d)\n", cur_frame, cell & 31, cell >> 5);
}

/* 0x3D9A, the camera aim: runs once every frame of play (not in the pause loop or while a hint
   box holds the game) -- the "a maze is being played" tick, with the game's context for set_cell.
   (0x67BE, the heroes' tile routine, is skipped while a hero holds fire, so it cannot be the tick.) */
static void (*orig_play_tick)(M68K *);
/* The thief: Gauntlet 4's own (record $FFA362; set up per level by 0xF41C -- only from level 8,
   after the richest hero, with a countdown by level and difficulty -- and run by 0xE654: type 0
   waiting, 0x1C about). Gauntlet II's thief comes back again and again, so in Gauntlet II mode
   the set-up is run once more whenever the thief has been gone (or held back) for 30 seconds. */
/* The mugger (Gauntlet II, traced 2026-11-22): each time a thief is set up (0x4E4D8) it may be a
   mugger instead. Per level, flags 0x10 (a theft happened) and 0x20 (a mugging happened): with no
   mugging yet it is a mugger half the time (random(32) < 16), otherwise -- and in the other half --
   a mugger only if there has been a theft. So: the first is 50/50, after a theft always a mugger,
   after a mugging a thief (unless there was a theft too). A mugger touching a hero takes 100
   health instead of an item (0x4E22A; not below 0) -- "KILL MUGGER TO RECOVER FOOD" -- and shot,
   it drops a bottle of cider (user). The arcade's mugger is also slower, 0x180 against 0x200: see thief_frame. Here it is
   Gauntlet 4's own thief with a mugger flag: its steal (0xF1CE, reached from the contact check
   0xF01C) runs as usual and the hero's items are put back, and its drop (0xFA68) becomes cider. */
static int mugger, mugger_loot, thief_flags, thief_flags_level = -1;
static void (*orig_thief_setup)(M68K *), (*orig_thief_contact)(M68K *), (*orig_thief_drop)(M68K *);
static void thief_setup(M68K *c)                       /* 0xF41C */
{
    orig_thief_setup(c);
    if (!level_set_g2 || !g2_active) { mugger = 0; return; }
    if (thief_flags_level != rd16(0xFFE760)) { thief_flags_level = rd16(0xFFE760); thief_flags = 0; }
    if (!(thief_flags & 0x20) && rd16(0xFFE7E8) % 32 < 16) mugger = 1;
    else mugger = (thief_flags & 0x10) != 0;
    if (getenv("G4MUGGER")) mugger = atoi(getenv("G4MUGGER"));   /* test aid: 1 always a mugger, 0 never */
    mugger_loot = 0;
    if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld thief set up: %s\n", cur_frame, mugger ? "a MUGGER" : "a thief");
}
static void thief_contact(M68K *c)                     /* 0xF01C: the thief meets the object in d1 */
{
    uint32_t t = 0xFFA362, hero = c->d[1] & 0xFFFFFF;
    int steals = (int32_t)c->d[1] < 0 && !rd8(t + 0x4A) && hero >= 0xFF0040 && hero < 0xFF0040 + 4 * 0x2C4;
    static const uint8_t items[9] = {0x96, 0x98, 0x9A, 0x9C, 0x9E, 0xA0, 0xCA, 0xCC, 0xCE};   /* what 0xF1CE takes
                                                     from: power potions +$96.. (by table 0xFBCE), +$CA, keys, potions */
    uint16_t keep[9]; int k;
    if (!steals || !level_set_g2) { orig_thief_contact(c); return; }
    for (k = 0; k < 9; k++) keep[k] = rd16(hero + items[k]);
    orig_thief_contact(c);
    if (mugger) {
        int16_t hp = (int16_t)rd16(hero + 0xB8);
        for (k = 0; k < 9; k++) wr16(hero + items[k], keep[k]);   /* nothing taken... */
        wr16(hero + 0xB8, (uint16_t)(hp > 100 ? hp - 100 : 0));                       /* ...but 100 health */
        mugger_loot = 1; thief_flags |= 0x20;
        snprintf(banner, sizeof banner, "KILL MUGGER TO RECOVER FOOD"); banner_until = cur_frame + 180;
        if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld the mugger takes 100 health from hero %d\n", cur_frame, (int)((hero - 0xFF0040) / 0x2C4));
        g2_say(39); g2_say(40);                        /* sound test 100/101: the mugger's steal, "you can't catch me" */
    } else { thief_flags |= 0x10; g2_say(37); g2_say(38); }   /* sound test 098/099: the thief's */
}
/* The mugger's speed (Gauntlet II 0x4E552: a mugger moves at 0x180 against the thief's 0x200 --
   three quarters): Gauntlet 4's thief routine 0xE654 (called once a frame from 0xA848: spawn timer,
   chasing, fleeing) is skipped one frame in four while a mugger is out. The frame count is the same
   on every machine, so online play stays in step. */
static void (*orig_thief_frame)(M68K *);
static long thief_jump_until = -1;                    /* see thief_trail: the hero's teleport state is hidden from it */
/* The thief takes the shortest way (user, 2026-11-22: "always the shortest route to get to you, even if it
   means taking a transporter"). Gauntlet 4's thief steps by the footprint grid $FFA3B2: low nibble = the way
   on (a CLEAR bit: 0 up, 1 down, 2 left, 3 right; 0xF5CC writes 0xE/0xD/0xB/0x7), high nibble = the way back.
   While it chases (out, not appearing, nothing stolen), every 8 frames the low nibbles are rewritten as a
   map to its target: a breadth-first search back from the hero over floor cells, where a transporter
   (code 0x13) leads to the free cell beside the closest other transporter (Gauntlet 4 0x6D92 searches
   square rings 1..16; the on-screen check is left out). A thief beside a transporter its map points into
   is carried there. */
static int thief_dest_of(int tcell)                    /* the transporter a transporter sends to: closest other */
{
    int r, x0 = tcell & 31, y0 = tcell >> 5, dx, dy;
    for (r = 1; r <= 16; r++)
        for (dy = -r; dy <= r; dy++) for (dx = -r; dx <= r; dx++) {
            int x = (x0 + dx) & 31, y = (y0 + dy) & 31;
            if ((dx != -r && dx != r && dy != -r && dy != r) || y == 0) continue;
            if (rd8(0xFFE246 + (uint32_t)(y * 32 + x)) == 0x13) return y * 32 + x;
        }
    return -1;
}
static int thief_landing(int dcell)                    /* a free floor cell beside a transporter */
{
    static const int nd[4][2] = {{0, 1}, {1, 0}, {-1, 0}, {0, -1}}; int d;
    for (d = 0; d < 4; d++) {
        int x = (dcell & 31) + nd[d][0], y = (dcell >> 5) + nd[d][1], c;
        if (x < 0 || x > 31 || y < 1 || y > 31) continue;
        c = y * 32 + x;
        if (!rd8(0xFFE246 + (uint32_t)c) && !rd32(0xFFC246 + 4u * (uint32_t)c)) return c;
    }
    return -1;
}
static short thief_dist[1024];
static void thief_route(void)
{
    uint32_t t = 0xFFA362, h = rd32(t + 0x2E) & 0xFFFFFF;
    static short queue[1024]; static int land_of[1024]; int head = 0, tail = 0, k, hc;
    static const int nd[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};   /* up down left right = bits 0 1 2 3 */
    if (h < 0xFF0040 || h >= 0xFF0040 + 4 * 0x2C4) return;
    hc = ((((rd16(h + 4) + 8) & 0x1F0) >> 4) * 32) + (((rd16(h + 2) + 7) & 0x1F0) >> 4);
    for (k = 0; k < 1024; k++) { thief_dist[k] = -1; land_of[k] = -1; }
    for (k = 32; k < 1024; k++)                          /* each transporter: where taking it leaves you */
        if (rd8(0xFFE246 + (uint32_t)k) == 0x13) { int d = thief_dest_of(k); land_of[k] = d >= 0 ? thief_landing(d) : -1; }
    thief_dist[hc] = 0; queue[tail++] = (short)hc;
    while (head < tail) {
        int c = queue[head++], d;
        for (k = 32; k < 1024; k++)                      /* transporters that land here */
            if (land_of[k] == c && thief_dist[k] < 0) { thief_dist[k] = (short)(thief_dist[c] + 1); queue[tail++] = (short)k; }
        if (rd8(0xFFE246 + (uint32_t)c) == 0x13 && c != hc) {   /* a transporter is only entered, not walked across */
            for (d = 0; d < 4; d++) {
                int x = (c & 31) + nd[d][0], y = (c >> 5) + nd[d][1], n;
                if (x < 0 || x > 31 || y < 1 || y > 31) continue;
                n = y * 32 + x;
                if (thief_dist[n] < 0 && !rd8(0xFFE246 + (uint32_t)n)) { thief_dist[n] = (short)(thief_dist[c] + 1); queue[tail++] = (short)n; }
            }
            continue;
        }
        for (d = 0; d < 4; d++) {
            int x = (c & 31) + nd[d][0], y = (c >> 5) + nd[d][1], n, code;
            if (x < 0 || x > 31 || y < 1 || y > 31) continue;
            n = y * 32 + x; code = rd8(0xFFE246 + (uint32_t)n);
            if (thief_dist[n] >= 0 || (code && code != 0x13)) continue;
            if (code == 0x13) continue;                  /* reached only through land_of, above */
            thief_dist[n] = (short)(thief_dist[c] + 1); queue[tail++] = (short)n;
        }
    }
    for (k = 32; k < 1024; k++) {                         /* the way on: towards the nearer neighbour */
        int best = -1, bd = 0x7FFF, d; uint8_t m;
        if (thief_dist[k] <= 0 || rd8(0xFFE246 + (uint32_t)k)) continue;
        for (d = 0; d < 4; d++) {
            int x = (k & 31) + nd[d][0], y = (k >> 5) + nd[d][1], n;
            if (x < 0 || x > 31 || y < 1 || y > 31) continue;
            n = y * 32 + x;
            if (thief_dist[n] >= 0 && thief_dist[n] < bd) { bd = thief_dist[n]; best = d; }
        }
        if (best < 0) continue;
        m = rd8(0xFFA3B2 + (uint32_t)k);
        wr8(0xFFA3B2 + (uint32_t)k, (uint8_t)((m & 0xF0) | (0xF & ~(1 << best))));
    }
}
static void thief_route_frame(void)                    /* after the thief's routine, each frame */
{
    uint32_t t = 0xFFA362; int tc, d;
    static const int nd[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    if (!rd16(t) || rd8(t + 0x4C) || rd8(t + 0x4D) || rd16(t + 0x46)) return;   /* not chasing */
    if (cur_frame % 8 == 0) thief_route();
    tc = ((((rd16(t + 4) + 8) & 0x1F0) >> 4) * 32) + (((rd16(t + 2) + 7) & 0x1F0) >> 4);
    {   uint8_t m = rd8(0xFFA3B2 + (uint32_t)tc) & 15;  /* its map says: into a transporter beside it? */
        for (d = 0; d < 4; d++) {
            int x = (tc & 31) + nd[d][0], y = (tc >> 5) + nd[d][1], n, dc, land;
            if (m & (1 << d) || x < 0 || x > 31 || y < 1 || y > 31) continue;
            n = y * 32 + x;
            if (rd8(0xFFE246 + (uint32_t)n) != 0x13 || (dc = thief_dest_of(n)) < 0 || (land = thief_landing(dc)) < 0) continue;
            {   uint32_t slot = rd32(t + 0x12), ns = 0xFFC246 + 4u * (uint32_t)land;
                if (slot >= 0xFFC246 && slot < 0xFFC246 + 4096 && (rd32(slot) & 0xFFFFFF) == t) wr32(slot, 0);
                wr32(ns, t); wr32(t + 0x12, ns);
                wr16(t + 2, (uint16_t)((land & 31) * 16)); wr16(t + 4, (uint16_t)((land >> 5) * 16));
                if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld the thief takes the transporter at (%d,%d) to (%d,%d)\n", cur_frame, n & 31, n >> 5, land & 31, land >> 5);
                thief_route();
            }
            break;
        }
    }
}
static void thief_frame(M68K *c)
{
    if (getenv("G4THIEFLOG") && cur_frame % 10 == 0 && (rd16(0xFFA362) || getenv("G4THIEFLOG")[0] == '2'))
        fprintf(stderr, "f%ld %s type %04X held %02X flee %02X at %d,%d  hero %d,%d\n", cur_frame, mugger ? "mugger" : "thief", rd16(0xFFA362),
                rd8(0xFFA362 + 0x4C), rd8(0xFFA362 + 0x4D), rd16(0xFFA362 + 2), rd16(0xFFA362 + 4), rd16(0xFF0042), rd16(0xFF0044)),
        fprintf(stderr, "      $4A %02X hero+5A %04X +60 %02X $34 %d cellbyte %02X\n", rd8(0xFFA362 + 0x4A), rd16(0xFF0040 + 0x5A), rd8(0xFF0040 + 0x60), (int16_t)rd16(0xFFA362 + 0x34),
                rd8(0xFFA3B2 + (uint32_t)((((rd16(0xFFA362 + 4) + 8) & 0x1F0) << 1) + (((rd16(0xFFA362 + 2) + 7) & 0x1F0) >> 4))));
    if (mugger && level_set_g2 && g2_active && rd16(0xFFA362) && !rd8(0xFFA362 + 0x4C) && (cur_frame & 3) == 3) return;
    if (cur_frame < thief_jump_until && level_set_g2 && g2_active) {   /* just after a teleport: the thief follows on */
        uint32_t h = rd32(0xFFA362 + 0x2E) & 0xFFFFFF; uint16_t st;
        if (h >= 0xFF0040 && h < 0xFF0040 + 4 * 0x2C4) {
            st = rd16(h + 0x5A); wr16(h + 0x5A, 0); wr8(0xFFA362 + 0x4A, 0);
            orig_thief_frame(c);
            wr16(h + 0x5A, st);
            if (level_set_g2 && g2_active) thief_route_frame();
            return;
        }
    }
    orig_thief_frame(c);
    if (level_set_g2 && g2_active) thief_route_frame();
}
static void thief_drop(M68K *c)                        /* 0xFA68: the thief, shot, drops its loot; a mugger
                                                          drops a bottle of cider instead (0x25; user: on
                                                          purpose in the arcade, to punish trigger-happy
                                                          players -- he is meant to be fought hand to hand) */
{
    uint32_t t = 0xFFA362; int x, y, dx, dy; uint8_t before[25];
    if (!mugger_loot) { orig_thief_drop(c); return; }
    x = ((rd16(t + 2) + 7) >> 4) & 31; y = ((rd16(t + 4) + 8) >> 4) & 31;
    for (dy = -2; dy <= 2; dy++) for (dx = -2; dx <= 2; dx++) before[(dy + 2) * 5 + dx + 2] = rd8(0xFFE246 + (uint32_t)(((y + dy) & 31) * 32 + ((x + dx) & 31)));
    orig_thief_drop(c);
    for (dy = -2; dy <= 2; dy++) for (dx = -2; dx <= 2; dx++) {   /* what it dropped becomes food */
        int cell = ((y + dy) & 31) * 32 + ((x + dx) & 31);
        if (rd8(0xFFE246 + (uint32_t)cell) != before[(dy + 2) * 5 + dx + 2] && cell >= 32) { set_cell(c, cell, 0x25); dx = dy = 3; }
    }
    mugger_loot = 0;
    if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld the mugger drops cider\n", cur_frame);
}
static int thief_gone;
static void g4_thief_tick(M68K *c)
{
    uint32_t t = 0xFFA362;
    if (getenv("G4THIEFLOG") && cur_frame % 120 == 0 && !rd16(0xFF0040 + 0xCE)) wr16(0xFF0040 + 0xCE, 3);   /* test: wealth */
    if (getenv("G4THIEFLOG") && cur_frame % 120 == 0)
        fprintf(stderr, "f%ld level %d thief type %04X held %02X countdown %d at %d,%d entry %d,%d\n", cur_frame, rd16(0xFFE760), rd16(t),
                rd8(t + 0x4C), (int16_t)rd16(t + 0x48), rd16(t + 2) >> 4, rd16(t + 4) >> 4, (int16_t)rd16(t + 0x3A), (int16_t)rd16(t + 0x3C));
    if (in_room || rd16(0xFFE760) < 8) { thief_gone = 0; return; }
    if (rd16(t) || !rd8(t + 0x4C)) { thief_gone = 0; return; }   /* about, or counting down */
    if (++thief_gone < 3600) return;                    /* 90 s from one gone to the next one in (user, 2026-11-22:
                                                          the arcade's 60, longer to give players time to breathe):
                                                          60 s here + the 15 x 2 s countdown below */
    thief_gone = 0;
    wr32(t + 0x2E, 0);
    JSR(c, 0x0008F6, 0x00F41C);
    if (rd8(t + 0x4C) && rd32(t + 0x2E)) {               /* Gauntlet 4's dice said no, but there is a
                                                            target: Gauntlet II's thief comes anyway */
        wr8(t + 0x4C, 0); wr8(t + 0x4B, 0);
    }
    if (!rd8(t + 0x4C)) wr16(t + 0x48, 15);             /* always 15 x 2 s, so the wait is the same each time */
    if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld thief set up again: %s, countdown %d\n", cur_frame,
                                      rd8(t + 0x4C) ? "held back" : "coming", (int16_t)rd16(t + 0x48));
}
/* (Battle mode's panel, 0x23BD0 -- not reached in Arcade, whose names come from 0x1F6D0 below.)
   The side panel's hero block (0x23BD0, called per record from 0x23B86 with a2 = the 4-entry
   table 0x23DA4: per hero a name pointer and 12 bytes copied to +$2B8/+$2AC). It reads $B4 once
   for both the panel row (the player: row 5 * $B4 + 5) and the table entry (the hero), so it
   cannot go through class_sites.txt. With character select the routine draws as usual, then the
   chosen hero's name is printed over the row (blanked first: names are centred, 3-8 letters)
   and the hero's 12 bytes copied. */
/* Character select (Arcade and Record mode): on "SELECT PLAYER BY LEVER UP/DOWN" Gauntlet 4's
   up/down (0x1F416 / 0x1F47A, from the pad code 0x1EF02 / 0x1EF10) swaps the controller onto
   the previous / next hero's record (table 0x1DD5A, skipping heroes another controller holds).
   Here the controller keeps its own record (the player: P1 red, P2 blue, P3 yellow, P4 green)
   and up/down steps that player's hero class instead -- any hero, duplicates allowed -- and the
   panel is redrawn (0x1F6D0, whose names follow the class). Quest and Battle keep the original. */
/* Per-hero shot behaviour that the set-up 0x1D64A copies by record position, not by hero (table
   0x328CE, a column per hero): +$7C (0x1 Warrior/Wizard, 0x1000 Valkyrie/Elf; 0x20944 copies it to
   +$78 for the flying shot) and +$8C (shot animation frames mask 0x20938: 0x0F the Warrior's
   spinning axe, 1 the straight shots). With character select they come from the chosen hero. */
static void class_fields(int slot)
{
    extern int16_t g4_hero_class_tab[4];
    uint32_t pl = 0xFF0040 + 0x2C4u * (uint32_t)slot;
    int k = g4_hero_class_tab[slot] >= 0 ? g4_hero_class_tab[slot] & 3 : slot;
    wr16(pl + 0x7C, rd16(0x0328CE + 0x38 + 2u * (uint32_t)k));
    wr16(pl + 0x8C, rd16(0x0328CE + 0x40 + 2u * (uint32_t)k));
}
static void (*orig_hero_setup)(M68K *);
static void hero_setup(M68K *c)                        /* 0x1D64A */
{
    int p;
    orig_hero_setup(c);
    for (p = 0; p < 4; p++) class_fields(p);
}
static int class_select_mode(void) { uint16_t m = rd16(0xFFE838); return mods_enabled && (m == 0 || m == 3); }
static void (*orig_hero_up)(M68K *), (*orig_hero_down)(M68K *);
static void hero_step(M68K *c, int dir)
{
    extern int16_t g4_hero_class_tab[4];
    int slot = rd16(c->a[0] + 0xB4) & 3, cur = g4_hero_class_tab[slot] >= 0 ? g4_hero_class_tab[slot] : slot;
    M68K save = *c;
    g4_hero_class_tab[slot] = (int16_t)((cur + dir) & 3);
    class_fields(slot);
    JSR(c, 0x01EF06, 0x01F6D0);                          /* the panel, with the new name */
    save.a[7] = c->a[7]; save.a[6] = c->a[6]; *c = save;
    if (getenv("G4MODDEBUG")) fprintf(stderr, "player %d chooses hero %d\n", slot + 1, g4_hero_class_tab[slot]);
}
static void hero_up(M68K *c)   { if (class_select_mode()) hero_step(c, -1); else orig_hero_up(c); }
static void hero_down(M68K *c) { if (class_select_mode()) hero_step(c, +1); else orig_hero_down(c); }

/* --nomusic (for listening to sound effects and voices): the game's music routine 0x1F664
   (d1 = tune; it asks the sound code at 0xFE010 -> 0xFF524, $FFF004) does nothing. Sound effects
   and Gauntlet 4's voice go to the Z80 through 0xFE014/0xFE018, the Gauntlet II narration
   through the host's voice channel: both still play. */
int mods_no_music;
static void (*orig_sfx_a)(M68K *), (*orig_sfx_b)(M68K *);   /* 0xFE014 / 0xFE018: effects straight to the Z80 */
static void sfx_a(M68K *c) { if (!extract_music_active) orig_sfx_a(c); }
/* the other music commands (0x1F69E..0x1F6C6: $FFF00C = 3..7, fades/pauses): not while extracting */
static void (*orig_mcmd[6])(M68K *);
/* The recorded / MP3 music follows them too (callers: 0x1F6A8 at 0x1FBF6 = the pause loop starting,
   0x1F6B2 at 0x1FC4E/0x1FC5A = leaving it; 0x1F69E from the title and menus = fade out; 0x1F694 =
   stop, e.g. leaving a game). */
static void mus_follow(int what);
static void mcmd0(M68K *c) { if (!extract_music_active) { mus_follow(3); orig_mcmd[0](c); } }
static void mcmd1(M68K *c) { if (!extract_music_active) { mus_follow(4); orig_mcmd[1](c); } }
static void mcmd2(M68K *c) { if (!extract_music_active) { mus_follow(5); orig_mcmd[2](c); } }
static void mcmd3(M68K *c) { if (!extract_music_active) orig_mcmd[3](c); }
static void mcmd4(M68K *c) { if (!extract_music_active) orig_mcmd[4](c); }
static void mcmd5(M68K *c) { if (!extract_music_active) { mus_follow(1); orig_mcmd[5](c); } }   /* 0x1F694: $FFF00C = 1 (stop) */
static void sfx_b(M68K *c) { if (!extract_music_active) orig_sfx_b(c); }
/* 0xFE00C: Gauntlet 4's music sequencer, run by the vblank (it plays the tune on the 68000 and
   writes the sound chip itself). While extracting, the attract loop's mute flag $FFF011 (= not
   $FFE82A, set for its demo) is cleared right before it runs, or a note can be left hanging. */
static void (*orig_music_driver)(M68K *);
static void music_driver(M68K *c) { if (extract_music_active) wr8(0xFFF011, 0); orig_music_driver(c); }
static void (*orig_music)(M68K *);
static unsigned g2_fm_tune = 0x0A;                     /* the Gauntlet 4 level tune the game chose at level 1 */
static void music_fm(M68K *c, unsigned track)          /* ask the game's own sound code for a track */
{
    uint32_t d1 = c->d[1];
    c->d[1] = (d1 & ~0xFFFFu) | track; orig_music(c); c->d[1] = d1;
}
static void music_req(M68K *c)
{
    unsigned t = c->d[1] & 0xFFFF; uint32_t from = rd32(c->a[7]);
    if (extract_music_active) return;                  /* --extract-music: the extractor alone asks */
    if (getenv("G4MUSICLOG")) fprintf(stderr, "f%ld music request %02X (from %06X, mode %d, level %d, allowed %d/%d)\n", cur_frame, t, from, rd16(0xFFE838), rd16(0xFFE760), rd8(0xFFE82A), rd8(0xFFE820));
    if (mods_no_music) return;
    if (!rd8(0xFFE82A) || rd8(0xFFE820)) { orig_music(c); return; }   /* music off: the game's own checks */
    if (t == 0x14) { music_mp3_stop(); orig_music(c); return; }       /* stop */
    if (level_set_g2 && !level_set_g3 && from == 0x0028F4) {          /* Gauntlet II: the level music */
        int id = room_kind == 2 ? MUS_G2_TREASURE : room_kind == 1 ? MUS_G2_SECRET : MUS_G2_LEVEL;
        g2_music_room = room_kind; g2_fm_tune = t;
        if (music_mp3(id)) { music_fm(c, 0x14); return; }
    }
    if (t < 0x100 && music_mp3((int)t)) { music_fm(c, 0x14); return; }   /* Gauntlet 4 track t as MP3 */
    if (mus_cur >= 0) music_mp3_stop();
    orig_music(c);
}
/* Gauntlet II levels: entering or leaving a treasure / secret room changes the music (only then).
   Called at each level start from g2_order_maze's caller with the new room kind. */
static void g2_music_level(M68K *c, int kind)
{
    int id, was = g2_music_room;
    if (mods_no_music || was < 0 || kind == was) return;  /* no level music started yet, or no change */
    g2_music_room = kind;
    if (!rd8(0xFFE82A) || rd8(0xFFE820)) return;
    id = kind == 2 ? MUS_G2_TREASURE : kind == 1 ? MUS_G2_SECRET : MUS_G2_LEVEL;
    if (music_mp3(id)) { music_fm(c, 0x14); return; }
    if (mus_cur >= 0) music_mp3_stop();                  /* no file for this one: the game's level music */
    if (kind == 0 || was == 0) music_fm(c, g2_fm_tune);  /* the game's own level tune again */
}

/* Gauntlet II narration at Gauntlet 4's sound requests (0x1F616, d1 = sound; the user's sound
   test, 2026-11-20): 0-3 "<hero> joins" (Gauntlet 4's voice) -> "<colour> <hero>"; 0x11-0x14 the
   low-health beep of player 1-4 (kept) -> once per fall "<colour> <hero> needs food, badly"
   (Gauntlet II phrase 24; said again after health is back above 600). Gauntlet II level set only;
   every other sound is Gauntlet 4's; 0x1F-0x22 hero death -> the Gauntlet II death cry.
   Test aid G4VOICELOG logs the requests. */
static int lowhp_said[4], die_said[4];
static void (*orig_sound_req)(M68K *);
static void sound_req(M68K *c)
{
    int d = (int)(c->d[1] & 0xFFFF), slot;
    uint32_t r = c->a[0] - 0xFF0040u;
    if (extract_music_active) return;
    if (getenv("G4VOICELOG")) fprintf(stderr, "f%ld sound request %02X\n", cur_frame, d);
    if (!g2_voice || !level_set_g2 || level_set_g3) { orig_sound_req(c); return; }
    if (d <= 3) {                                      /* a player joins: the Gauntlet II voice instead */
        if (r >= 4u * 0x2C4u || r % 0x2C4u) r = c->a[5] - 0xFF0040u;
        slot = (r < 4u * 0x2C4u && r % 0x2C4u == 0) ? (int)(r / 0x2C4u) : d;
        audio_voice_stop(); g2_say_hero(slot);
        return;
    }
    if (d == 0x1D && n_super && magic_frame < 0) {    /* note the living super sorcerers */
        int k; magic_saved_n = 0;
        for (k = 0; k < n_super; k++) {
            uint32_t o = super_sorc[k];
            int ex, ey;
            if (rd16(o) != 0x0010) continue;
            ex = ((rd16(o + 2) + 8 - (int)rd16(0xFFE740) - 112 + 256) & 511) - 256;   /* in view? */
            ey = ((rd16(o + 4) + 8 - (int)rd16(0xFFE742) - 112 + 256) & 511) - 256;
            if (ex < -128 || ex > 128 || ey < -120 || ey > 120) continue;
            magic_saved_cell[magic_saved_n] = (((rd16(o + 4) + 8) >> 4) & 31) * 32 + (((rd16(o + 2) + 8) >> 4) & 31);
            magic_saved_k[magic_saved_n++] = k;
            if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld magic: super sorcerer %d at (%d,%d), hero at (%d,%d)\n", cur_frame, k,
                magic_saved_cell[magic_saved_n - 1] & 31, magic_saved_cell[magic_saved_n - 1] >> 5, (rd16(0xFF0042) + 8) >> 4, (rd16(0xFF0044) + 8) >> 4);
        }
        magic_frame = cur_frame;
    }
    if (d == 0x1D && dragon.cell >= 0) {               /* magic stuns the dragon (arcade legend): no fire */
        int ex = (((dragon.cell & 31) * 16 + 8 - (int)rd16(0xFFE740) - 112) + 256 & 511) - 256;
        int ey = ((((dragon.cell >> 5) & 31) * 16 - (int)rd16(0xFFE742) - 112) + 256 & 511) - 256;
        if (ex > -128 && ex < 128 && ey > -120 && ey < 120) {
            dragon_stun = 300; dragon.breath = 0;
            if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld magic stuns the dragon\n", cur_frame);
        }
    }
    if (d == 0x1D && n_acid) {                         /* magic (the potion sound): acid on screen */
        /* (GameFAQs arcade strategy guide, via the user) magic stuns acid puddles; magic again on a
           stunned puddle destroys it. Stun length assumed: 300 frames (5 s). */
        int i = 0, cx = rd16(0xFFE740) + 112, cy = rd16(0xFFE742) + 112;
        while (i < n_acid) {
            int ex = acid[i].x + 8 - cx, ey = acid[i].y + 8 - cy;           /* distance from the view's centre */
            if (rd8(0xFFE7B6)) ex = ((ex + 256) & 511) - 256;                  /* across the edge only when */
            if (rd8(0xFFE7B7)) ey = ((ey + 256) & 511) - 256;                  /* the maze wraps that way */
            if (ex < -128 || ex > 128 || ey < -120 || ey > 120) { i++; continue; }
            if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld magic: acid %d at (%d,%d) %s\n", cur_frame, i, acid[i].x >> 4, acid[i].y >> 4, acid[i].stun > 0 ? "destroyed" : "stunned");
            if (acid[i].stun > 0) { acid[i] = acid[--n_acid]; continue; }   /* destroyed */
            acid[i].stun = 300; i++;
        }
    }
    if (d >= 0x1F && d <= 0x22) {                      /* a hero dies: the Gauntlet II cry instead (user's sound
                                                          test 185-188: Valkyrie 157, Wizard 160, Warrior 162, Elf 158) */
        static const short cry[4] = {162, 157, 160, 158};
        g2_say(cry[d - 0x1F]);
        return;
    }
    orig_sound_req(c);
    if (d >= 0x11 && d <= 0x14 && !lowhp_said[d - 0x11]) {
        slot = d - 0x11; lowhp_said[slot] = 1;
        g2_say_hero(slot); g2_say(24);                 /* after whatever is being said */
    }
}

/* The panel's hero names (0x1F6D0 prints them per player from fixed print records 0x34A92 +
   26 * player: long, column, row, pointer to the name's picture tiles, then the SCORE/HEALTH line).
   With character select each record's name pointer is the chosen hero's. */
static void (*orig_panel_names)(M68K *);
static void panel_names(M68K *c)
{
    extern int16_t g4_hero_class_tab[4];
    static uint32_t name_ptr[4]; static int saved;
    int p;
    if (!saved) { for (p = 0; p < 4; p++) name_ptr[p] = rd32(0x034A92 + 26u * (uint32_t)p + 8); saved = 1; }
    static uint32_t blank_rec;
    M68K save;
    for (p = 0; p < 4; p++) {
        int k = g4_hero_class_tab[p] >= 0 ? g4_hero_class_tab[p] & 3 : p;
        wr32(0x034A92 + 26u * (uint32_t)p + 8, name_ptr[k]);
    }
    /* blank the name rows first: the names are 2-5 picture tiles wide, so a shorter one would
       leave part of the last one behind (a print record: long, column, row, text; then -1) */
    if (!blank_rec) { uint32_t t = put_string("        "); blank_rec = put_long(0); put_long(0); put_long(t); put_long(0xFFFFFFFFu); }
    save = *c;
    for (p = 0; p < 4; p++) {
        uint32_t r = 0x034A92 + 26u * (uint32_t)p;
        wr32(blank_rec, rd32(r)); wr16(blank_rec + 4, rd16(r + 4)); wr16(blank_rec + 6, rd16(r + 6));
        c->a[7] -= 4; wr32(c->a[7], blank_rec);
        JSR(c, 0x01F7E0, 0x01CCFA);
        c->a[7] += 4;
    }
    save.a[7] = c->a[7]; save.a[6] = c->a[6]; *c = save;
    orig_panel_names(c);
}
static void (*orig_panel_hero)(M68K *);
static void panel_hero(M68K *c)
{
    extern int16_t g4_hero_class_tab[4];
    uint32_t a0 = c->a[0], ent, a2 = c->a[2];
    int slot = rd16(a0 + 0xB4), cls, k;
    static uint32_t blank;
    M68K save;
    orig_panel_hero(c);
    if (slot < 0 || slot > 3 || g4_hero_class_tab[slot] < 0 || g4_hero_class_tab[slot] == slot) return;
    cls = g4_hero_class_tab[slot] & 3;
    ent = a2 + 16u * (uint32_t)cls;
    for (k = 0; k < 12; k++) { wr8(a0 + 0x2B8 + (uint32_t)k, rd8(ent + 4 + (uint32_t)k)); wr8(a0 + 0x2AC + (uint32_t)k, rd8(ent + 4 + (uint32_t)k)); }
    if (!blank) blank = put_string("        ");
    save = *c;
    for (k = 0; k < 2; k++) {                         /* blank the row, then the hero's name */
        c->a[0] = a0; c->a[1] = k ? rd32(ent) : blank;
        c->d[3] = (uint32_t)(slot * 5 + 5); c->d[2] = 2; c->d[1] = rd16(a0 + 0xD6);
        JSR(c, 0x023C12, 0x01D0F2);
    }
    save.a[7] = c->a[7]; save.a[6] = c->a[6]; *c = save;
}

/* The thief's trail (0xF5CC(d2 x, d3 y in pixels), called from the hero code 0x5EFA): the thief
   record keeps the hero's last two cells ($3E/$40, $42/$44) and picks a step routine from table
   0xF78E by the move between them -- which must be at most one cell each way (5-bit wrap-safe
   difference). Gauntlet II's transportability (two cells through a wall) and the lone transporter
   jump further: the index then leaves the table and the game jumps into nowhere. So after a
   jump the trail starts again at the new cell. */
static void (*orig_thief_trail)(M68K *);
static void thief_trail(M68K *c)
{
    uint32_t t = 0xFFA362;
    int x = (int)((((c->d[2] & 0xFFFF) + 7) & 0x1F0) >> 4), y = (int)((((c->d[3] & 0xFFFF) + 8) & 0x1F0) >> 4);
    int dx = ((x - (int)rd16(t + 0x3E)) & 31), dy = ((y - (int)rd16(t + 0x40)) & 31);
    if (dx > 16) dx -= 32;
    if (dy > 16) dy -= 32;
    if ((dx < -1 || dx > 1 || dy < -1 || dy > 1) && !getenv("G4NOTRAILFIX")) {
        /* The thief follows through a jump (user, 2026-11-22: as in the arcade). The footprints need steps of
           one cell, so the jump is laid down as a walk: the shortest walkable route from the cell left to the
           cell landed on (floor and pickups, up/down/left/right), each step fed to 0xF5CC as if the hero had
           walked it. No route (a closed-off part of the maze): the trail restarts there and the thief, at its
           end, gives up as before. */
        static short prev[1024]; static short queue[1024];
        int from = (int)(rd16(t + 0x40) & 31) * 32 + (int)(rd16(t + 0x3E) & 31), to = y * 32 + x, head = 0, tail = 0, k, found = 0;
        for (k = 0; k < 1024; k++) prev[k] = -1;
        prev[from] = (short)from; queue[tail++] = (short)from;
        while (head < tail && !found) {
            int cl = queue[head++], cx = cl & 31, cy = cl >> 5, d;
            static const int nd[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (d = 0; d < 4; d++) {
                int nx = cx + nd[d][0], ny = cy + nd[d][1], n, code;
                if (nx < 0 || nx > 31 || ny < 1 || ny > 31) continue;
                n = ny * 32 + nx;
                if (prev[n] >= 0) continue;
                code = rd8(0xFFE246 + (uint32_t)n);
                if (n != to && code && !(code >= 0x13 && code <= 0x2F)) continue;   /* walls, doors, exits, ... */
                prev[n] = (short)cl; queue[tail++] = (short)n;
                if (n == to) { found = 1; break; }
            }
        }
        if (found) {
            int path[1024], np = 0, cl; uint32_t d2 = c->d[2], d3 = c->d[3];
            for (cl = to; cl != from && np < 1024; cl = prev[cl]) path[np++] = cl;
            for (k = np - 1; k >= 1; k--) {             /* every cell between: one step each */
                c->d[2] = (c->d[2] & 0xFFFF0000u) | (uint32_t)((path[k] & 31) * 16);
                c->d[3] = (c->d[3] & 0xFFFF0000u) | (uint32_t)((path[k] >> 5) * 16);
                orig_thief_trail(c);
            }
            c->d[2] = d2; c->d[3] = d3;
            wr8(t + 0x4A, 0);                            /* Gauntlet 4 makes a teleport shake the thief off (the hero's
                                                            $5A state sets its give-up flag $4A, 0xE75A): not here */
            thief_jump_until = cur_frame + 180;
            if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld thief trail laid across a jump: (%d,%d) -> (%d,%d), %d steps\n", cur_frame, from & 31, from >> 5, x, y, np);
        } else {
            wr16(t + 0x3E, (uint16_t)x); wr16(t + 0x40, (uint16_t)y); wr16(t + 0x42, (uint16_t)x); wr16(t + 0x44, (uint16_t)y);
            if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld thief trail restarted at (%d,%d) after a jump (no way through)\n", cur_frame, x, y);
        }
    }
    orig_thief_trail(c);
}
static void play_tick(M68K *c)
{
    if (rd16(0xFFE7EA)) { orig_play_tick(c); return; } /* a hint box holds the game (0x958): everything
                                                          waits, as Gauntlet 4's own monsters and thief do */
    play_frame = cur_frame;
    if (getenv("G4SAYALL") && cur_frame >= atol(getenv("G4SAYALL")) && (cur_frame - atol(getenv("G4SAYALL"))) % 150 == 0) {
        long n = (cur_frame - atol(getenv("G4SAYALL"))) / 150;   /* test aid: speak phrase n (0x1F616), one every 2.5 s */
        if (n < 0x34) { M68K save = *c; c->a[0] = 0xFF0040; c->d[1] = (uint32_t)n; JSR(c, 0x003DA0, 0x01F616);
                        save.a[7] = c->a[7]; save.a[6] = c->a[6]; *c = save; fprintf(stderr, "f%ld say %ld\n", cur_frame, n); }
    }
    {   const char *fa = getenv("G4FXAT");               /* test aid: G4FXAT=frame,n plays effect n once */
        if (fa && cur_frame == atol(fa) && strchr(fa, ',')) g2_fx(atoi(strchr(fa, ',') + 1)); }
    if (g3_poison_pending && level_set_g3 && g2_active) {   /* GAUNTLET III: its poisoned food (extras 26) */
        int i; g3_poison_pending = 0; n_g3_poison = 0;
        for (i = 32; i < 1024 && n_g3_poison < 64; i++)
            if (g2_cell[i] == 26) {
                uint32_t e = rd32(0xFFC246 + 4u * (uint32_t)i);
                if (e && !(e & 0x80000000u)) { g3_poison[n_g3_poison].obj = e & 0xFFFFFF; g3_poison[n_g3_poison].cell = i; n_g3_poison++; }
            }
        if (getenv("G4MODDEBUG")) fprintf(stderr, "GAUNTLET III: %d poisoned food\n", n_g3_poison);
    }
    if (item_pending && level_set_g2 && !level_set_g3 && g2_active && !in_room) {   /* the extra item (see poison_obj) */
        uint32_t r = (uint32_t)rd16(0xFFE7E8) * 31u + rd16(0xFFE7E4);
        item_pending = 0;
        const char *force = getenv("G4EXTRAITEM");     /* test aid: potion / poison, always */
        if ((rd16(0xFFE760) >= 3 && (r % 64) < 24) || force) {
            int poison = force ? force[1] == 'o' && force[2] == 'i' : ((r / 64) % 64) >= 32, k, cell = -1;
            for (k = 0; k < 400 && cell < 0; k++) {
                int t = (int)((r * 7919u + (uint32_t)k * 613u) % 992u) + 32;
                if (!rd8(0xFFE246 + (uint32_t)t) && !rd32(0xFFC246 + 4u * (uint32_t)t) && !g2_cell[t]) cell = t;
            }
            if (cell >= 0) {
                set_cell(c, cell, poison ? 0x25 : 0x23);       /* poison on 0x25: the food shots destroy */
                if (poison) { poison_cell = cell; poison_obj = rd32(0xFFC246 + 4u * (uint32_t)cell) & 0xFFFFFF; g2_cell[cell] = 26; }
                if (getenv("G4MODDEBUG")) fprintf(stderr, "extra item: %s at (%d,%d)\n", poison ? "poison" : "potion", cell & 31, cell >> 5);
            }
        }
    }
    if (slow_timer > 0) slow_timer--;
    if (magic_frame >= 0 && cur_frame - magic_frame >= 2) {   /* super sorcerers killed by magic come back */
        int j;
        for (j = 0; j < magic_saved_n; j++) {
            int k = magic_saved_k[j], cell = magic_saved_cell[j];
            uint32_t e;
            if (rd16(super_sorc[k]) == 0x0010) {         /* not killed (e.g. blinked out): stunned all the same */
                if (super_cool[k] < 300) super_cool[k] = 300;
                if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld super sorcerer %d stunned by magic\n", cur_frame, k);
                continue;
            }
            if (rd32(0xFFC246 + 4u * (uint32_t)cell)) continue;   /* the square is taken */
            spawn_monster(c, cell, 0x9A);
            e = rd32(0xFFC246 + 4u * (uint32_t)cell);
            if (e && !(e & 0x80000000u) && rd16(e & 0xFFFFFF) == 0x0010) { super_sorc[k] = e & 0xFFFFFF; super_cool[k] = 300; }
            if (getenv("G4MODDEBUG")) fprintf(stderr, "f%ld super sorcerer %d stunned, not killed, by magic\n", cur_frame, k);
        }
        magic_frame = -1;
    }
    if (rule_say && level_set_g3 && g2_active) {        /* GAUNTLET III: the Atari's messages */
        rule_say = 0;
        if (g3_troom) ;                                /* room_tick shows "TREASURE ROOM: nn SECONDS" */
        else if (pvp_rule & 2) snprintf(banner, sizeof banner, "SHOTS NOW HURT OTHER PLAYERS");
        else if (pvp_rule & 1) snprintf(banner, sizeof banner, "SHOTS NOW STUN OTHER PLAYERS");
        if (!g3_troom && pvp_rule) banner_until = cur_frame + 240;
    }
    if (rule_say && level_set_g2 && g2_active) {        /* narration: the maze's rules, once at its start */
        rule_say = 0;
        if (rd16(0xFFE760) > 8 && !in_room) {          /* after level 8, sometimes (user, arcade): a taunt */
            static const short taunt[3] = {17, 14, 32};  /* "try this level now", "try and find the way out",
                                                           "let's see you get out of here" */
            unsigned r = (unsigned)(rd16(0xFFE7E8) ^ (uint16_t)cur_frame) % 6u;
            if (r < 3) g2_say(taunt[r]);               /* half the time: nothing */
        }
        if (inv_rule) g2_say(179);                     /* "some walls are invisible" */
        if (pvp_rule & 2) g2_say(112);                 /* "your shots now hurt other players" */
        else if (pvp_rule & 1) g2_say(98);             /* "your shots now stun other players" (sound test 140) */
        if (g2_cell[4] == 27) g2_say(180);             /* "exits may move around" (sound test 206) */
    }
    if (rnd_msg && level_set_g2 && g2_active) { snprintf(banner, sizeof banner, "%s", rnd_msg); banner_until = cur_frame + 240; rnd_msg = NULL; }
    if (level_set_g2 && g2_active && !super_found) find_super_sorcerers();
    if (level_set_g2 && g2_active && !dragon.placed) dragon_place(c);
    if (level_set_g2 && g2_active) dragon_tick();
    if (level_set_g2 && g2_active && !exits_done) exits_init(c);
    if (level_set_g2 && g2_active) exits_tick(c);
    if (level_set_g2 && g2_active && in_room) room_tick(c);
    if (level_set_g2 && g2_active && !level_set_g3) idle_tick(c);
    if (level_set_g2 && g2_active && !level_set_g3) g4_thief_tick(c);
    if (level_set_g2 && cycle_frame != cur_frame && g2_level_features()) { cycle_frame = cur_frame; cycle_walls(c); random_walls(c); }
    if (drop_item && level_set_g2 && g2_active) {      /* the thief's loot, dropped where it died */
        int cell = drop_item >> 8;
        if (rd8(0xFFE246 + (uint32_t)cell) == 0x00) set_cell(c, cell, drop_item & 0xFF);
        drop_item = 0;
    }
    orig_play_tick(c);
}

static void (*orig_player_tiles)(M68K *);
static void player_tiles(M68K *c)                        /* 0x67BE, per hero: wall pushing */
{
    if (level_set_g2 && g2_active && c->a[0] >= 0xFF0040 && c->a[0] < 0xFF0040 + 4 * 0x2C4) {
        int p = (int)((c->a[0] - 0xFF0040) / 0x2C4);
        if (!stun_timer[p]) push_wall(c, p);
    }
    orig_player_tiles(c);
}

/* drawn over the maze (plane B world pixel wx, wy in the 512x512 plane); 0 = nothing */
uint32_t mods_overlay_px(int wx, int wy, long frame)
{
    int f, x = wx & 15, y = wy & 15;
    mods_overlay_calls++;
    mods_pic_id = -1; mods_pic_scale = 256;
    if (!g2_active) return 0;
    if (frame - play_frame > 600 && !in_pause_loop) return 0;  /* only while a maze is played (hint boxes
                                                                  stop the player logic for a while) */
    f = g2_cell[((wy >> 4) & 31) * 32 + ((wx >> 4) & 31)];
    if (f) mods_overlay_hits++;
    if (f == 1) {                                         /* stun tile: a lattice of sparks */
        pic_at(SH_STUN, x, y);
        if (x > 1 && x < 14 && y > 1 && y < 14 && ((x + y) & 3) == 0) return 0x1000000u | 0xF0D040u;
        return 0;
    }
    if (exit_real >= 0 && g2_cell[4] == 27 && exit_timer <= 32 && ((frame >> 2) & 1) &&   /* the exit blinks */
        ((wy >> 4) & 31) * 32 + ((wx >> 4) & 31) == exit_pos[exit_real])                  /* before it jumps */
        return 0x1000000u | 0x101010;
    if (dragon.fb) {                                      /* its fireball: a 2x2-square blaze (user, footage) */
        int fx = rd16(dragon.fb + 4) >> 6, fy = rd16(dragon.fb + 6) >> 6;
        int ex = ((wx - fx + 256) & 511) - 256, ey = ((wy - fy + 256) & 511) - 256, r = ex * ex + ey * ey;
        if (ex >= -16 && ex < 16 && ey >= -16 && ey < 16) pic_at(SH_FIREBALL, ex + 16, ey + 16);
        if (r <= 15 * 15) {
            int fl = (int)((frame >> 1) + (unsigned)(ex * 7 + ey * 13)) & 3;
            return 0x1000000u | (r <= 36 ? 0xFFF0A0 : r <= 100 ? (fl ? 0xFFD040 : 0xFFF0A0) : (fl & 1 ? 0xFF6010 : 0xE02000));
        }
    }
    if (dragon.cell >= 0 && f == 25) {                   /* the dragon: 32x32 over its block */
        int ox = (dragon.cell & 31) * 16, oy = (((dragon.cell >> 5) - 1) & 31) * 16;
        int ax = (wx - ox) & 0x1FF, ay = (wy - oy) & 0x1FF, ex, ey, flick = (int)(frame >> 2) & 1;
        if (ax < 32 && ay < 32 && g2pic_find(1) >= 0) {     /* Gauntlet II's own dragon pictures */
            int pk = g2pic_find(dragon_pic) >= 0 ? g2pic_find(dragon_pic) : g2pic_find(1);
            const uint8_t *q = g2pic[pk].px + (ay * 32 + ax) * 4;
            pic_pk(pk, ax, ay);
            if (dragon.breath) {                          /* fire at the mouth, on the side it faces */
                int along = dragon_face >= 2 ? ax : ay, out = dragon_face == 0 ? ax : dragon_face == 1 ? 31 - ax : dragon_face == 2 ? 31 - ay : ay;
                if (out < 3 && along >= 4 + 7 * dragon_aim && along <= 8 + 7 * dragon_aim) {
                    mods_pic_id = -1;                     /* the fire stays on top of a pack's dragon */
                    return 0x1000000u | ((ax + ay + flick) & 1 ? 0xFFD040 : 0xFF6010);
                }
            }
            if (q[3]) {                                  /* darker as it is hurt (user, from the arcade
                                                           footage): bright, dark, very dark red */
                int sc = dragon.hp > 13 ? 256 : dragon.hp > 6 ? 165 : 100;
                mods_pic_scale = sc;
                return 0x1000000u | (uint32_t)((q[0] * sc >> 8) << 16 | (q[1] * sc >> 8) << 8 | (q[2] * sc >> 8));
            }
            return 0x1000000u | 0x24180E;                                          /* shadowed floor */
        }
        if (ax < 32 && ay < 32) {
            /* head (left, upper), with an eye; body (right, lower); wings; fire when breathing */
            ex = ax - 9; ey = ay - 9;
            if (ex * ex + ey * ey <= 49) {
                if ((ax == 7 || ax == 8) && ay == 7) return 0x1000000u | (flick ? 0xFFFF40 : 0xFF2020);
                return 0x1000000u | (ex * ex + ey * ey <= 20 ? 0x50C040 : 0x2E7A24);
            }
            if (dragon.breath && ax < 5 && ay >= 9 && ay <= 13) return 0x1000000u | ((ax + ay + flick) & 1 ? 0xFFD040 : 0xFF6010);
            ex = ax - 19; ey = (ay - 21) * 4 / 3;
            if (ex * ex + ey * ey <= 121) return 0x1000000u | (ey > 6 ? 0x6ACB50 : (ex * ex + ey * ey <= 40 ? 0x3E9A30 : 0x2E7A24));
            if (ay >= 3 && ay <= 14 && ax >= 16 && ax <= 30 && (ax - 16) >= (14 - ay) * 1)
                return 0x1000000u | ((ax + ay) % 4 ? 0x1E5A18 : 0x2E7A24);           /* wing */
            if (ay >= 26 && (ax == 12 || ax == 13 || ax == 24 || ax == 25)) return 0x1000000u | 0x2E7A24;   /* legs */
            return 0x1000000u | 0x24180E;                                           /* shadowed floor */
        }
    }
    if (thief.state == 1 || thief.state == 2) {          /* the thief: a hooded figure with a sack */
        int ax = ((wx - thief.x) & 0x1FF), ay = ((wy - thief.y) & 0x1FF);
        if (ax < 16 && ay < 16) {
            static const char *fig[16] = {
                "......####......", ".....######.....", "....###..###....", "....##w##w##....",
                ".....######.....", "....########....", "...##########...", "..####.##.####..",
                "..###..##..###..", "......####...ss.", ".....######.ssss", ".....##..##.ssss",
                ".....##..##..ss.", "....###..###....", "....##....##....", "................"};
            char ch = fig[ay][ax];
            if (ch == '#') return 0x1000000u | (thief.mugger ? 0x902020 : 0x403020);
            if (ch == 'w') return 0x1000000u | 0xFFFFFF;
            if (ch == 's') return 0x1000000u | (thief.state == 2 ? 0xE0C040 : 0x806040);
        }
    }
    {                                                     /* IT: a purple orb marked "IT" */
        static const char *glyph[5] = {"###.###", ".#...#.", ".#...#.", ".#...#.", "###..#."};
        int i;
        for (i = 0; i < n_it; i++) {
            int ax = ((wx - it_obj[i].x) & 0x1FF), ay = ((wy - it_obj[i].y) & 0x1FF), ex, ey, r;
            if (ax >= 16 || ay >= 16) continue;
            pic_at(SH_IT, ax, ay);
            ex = ax * 2 - 15; ey = ay * 2 - 15; r = ex * ex + ey * ey;
            if (r > 225) continue;
            if (ax >= 4 && ax < 11 && ay >= 5 && ay < 10 && glyph[ay - 5][ax - 4] == '#') return 0x1000000u | 0xFFFFFF;
            return 0x1000000u | (r < 100 ? 0xB040E0 : 0x601880);
        }
    }
    {                                                     /* acid puddles: green bubbling blobs */
        int i;
        for (i = 0; i < n_acid; i++) {
            int ax = ((wx - acid[i].x) & 0x1FF), ay = ((wy - acid[i].y) & 0x1FF), ex, ey, r, pk;
            if (acid[i].stun > 0 && ((frame >> 3) & 1)) continue;   /* stunned by magic: it blinks */
            pk = g2pic_find(50 + (int)(((frame >> 3) + (unsigned)i * 3) % 10));   /* Gauntlet II's own frames */
            if (pk >= 0) {                                /* 24x24, centred on its 16x16 square */
                int px2 = (ax + 4) & 0x1FF, py2 = (ay + 4) & 0x1FF; const uint8_t *q;
                if (px2 >= 24 || py2 >= 24) continue;
                q = g2pic[pk].px + (py2 * 24 + px2) * 4;
                pic_pk(pk, px2, py2);
                if (q[3]) return 0x1000000u | (uint32_t)(q[0] << 16 | q[1] << 8 | q[2]);
                continue;
            }
            if (ax >= 16 || ay >= 16) continue;
            ex = ax * 2 - 15; ey = (ay * 2 - 15) * 3 / 2; r = ex * ex + ey * ey;
            if (r > 196) continue;
            if (((ax * 5 + ay * 3 + (int)(frame >> 3)) % 11) == 0) return 0x1000000u | 0xC8FF90;   /* bubbles */
            return 0x1000000u | (r < 80 ? 0x40D020 : 0x208010);
        }
    }
    if (g2pic_find(60) >= 0) {                            /* locked chests: Gauntlet II's picture, 24x24 over
                                                             its door cell (spilling onto the neighbours) */
        int dx, dy, pk = g2pic_find(60);
        for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++) {
            int ccx = ((wx >> 4) + dx) & 31, ccy = ((wy >> 4) + dy) & 31, cc = ccy * 32 + ccx, lx, ly;
            const uint8_t *q;
            if (g2_cell[cc] != 22 || rd8(0xFFE246 + (uint32_t)cc) != 0x07) continue;
            lx = ((wx - ccx * 16 + 4) & 0x1FF); ly = ((wy - ccy * 16 + 4) & 0x1FF);
            if (lx >= 24 || ly >= 24) continue;
            q = g2pic[pk].px + (ly * 24 + lx) * 4;
            pic_pk(pk, lx, ly);
            if (q[3]) return 0x1000000u | (uint32_t)(q[0] << 16 | q[1] << 8 | q[2]);
        }
        if (f == 22) return 0x1000000u | 0x24180E;        /* the door under it: shadowed floor */
    } else if (f == 22 && rd8(0xFFE246 + (uint32_t)(((wy >> 4) & 31) * 32 + ((wx >> 4) & 31))) == 0x07) {   /* locked chest */
        if (x == 0 || x == 15 || y == 0 || y == 15) return 0x1000000u | 0x201008;
        if (y <= 5) return 0x1000000u | (y == 5 ? 0xD0A030 : 0x9A5A20);     /* lid */
        if (x >= 6 && x <= 9 && y >= 6 && y <= 10) return 0x1000000u | ((x == 7 || x == 8) && y == 8 ? 0x202020 : 0xF0D040);
        return 0x1000000u | ((y & 3) == 0 ? 0x6A3A14 : 0x8A4C1C);              /* planks */
    }
    if (f == 18 && getenv("G4EXIT6RAW")) return 0;       /* test aid: the bare tile */
    if (f == 18) pic_at(SH_EXIT6, x, y);                  /* (a pack may draw the whole exit-to-6 square) */
    if (f == 18) {                                        /* exit to 6: Gauntlet 4's "EXIT TO 8" tile (code 06,
                                                             black and white; its 8 at x 11-14, y 8-14) made a 6 */
        if (x == 14 && (y == 9 || y == 10)) return 0x1000000u | 0x000000;   /* no upper right stroke */
        if (x == 11 && y == 11) return 0x1000000u | 0xFCFCFC;                /* the 6's middle bar */
        return 0;
    }
    if (f >= 5 && f <= 9) {                                /* amulet: a jewel in its colour */
        static const uint32_t col[5] = {0x40D040, 0x40A0FF, 0xC060FF, 0xFF5030, 0xFFD020};
        int dx = x * 2 - 15, dy = y * 2 - 15, r = dx * dx + dy * dy;
        pic_at(SH_AMULET + f - 5, x, y);
        if (r <= 100) return 0x1000000u | (r <= 30 ? 0xFFFFFF : col[f - 5]);
        if (r <= 144) return 0x1000000u | 0x202020;
        return 0;
    }
    if (f == 26 && g2pic_find(61) >= 0) {                 /* poison: Gauntlet II's skull flask (centre of 24x24) */
        const uint8_t *q = g2pic[g2pic_find(61)].px + ((y + 4) * 24 + x + 4) * 4;
        pic_pk(g2pic_find(61), x + 4, y + 4);
        if (q[3]) return 0x1000000u | (uint32_t)(q[0] << 16 | q[1] << 8 | q[2]);
        return 0x1000000u | 0x24180E;
    }
    if (f == 2 && g2pic_find(70) >= 0) {                  /* force-field post: Gauntlet II's own, glowing on
                                                             the sides its beams leave from (0x5BA70[mask]) */
        int cx = (wx >> 4) & 31, cy = (wy >> 4) & 31, mask = 0, pk; const uint8_t *q;
        if (g2_cell[((cy - 1) & 31) * 32 + cx] == 4) mask |= 1;   /* up */
        if (g2_cell[cy * 32 + ((cx + 1) & 31)] == 3) mask |= 2;   /* right */
        if (g2_cell[((cy + 1) & 31) * 32 + cx] == 4) mask |= 4;   /* down */
        if (g2_cell[cy * 32 + ((cx - 1) & 31)] == 3) mask |= 8;   /* left */
        pk = g2pic_find(70 + mask);
        if (pk >= 0) { q = g2pic[pk].px + (y * 16 + x) * 4; pic_pk(pk, x, y); return 0x1000000u | (uint32_t)(q[0] << 16 | q[1] << 8 | q[2]); }
    }
    if (f == 3 || f == 4) {                               /* beam square: speckles (see beams_on) */
        uint32_t h = (uint32_t)(wx * 73856093u) ^ (uint32_t)(wy * 19349663u) ^ (uint32_t)((frame >> 1) * 83492791u);
        h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
        pic_at(SH_BEAM + (beams_on(frame) ? (beams_red(frame) ? 2 : 0) : 4) + (f == 4), x, y);
        if (beams_on(frame)) {
            if ((h & 7) < 4) return 0x1000000u | (beams_red(frame) ? ((h & 8) ? 0xFF3020u : 0xC01010u) : ((h & 8) ? 0x40FF40u : 0x10B010u));
            return 0x1000000u | 0x0A0A0A;
        }
        return 0x1000000u | ((h & 31) == 0 ? 0x505050u : 0x0A0A0Au);
    }
    return 0;
}

/* 0x52BE player damage (A0 = player record; the frame's damage piled up in +$C0): an
   invulnerable player takes none */
static void (*orig_damage)(M68K *);
static void player_damage(M68K *c)
{
    uint32_t pl = c->a[0];
    if (level_set_g2 && pl >= 0xFF0040 && pl < 0xFF0040 + 4 * 0x2C4) {
        int p = (int)((pl - 0xFF0040) / 0x2C4);
        if (invuln[p] > 0) { wr16(pl + 0xC0, 0); return; }
        if (rd16(pl + 0xC0)) tc_inv[p] = 0;                /* hurt (goal 8) */
    }
    orig_damage(c);
}

int mods_logo_kind(void)                               /* the game the panel logo names */
{
    uint16_t mode = rd16(0xFFE838);
    if (!mods_enabled || (mode != 0 && mode != 3)) return 0;   /* Quest and Battle keep Gauntlet 4 */
    return level_set_g3 && g2_count ? 3 : level_set_g2 && g2_count ? 2 : 1;   /* GAUNTLET III, Gauntlet II, or the arcade Gauntlet */
}
const char *mods_banner(long frame) { return (banner[0] && frame < banner_until) ? banner : NULL; }

/* ------------------------------------------------------------------ dispatcher hook */
/* 0x1792E: "hurry the story text?" (Quest's opening and other scripted text, 0x1767C): Z set while
   any hero holds Start. Holding Start makes some gamepads switch modes, so A or B held hurries the
   text too (C still turns the page, 0x17962). */
static void (*orig_text_hurry)(M68K *);
static void text_hurry(M68K *c)
{
    int p;
    orig_text_hurry(c);
    if (c->z || rd8(0xFFE866)) return;
    for (p = 0; p < 4; p++)
        if (~rd8(0xFF0040 + 0x2C4u * (uint32_t)p + 0x22) & 0x50) { c->z = 1; return; }
}

int mods_override(uint32_t addr, void (**fn)(M68K *))
{

    if (!mods_enabled) return 0;
    {
        unsigned i;
        for (i = 0; i < 6; i++)
            if (addr == pickup_fn[i]) {
                if (*fn != pickup_wrap[i]) { orig_pickup_fn[i] = *fn; *fn = pickup_wrap[i]; }
                return 1;
            }
        for (i = 0; i < N_MONSTER_FN; i++)
            if (addr == monster_fn[i]) {
                if (*fn != monster_wrap[i]) {
                    orig_monster_fn[i] = *fn; *fn = monster_wrap[i];
                    if (addr == 0x00F41C) { orig_thief_setup = orig_monster_fn[i]; orig_monster_fn[i] = thief_setup; }   /* the
                                                                  thief's set-up: the mugger choice too */
                }
                return 1;
            }
    }
    switch (addr) {
    case FN_LEVEL_CHANGE: *fn = level_change; return 1;
    case FN_LEVEL_DRAW:   *fn = level_draw; return 1;
    case 0x0244DE:        *fn = item_list; return 1;
    case 0x000C26:        if (*fn != setup_arcade) { orig_setup_arcade = *fn; *fn = setup_arcade; } return 1;
    case 0x01792E:        if (*fn != text_hurry) { orig_text_hurry = *fn; *fn = text_hurry; } return 1;
    case 0x003D9A:        if (*fn != play_tick) { orig_play_tick = *fn; *fn = play_tick; } return 1;
    case 0x0067BE:        if (*fn != player_tiles) { orig_player_tiles = *fn; *fn = player_tiles; } return 1;
    case 0x006D92:        if (*fn != transporter) { orig_transporter = *fn; *fn = transporter; } return 1;
    case 0x006BFE:        if (*fn != exit_touch) { orig_exit = *fn; *fn = exit_touch; } return 1;
    case 0x008FF0:        if (*fn != shot_objects) { orig_shot_objects = *fn; *fn = shot_objects; } return 1;
    case 0x00671A:        if (*fn != door_touch) { orig_door = *fn; *fn = door_touch; } return 1;
    case 0x0088F6:        if (*fn != shot_bounce) { orig_shot_bounce = *fn; *fn = shot_bounce; } return 1;
    case 0x0088E8:        if (*fn != shot_wall) { orig_shot_wall = *fn; *fn = shot_wall; } return 1;
    case 0x0052BE:        if (*fn != player_damage) { orig_damage = *fn; *fn = player_damage; } return 1;
    case 0x0040D8:        if (*fn != level_ptr) { orig_level_ptr = *fn; *fn = level_ptr; } return 1;
    case 0x019340:        if (*fn != map_remap) { orig_map_remap = *fn; *fn = map_remap; } return 1;
    case 0x01D64A:        if (*fn != hero_setup) { orig_hero_setup = *fn; *fn = hero_setup; } return 1;
    case 0x01F694:        if (*fn != mcmd5) { orig_mcmd[5] = *fn; *fn = mcmd5; } return 1;
    case 0x01F69E:        if (*fn != mcmd0) { orig_mcmd[0] = *fn; *fn = mcmd0; } return 1;
    case 0x01F6A8:        if (*fn != mcmd1) { orig_mcmd[1] = *fn; *fn = mcmd1; } return 1;
    case 0x01F6B2:        if (*fn != mcmd2) { orig_mcmd[2] = *fn; *fn = mcmd2; } return 1;
    case 0x01F6BC:        if (*fn != mcmd3) { orig_mcmd[3] = *fn; *fn = mcmd3; } return 1;
    case 0x01F6C6:        if (*fn != mcmd4) { orig_mcmd[4] = *fn; *fn = mcmd4; } return 1;
    case 0x0FE00C:        if (*fn != music_driver) { orig_music_driver = *fn; *fn = music_driver; } return 1;
    case 0x0FE014:        if (*fn != sfx_a) { orig_sfx_a = *fn; *fn = sfx_a; } return 1;
    case 0x0FE018:        if (*fn != sfx_b) { orig_sfx_b = *fn; *fn = sfx_b; } return 1;
    case 0x01F664:        if (*fn != music_req) { orig_music = *fn; *fn = music_req; } return 1;
    case 0x01F616:        if (*fn != sound_req) { orig_sound_req = *fn; *fn = sound_req; } return 1;
    case 0x023E96:        if (*fn != menu_wrap) { orig_menu = *fn; *fn = menu_wrap; } return 1;
    case 0x0236A4:        if (*fn != mode_select) { orig_mode_select = *fn; *fn = mode_select; } return 1;
    case 0x02369A:        if (*fn != attract_clear) { orig_attract_clear = *fn; *fn = attract_clear; } return 1;
    case 0x0237A6:        if (*fn != arcade_screen) { orig_arcade_screen = *fn; *fn = arcade_screen; } return 1;
    case 0x0179AA:        if (*fn != record_screen) { orig_record_screen = *fn; *fn = record_screen; } return 1;
    case 0x01F416:        if (*fn != hero_up) { orig_hero_up = *fn; *fn = hero_up; } return 1;
    case 0x01F47A:        if (*fn != hero_down) { orig_hero_down = *fn; *fn = hero_down; } return 1;
    case 0x01F6D0:        if (*fn != panel_names) { orig_panel_names = *fn; *fn = panel_names; } return 1;
    case 0x023BD0:        if (*fn != panel_hero) { orig_panel_hero = *fn; *fn = panel_hero; } return 1;
    case 0x00F01C:        if (*fn != thief_contact) { orig_thief_contact = *fn; *fn = thief_contact; } return 1;
    case 0x00E654:        if (*fn != thief_frame) { orig_thief_frame = *fn; *fn = thief_frame; } return 1;
    case 0x00FA68:        if (*fn != thief_drop) { orig_thief_drop = *fn; *fn = thief_drop; } return 1;
    case 0x00F5CC:        if (*fn != thief_trail) { orig_thief_trail = *fn; *fn = thief_trail; } return 1;
    case 0x019B7A:        if (*fn != floor_draw) { orig_floor_draw = *fn; *fn = floor_draw; } return 1;
    case 0x019660:        if (*fn != wall_draw) { orig_wall_draw = *fn; *fn = wall_draw; } return 1;
    case 0x000F06:        if (*fn != next_level) { orig_next_level = *fn; *fn = next_level; } return 1;
    case 0x00288E:        if (*fn != level_music) { orig_level_music = *fn; *fn = level_music; } return 1;
    case 0x017B2C:        if (*fn != record_start) { orig_record_start = *fn; *fn = record_start; } return 1;
    }
    return 0;
}
