/*
 * g4host.c -- host program for the C translation of Gauntlet 4 (Mega Drive).
 *
 * Runs the translated game (c/, c/gen/) natively: the ROM is loaded into the
 * 68000 memory array (for its data), the hardware is emulated here, and every
 * 68000 routine runs as its C translation. Reaching code that has no C version
 * stops the program with its address.
 *
 *   VDP        ports, registers, VRAM/CRAM/VSRAM, DMA (68000->VDP, fill, copy), a
 *              per-scanline renderer: planes A/B (full/cell/line H scroll, full or
 *              2-column V scroll), window, sprites, priorities, H32/H40
 *   Interrupts the game waits for vertical blank in wait_vblank (0x20496); the host
 *              replaces that routine with one frame: 224 lines with the HBlank
 *              interrupt fired by the VDP line counter (the RAM stub at $FF0000 is
 *              done here -- CRAM colour 2 <- the word at $FF001A -- then the
 *              translated continuation 0x1B936), then the VBlank interrupt (0x1A270)
 *   Pads       3-button pads on ports 1 and 2 (TH protocol), or a Team Player with four, scripted presses from the command line
 *   Sound      host/g4audio.c: YM2612 (Nuked-OPN2), PSG, the translated Z80 sample player;
 *              --wav FILE records it; in a window it plays and paces the frames
 *
 *   ghost ROM [--window] [--teamplayer] [--frames N] [--shot F,F,...] [--out DIR] [--press [P:]BUTTON@F-F ...]
 *   buttons: up down left right a b c start; P = player 1-4 (default 1)
 *   online: --host PORT --players N (you are player 1) | --join HOST:PORT; --delay F (host, default 4)
 *   --arcade-colours: start with the arcade game's floor/wall colours (also in the pause menu);
 *   --colours-file F: where arcade_colours.txt is (default: current folder, then the program's)
 *   --level-set gauntlet2: Arcade/Record play the Gauntlet II mazes (host/gauntlet2_levels.bin,
 *            made by g2_convert.py; --g2-levels FILE elsewhere)
 *   --original: none of the extras (host/g4mods.c: Options "Start Level"; pause menu here)
 *   --wav FILE: record the sound; --nosound: window without sound (60 Hz timer);
 *   --nomusic: no music (sound effects and voices only)
 *   --teamplayer: a Sega Team Player on port 1 carries players 1-4 (otherwise pads on ports 1, 2)
 *   --window (build with -DG4_SDL and SDL3: `pkg-config --cflags --libs sdl3`): play in a window, 60 Hz;
 *            keyboard and up to four gamepads, see window_input(); Esc quits; --frames 0 = no limit
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../c/g4_m68k.h"
#include "../c/g4_z80.h"
#include "g4audio.h"
#include "g4extract.h"
#include "g4launch.h"
#include "g4data.h"
#include "g4pack.h"
#include "g2run.h"
#include "g4music.h"
#include "g4net.h"
#include "g4mods.h"

/* FNV-1a, for the online ROM and state checks */
static uint32_t hash_bytes(uint32_t h, const uint8_t *p, size_t n)
{
    while (n--) { h ^= *p++; h *= 16777619u; }
    return h;
}

/* g4_mem: defined in c/g4_bits.c */
static M68K cpu;

/* ------------------------------------------------------------------ untranslated code */
static long frame_no;
void g4_external(M68K *c, uint32_t target)
{
    fprintf(stderr, "frame %ld: call into untranslated code at %06X (return address %06X)\n",
            frame_no, target, rd32(c->a[7]));
    exit(2);
}

/* ------------------------------------------------------------------ VDP */
static struct {
    uint8_t  vram[0x10000];
    uint16_t cram[64], vsram[40];
    uint8_t  reg[24];
    uint16_t addr; uint8_t code; int pending; int fill;
    int line, vb, hb;
} vdp;

static int dma_len(void) { int n = vdp.reg[19] | (vdp.reg[20] << 8); return n ? n : 0x10000; }

static void vdp_store(uint16_t w)
{
    switch (vdp.code & 0x0F) {
    case 1: vdp.vram[vdp.addr & 0xFFFE] = (uint8_t)(w >> 8); vdp.vram[(vdp.addr & 0xFFFE) | 1] = (uint8_t)w;
        if (getenv("G4WTRACE")) {                        /* debug: who writes the panel's row G4WTRACE (window plane) */
            extern uint32_t dbg_last_call_x(void); static int n;
            uint16_t base = (uint16_t)((vdp.reg[3] & 0x3C) << 10), row = (uint16_t)atoi(getenv("G4WTRACE"));
            if (n < 60 && vdp.addr >= base + row * 128 && vdp.addr < base + row * 128 + 128) { n++; fprintf(stderr, "window row %d col %d = %04X after call %06X\n", row, (vdp.addr - base - row * 128) / 2, w, dbg_last_call_x()); }
        }
        break;
    case 3: vdp.cram[(vdp.addr >> 1) & 63] = w & 0x0EEE; break;
    case 5: if (((vdp.addr >> 1) & 63) < 40) vdp.vsram[(vdp.addr >> 1) & 63] = w & 0x07FF;
        { extern uint32_t dbg_last_call_x(void); static int n; static uint16_t last = 0xFFFF; if (getenv("G4VSDEBUG") && n < 400 && ((vdp.addr >> 1) & 63) == 1 && w != last) { n++; last = w; fprintf(stderr, "vsram B = %04X after call %06X\n", w, dbg_last_call_x()); } }
        break;
    }
    vdp.addr = (uint16_t)(vdp.addr + vdp.reg[15]);
}

static void vdp_dma(void)
{
    int kind = vdp.reg[23] >> 6, n = dma_len();
    if (kind <= 1) {                                       /* 68000 memory -> VDP */
        uint32_t src = ((uint32_t)(vdp.reg[23] & 0x7F) << 17) | ((uint32_t)vdp.reg[22] << 9) | ((uint32_t)vdp.reg[21] << 1);
        while (n--) { uint32_t a = src & 0xFFFFFF; vdp_store((uint16_t)((g4_mem[a] << 8) | g4_mem[a + 1])); src += 2; }
        vdp.code &= ~0x20;
    } else if (kind == 2) {
        vdp.fill = 1;                                      /* the next data write starts it */
    } else {                                               /* VRAM copy */
        uint16_t src = (uint16_t)(vdp.reg[21] | (vdp.reg[22] << 8));
        while (n--) { vdp.vram[vdp.addr] = vdp.vram[src++]; vdp.addr = (uint16_t)(vdp.addr + vdp.reg[15]); }
    }
}

static void vdp_ctrl(uint16_t w)
{
    if (vdp.pending) {
        vdp.pending = 0;
        vdp.addr = (uint16_t)((vdp.addr & 0x3FFF) | ((w & 3) << 14));
        vdp.code = (uint8_t)((vdp.code & 0x03) | ((w >> 2) & 0x3C));
        if ((vdp.code & 0x20) && (vdp.reg[1] & 0x10)) vdp_dma();
        return;
    }
    if ((w & 0xC000) == 0x8000) { int r = (w >> 8) & 0x1F; if (r < 24) vdp.reg[r] = (uint8_t)w; return; }
    vdp.code = (uint8_t)((vdp.code & 0x3C) | (w >> 14));
    vdp.addr = (uint16_t)((vdp.addr & 0xC000) | (w & 0x3FFF));
    vdp.pending = 1;
}

static void vdp_data(uint16_t w)
{
    vdp.pending = 0;
    if (vdp.fill) {
        int n = dma_len();
        vdp.fill = 0; vdp_store(w);
        while (n--) { vdp.vram[vdp.addr] = (uint8_t)(w >> 8); vdp.addr = (uint16_t)(vdp.addr + vdp.reg[15]); }
        return;
    }
    vdp_store(w);
}

static uint16_t vdp_read_data(void)
{
    uint16_t v = 0;
    vdp.pending = 0;
    switch (vdp.code & 0x0F) {
    case 0: v = (uint16_t)((vdp.vram[vdp.addr & 0xFFFE] << 8) | vdp.vram[(vdp.addr & 0xFFFE) | 1]); break;
    case 8: v = vdp.cram[(vdp.addr >> 1) & 63]; break;
    case 4: v = vdp.vsram[((vdp.addr >> 1) & 63) % 40]; break;
    }
    vdp.addr = (uint16_t)(vdp.addr + vdp.reg[15]);
    return v;
}

static long status_reads_since_frame;
static void run_frame(void);

static uint16_t vdp_status(void)
{
    /* code that polls the status port itself (not through wait_vblank) sees VBlank and HBlank
       toggle; if it polls a long time without a frame, a frame runs (watchdog) */
    vdp.vb ^= 1; vdp.hb ^= 1;
    if (++status_reads_since_frame > 20000) run_frame();
    return (uint16_t)(0x3400 | (vdp.vb << 3) | (vdp.hb << 2));
}

/* ------------------------------------------------------------------ pads */
/* players 1-4: buttons held this frame, bit set = pressed */
static uint8_t held[4];
static char launch_base[1024];            /* the program's folder (launcher.cfg) */
/* text entry for the in-game lobby (typing the host's address): host_text_* below */
static int text_active, text_done; static char text_buf[256];
enum { P_UP = 1, P_DOWN = 2, P_LEFT = 4, P_RIGHT = 8, P_A = 16, P_B = 32, P_C = 64, P_START = 128 };

/* the two control ports: data latch and direction (ctrl: bit set = output). An input line
   reads high (pull-up); TH (bit 6) and TR (bit 5) are what the console drives */
static struct { uint8_t data, ctrl, th, tr, counter; } port[2];
static int teamplayer;                    /* --teamplayer: a Sega Team Player on port 1, players 1-4 */
static int pad2_present;                  /* is anything plugged into port 2? Only once player 2 exists: a
                                             second gamepad, the keyboard's player-2 keys used, scripted
                                             player-2 presses, or (online) a second player. An empty
                                             port reads all lines high, which the game tells from a pad */

/* plain 3-button pad on port n, showing player n */
static uint8_t pad_read(int n)
{
    uint8_t h = held[n], v;
    if (port[n].th) {   /* TH=1: ? 1 C B R L D U (active low) */
        v = (uint8_t)(0x40 | (~((h & P_UP ? 1 : 0) | (h & P_DOWN ? 2 : 0) | (h & P_LEFT ? 4 : 0) | (h & P_RIGHT ? 8 : 0)
                                | (h & P_B ? 16 : 0) | (h & P_C ? 32 : 0)) & 0x3F));
    } else {         /* TH=0: ? 0 S A 0 0 D U */
        v = (uint8_t)(~((h & P_UP ? 1 : 0) | (h & P_DOWN ? 2 : 0) | (h & P_A ? 16 : 0) | (h & P_START ? 32 : 0)) & 0x33);
    }
    return v;
}

/* Sega Team Player (the game's reader is 0x024850, its reset 0x024806). TH high resets the
   sequence; every TH/TR change while TH is low steps to the next nibble, and TL (bit 4) echoes
   TR to acknowledge. Nibbles: 3 (TH high: with TH low F gives device ID 7), F, 0, 0, then the
   four pad types (0 = 3-button), then per pad: R L D U, then Start A C B (all active low) */
static uint8_t teamplayer_read(int n)
{
    unsigned k = port[n].counter, nib;
    if (k == 0) nib = 0x3;
    else if (k == 1) nib = 0xF;
    else if (k < 4) nib = 0x0;
    else if (k < 8) nib = 0x0;                                   /* sockets A-D: 3-button pads */
    else if (k < 16) {
        uint8_t h = held[(k - 8) / 2];
        nib = (k & 1) ? ((h & P_B ? 1 : 0) | (h & P_C ? 2 : 0) | (h & P_A ? 4 : 0) | (h & P_START ? 8 : 0))
                      : (h & 0x0F);
        nib = ~nib & 0xF;
    } else nib = 0xF;
    return (uint8_t)((port[n].th << 6) | (port[n].tr << 5) | (port[n].tr << 4) | nib);
}

static uint8_t port_read(int n)
{
    uint8_t v = (teamplayer && n == 0) ? teamplayer_read(0)
              : (teamplayer && n == 1) ? 0x7F                    /* nothing on port 2 */
              : (n == 1 && !pad2_present) ? 0x7F                 /* nothing plugged in */
              : pad_read(n);
    return (uint8_t)((v & ~port[n].ctrl) | (port[n].data & port[n].ctrl & 0x7F));   /* outputs read back */
}

static void port_lines(int n)
{
    uint8_t th = (port[n].ctrl & 0x40) ? ((port[n].data >> 6) & 1) : 1;
    uint8_t tr = (port[n].ctrl & 0x20) ? ((port[n].data >> 5) & 1) : 1;
    if (th != port[n].th || tr != port[n].tr) {
        if (th) port[n].counter = 0;
        else if (port[n].counter < 255) port[n].counter++;
    }
    port[n].th = th; port[n].tr = tr;
}

/* ------------------------------------------------------------------ hardware hooks */
static int z80_busreq;
static uint32_t io_read(uint32_t a, int size)
{
    if (a >= 0xC00000 && a < 0xC00020) {
        uint32_t off = a & 0x1F;
        if (off < 4) { uint16_t v = vdp_read_data(); return size == 1 ? (off & 1 ? (v & 0xFF) : (v >> 8)) : v; }
        if (off < 8) { uint16_t v = vdp_status(); return size == 1 ? (off & 1 ? (v & 0xFF) : (v >> 8)) : v; }
        if (off < 0x10) { uint16_t v = (uint16_t)((vdp.line & 0xFF) << 8 | 0x80); return size == 1 ? (off & 1 ? (v & 0xFF) : (v >> 8)) : v; }
        return 0xFF;
    }
    if (a < 0xA02000u) {                                 /* Z80 RAM */
        uint32_t o = a - 0xA00000;
        return size == 1 ? g4z80_ram[o] : (uint32_t)((g4z80_ram[o] << 8) | g4z80_ram[(o + 1) & 0x1FFF]);
    }
    if (a >= 0xA04000 && a < 0xA04004) return audio_ym_read68k(a & 3);
    switch (a & ~1u) {
    case 0xA10000: return size == 1 ? 0xA0 : 0x00A0;     /* version: overseas, NTSC, no expansion */
    case 0xA10002: return port_read(0);
    case 0xA10004: return port_read(1);
    case 0xA10008: return port[0].ctrl;
    case 0xA1000A: return port[1].ctrl;
    case 0xA1000C: return 0;
    case 0xA11100: return size == 1 ? (z80_busreq ? 0 : 1) : (z80_busreq ? 0 : 0x100);  /* bit 0/8 clear = granted */
    }
    return 0;
}

static void io_write(uint32_t a, uint32_t v, int size)
{
    if (a >= 0xC00000 && a < 0xC00020) {
        uint32_t off = a & 0x1F;
        uint16_t w = size == 1 ? (uint16_t)((v & 0xFF) << 8 | (v & 0xFF)) : (uint16_t)v;
        if (off < 4) vdp_data(w);
        else if (off < 8) vdp_ctrl(w);
        else if (off == 0x10 || off == 0x11) audio_psg_write((uint8_t)v);   /* PSG */
        return;
    }
    if (a < 0xA02000u) {                                 /* Z80 RAM */
        uint32_t o = a - 0xA00000;
        if (audio_debug && o >= 0x10A && o <= 0x10B)
            fprintf(stderr, "f%ld 68000 -> Z80 $%04X = %02X (size %d)\n", frame_no, o, v & 0xFF, size);
        if (size == 1) g4z80_ram[o] = (uint8_t)v;
        else { g4z80_ram[o] = (uint8_t)(v >> 8); g4z80_ram[(o + 1) & 0x1FFF] = (uint8_t)v; }
        return;
    }
    switch (a & ~1u) {                                   /* control ports: data, ctrl */
    case 0xA10002: port[0].data = (uint8_t)v; port_lines(0); return;
    case 0xA10004: port[1].data = (uint8_t)v; port_lines(1); return;
    case 0xA10008: port[0].ctrl = (uint8_t)v; port_lines(0); return;
    case 0xA1000A: port[1].ctrl = (uint8_t)v; port_lines(1); return;
    }
    if (a >= 0xA04000 && a < 0xA04004) {                 /* YM2612: a word write puts its high byte on an even port */
        if (size == 1) audio_ym_write(0, a & 3, (uint8_t)v); else audio_ym_write(0, a & 2, (uint8_t)(v >> 8));
        return;
    }
    if ((a & ~1u) == 0xA11100 || (a & ~1u) == 0xA11200) {   /* Z80 bus request / reset (bit 8, or bit 0 of the byte) */
        int on = size == 1 ? ((a & 1) ? 0 : (v & 1)) : ((v >> 8) & 1);
        if (size == 1 && (a & 1)) return;
        if ((a & ~1u) == 0xA11100) { z80_busreq = on; audio_z80_busreq(on); }
        else audio_z80_reset(!on);
        return;
    }
    /* bank register, TMSS: ignored */
}

/* ------------------------------------------------------------------ renderer */
static int screen_w(void) { return (vdp.reg[12] & 0x81) ? 320 : 256; }
static uint32_t fb[224][320];
/* graphics packs (g4pack.c), per pixel, recorded by render_line when a pack is in use or being dumped:
   - the background: the best non-sprite layer's tile (pinfo_has PI_PLANE: name-table entry and the
     pixel's place in it as on screen; flips are applied when the pack picture is read) and its
     colour fb_plane; ovl_px = the Gauntlet II overlay's own pixel on it (bit 24 set), if any;
   - pic_id1 = 1 + the Gauntlet II picture/shape whose box covers the pixel (g4mods mods_pic_*), the
     place in it, its brightness/2;
   - the front sprite's box (opaque or not, where the planes would let it show; scov_*), spr_won =
     the game's pixel came from a sprite: a pack sprite may paint all of its square. */
static uint16_t pinfo_ent[224][320]; static uint8_t pinfo_lx[224][320], pinfo_ly[224][320], pinfo_has[224][320];
static uint32_t fb_plane[224][320], ovl_px[224][320];
static uint8_t pic_id1[224][320], pic_lx[224][320], pic_ly[224][320], pic_sc8[224][320];
static uint16_t scov_ent[224][320]; static uint8_t scov_lxy[224][320], scov_has[224][320], spr_won[224][320];
enum { PI_NONE, PI_PLANE };
extern int mods_pic_id, mods_pic_x, mods_pic_y, mods_pic_scale;
int mods_pic_size(int id, int *w, int *h);
static int tint_box[4 * 16], n_tint;                   /* super sorcerers' screen boxes this frame */
static uint32_t maroon(uint32_t o)                     /* their light maroon tint (user's choice): half the
                                                          colour, half deep red-purple at its brightness */
{
    int r = (o >> 16) & 255, g = (o >> 8) & 255, bl = o & 255, v = r > g ? r : g;
    if (bl > v) v = bl;
    r = (r + v * 80 / 100) / 2; g = (g + v * 8 / 100) / 2; bl = (bl + v * 22 / 100) / 2;
    return (uint32_t)(r << 16 | g << 8 | bl);
}
static int mug_on, mug_x, mug_y; static const uint8_t *mug_px;   /* the mugger's own picture this frame */
static int in_tint(int x, int y)
{
    int b;
    for (b = 0; b < n_tint; b++) if (x >= tint_box[4 * b] && x < tint_box[4 * b + 2] && y >= tint_box[4 * b + 1] && y < tint_box[4 * b + 3]) return 1;
    return 0;
}
static uint32_t fb_base[224][320];                     /* fb as drawn, before menus/messages on top */
static int pack_info_on;                               /* a pack or the tile dump is active */
static uint32_t *fb_hd; static int hd_w, hd_h;         /* the frame at the pack's resolution (pack_frame) */

static uint32_t ovr_rgb[64]; static uint64_t ovr_mask;     /* arcade colours: CRAM entries shown as these */

/* Character select, player colours (display only, so online play is unaffected). All heroes use
   palette line 0; each hero's own colours there are: Warrior red 11, Valkyrie blues 6/7/8, Wizard
   yellow 5 and beige 4, Elf greens 9/10 (skin, white, outline and browns are shared). A sprite
   pixel from a player's tiles (hero 0-35: 9 per player; shots 36-51: 4 per player) whose hero is
   not the player's own has those colours redrawn in the player's colour (red, blue, yellow, green)
   at the same brightness. */
static int hero_recolour(int slot, int ci, uint16_t c, uint32_t *out)
{
    extern int16_t g4_hero_class_tab[4];
    static const uint16_t team[4] = {1u << 11, 1u << 6 | 1u << 7 | 1u << 8, 1u << 5 | 1u << 4, 1u << 9 | 1u << 10};
    static const int hue[4][3] = {{7, 1, 1}, {3, 5, 7}, {7, 7, 3}, {2, 7, 2}};   /* the player's colour, full brightness */
    int cls = g4_hero_class_tab[slot], v, r, g, b;
    if (cls < 0 || (cls & 3) == slot || !((team[cls & 3] >> ci) & 1)) return 0;
    r = (c >> 1) & 7; g = (c >> 5) & 7; b = (c >> 9) & 7;
    v = r > g ? r : g; if (b > v) v = b;
    r = hue[slot][0] * v / 7; g = hue[slot][1] * v / 7; b = hue[slot][2] * v / 7;
    *out = (uint32_t)((r * 36) << 16 | (g * 36) << 8 | (b * 36));
    return 1;
}

static uint32_t rgb(uint16_t c)
{
    unsigned r = (c >> 1) & 7, g = (c >> 5) & 7, b = (c >> 9) & 7;
    return (r * 36) << 16 | (g * 36) << 8 | (b * 36);
}

/* one pixel of a 4bpp tile: entry = nametable word; returns colour index 0-15 */
static int tile_px(uint16_t entry, int x, int y)
{
    int idx = entry & 0x7FF, a;
    if (entry & 0x0800) x = 7 - x;
    if (entry & 0x1000) y = 7 - y;
    a = idx * 32 + y * 4 + (x >> 1);
    return (x & 1) ? (vdp.vram[a] & 15) : (vdp.vram[a] >> 4);
}

static void render_line(int y)
{
    int W = screen_w(), cells = W / 8, x;
    int psw = (vdp.reg[16] & 3) == 3 ? 128 : (vdp.reg[16] & 3) == 1 ? 64 : 32;
    int psh = ((vdp.reg[16] >> 4) & 3) == 3 ? 128 : ((vdp.reg[16] >> 4) & 3) == 1 ? 64 : 32;
    uint32_t ba = (uint32_t)(vdp.reg[2] & 0x38) << 10, bb = (uint32_t)(vdp.reg[4] & 7) << 13;
    uint32_t bw = (uint32_t)(vdp.reg[3] & (W == 320 ? 0x3C : 0x3E)) << 10, hs = (uint32_t)(vdp.reg[13] & 0x3F) << 10;
    uint32_t sat = (uint32_t)(vdp.reg[5] & (W == 320 ? 0x7E : 0x7F)) << 9;
    int hmode = vdp.reg[11] & 3, vcol = (vdp.reg[11] >> 2) & 1;
    uint32_t hsa = hs + (hmode == 0 ? 0 : hmode == 2 ? (uint32_t)(y & ~7) * 4 : (uint32_t)y * 4);
    int hsA = ((vdp.vram[hsa] << 8) | vdp.vram[hsa + 1]) & 0x3FF, hsB = ((vdp.vram[hsa + 2] << 8) | vdp.vram[hsa + 3]) & 0x3FF;
    int win_h = vdp.reg[17] & 0x1F, win_right = vdp.reg[17] & 0x80, win_v = vdp.reg[18] & 0x1F, win_down = vdp.reg[18] & 0x80;
    int in_win_rows = win_down ? (y >= win_v * 8) : (y < win_v * 8);
    static long tint_frame = -1;
    uint8_t spr_c[320]; uint8_t spr_p[320]; uint8_t spr_h[320]; uint16_t spr_ent[320]; uint8_t spr_lxy[320], spr_cov[320];   /* spr_h: 1 + the player whose tiles (character select colours) */
    int s, n;

    if (!(vdp.reg[1] & 0x40)) {                         /* display disabled: the backdrop colour only */
        uint32_t bd = rgb(vdp.cram[vdp.reg[7] & 0x3F]);
        for (x = 0; x < W; x++) fb[y][x] = bd;
        return;
    }
    /* sprites on this line: the first (lowest link order) opaque pixel wins */
    memset(spr_c, 0, sizeof spr_c); memset(spr_p, 0, sizeof spr_p); memset(spr_h, 0, sizeof spr_h); memset(spr_cov, 0, sizeof spr_cov);
    if (tint_frame != frame_no) { tint_frame = frame_no; n_tint = mods_tint_boxes(tint_box, 16); mug_on = mods_mugger_box(&mug_x, &mug_y, &mug_px); }
    for (s = 0, n = 0; n < 80; n++) {
        uint32_t e = sat + (uint32_t)s * 8;
        int sy = ((vdp.vram[e] << 8 | vdp.vram[e + 1]) & 0x3FF) - 128;
        int size = vdp.vram[e + 2], link = vdp.vram[e + 3] & 0x7F;
        uint16_t attr = (uint16_t)(vdp.vram[e + 4] << 8 | vdp.vram[e + 5]);
        int sx = ((vdp.vram[e + 6] << 8 | vdp.vram[e + 7]) & 0x1FF) - 128;
        int sw = ((size >> 2) & 3) + 1, sh = (size & 3) + 1;
        if (y >= sy && y < sy + sh * 8) {
            int ry = y - sy, cx;
            if (attr & 0x1000) ry = sh * 8 - 1 - ry;
            for (cx = 0; cx < sw * 8; cx++) {
                int px = sx + cx, rx = (attr & 0x0800) ? sw * 8 - 1 - cx : cx, ci;
                uint16_t t;
                if (px < 0 || px >= W) continue;
                t = (uint16_t)(((attr & 0x7FF) + (rx >> 3) * sh + (ry >> 3)) & 0x7FF);
                if (!spr_cov[px] && pack_info_on) {     /* graphics packs: the front sprite's box */
                    spr_cov[px] = 1; spr_ent[px] = (uint16_t)(t | (attr & 0xF800)); spr_lxy[px] = (uint8_t)((cx & 7) | ((y - sy) & 7) << 3);
                }
                if (spr_c[px]) continue;
                ci = tile_px(t, rx & 7, ry & 7);
                if (ci) {
                    spr_c[px] = (uint8_t)(((attr >> 13) & 3) * 16 + ci); spr_p[px] = (attr >> 15) & 1;
                    if (!(attr & 0x6000) && t < 52) spr_h[px] = (uint8_t)(1 + (t < 36 ? t / 9 : (t - 36) / 4));
                }
            }
        }
        if (!link) break;
        s = link;
    }

    for (x = 0; x < W; x++) {
        int col = x / 16, layer_c[3] = {0, 0, 0}, layer_p[3] = {0, 0, 0}, k, best = -1, best_l = -1, ci;
        uint32_t ovl = 0; uint16_t pent[2] = {0, 0}; uint8_t plxy[2] = {0, 0}; int pic = -1, pic_x = 0, pic_y = 0, pic_sc = 256;
        int vsA = vdp.vsram[vcol ? (col * 2) % 40 : 0] & 0x3FF, vsB = vdp.vsram[vcol ? (col * 2 + 1) % 40 : 1] & 0x3FF;
        int in_win = in_win_rows || (win_right ? (x >= win_h * 16) : (x < win_h * 16));
        /* plane B */
        {
            int px = (x - hsB) & (psw * 8 - 1), py = (y + vsB) & (psh * 8 - 1);
            uint32_t a = bb + (uint32_t)((py / 8) * psw + px / 8) * 2;
            uint16_t ent = (uint16_t)(vdp.vram[a & 0xFFFF] << 8 | vdp.vram[(a + 1) & 0xFFFF]);
            ci = tile_px(ent, px & 7, py & 7);
            if (ci) { layer_c[0] = ((ent >> 13) & 3) * 16 + ci; layer_p[0] = ent >> 15; }
            pent[0] = ent; plxy[0] = (uint8_t)((px & 7) | (py & 7) << 3);
            ovl = mods_overlay_px(px, py, frame_no);     /* Gauntlet II features on the maze (plane B in play) */
            if (pack_info_on) { pic = mods_pic_id; pic_x = mods_pic_x; pic_y = mods_pic_y; pic_sc = mods_pic_scale; }
        }
        /* plane A, or the window */
        if (in_win && (vdp.reg[17] || vdp.reg[18])) {
            int wcells = W == 320 ? 64 : 32;
            uint32_t a = bw + (uint32_t)((y / 8) * wcells + x / 8) * 2;
            uint16_t ent = (uint16_t)(vdp.vram[a & 0xFFFF] << 8 | vdp.vram[(a + 1) & 0xFFFF]);
            ci = tile_px(ent, x & 7, y & 7);
            if (ci) { layer_c[1] = ((ent >> 13) & 3) * 16 + ci; layer_p[1] = ent >> 15; }
            pent[1] = ent; plxy[1] = (uint8_t)((x & 7) | (y & 7) << 3);
        } else {
            int px = (x - hsA) & (psw * 8 - 1), py = (y + vsA) & (psh * 8 - 1);
            uint32_t a = ba + (uint32_t)((py / 8) * psw + px / 8) * 2;
            uint16_t ent = (uint16_t)(vdp.vram[a & 0xFFFF] << 8 | vdp.vram[(a + 1) & 0xFFFF]);
            ci = tile_px(ent, px & 7, py & 7);
            if (ci) { layer_c[1] = ((ent >> 13) & 3) * 16 + ci; layer_p[1] = ent >> 15; }
            pent[1] = ent; plxy[1] = (uint8_t)((px & 7) | (py & 7) << 3);
        }
        layer_c[2] = spr_c[x]; layer_p[2] = spr_p[x];
        /* low to high: B, A, sprites, then the same with priority set */
        for (k = 0; k < 6; k++) {
            int l = k % 3, pri = k / 3;
            if (layer_c[l] && layer_p[l] == pri) { best = layer_c[l]; best_l = l; }
        }
        if (best < 0) best = vdp.reg[7] & 0x3F;
        fb[y][x] = ((ovr_mask >> best) & 1) ? ovr_rgb[best] : rgb(vdp.cram[best]);
        if (best_l == 2 && spr_h[x]) { uint32_t rc; if (hero_recolour(spr_h[x] - 1, best & 15, vdp.cram[best], &rc)) fb[y][x] = rc; }
        else if (best_l == 2 && n_tint && in_tint(x, y)) fb[y][x] = maroon(fb[y][x]);   /* super sorcerers */
        if (ovl && best_l == 0) fb[y][x] = ovl & 0xFFFFFF;   /* only where the maze shows: under sprites,
                                                                 not over the status panel (plane A) */
        if (mug_on && x >= mug_x && x < mug_x + 24 && y >= mug_y && y < mug_y + 24 && (best_l == 2 || best_l == 0)) {
            const uint8_t *q = mug_px + 4 * ((y - mug_y) * 24 + (x - mug_x));   /* the mugger's own picture */
            if (q[3] >= 128) fb[y][x] = (uint32_t)(q[0] << 16 | q[1] << 8 | q[2]);
            else if (best_l == 2) {                     /* hide Gauntlet 4's thief there: what is under the sprites */
                int ub = -1, l2;
                for (k = 0; k < 6; k++) { l2 = k % 3; if (l2 != 2 && layer_c[l2] && layer_p[l2] == k / 3) ub = layer_c[l2]; }
                if (ub < 0) ub = vdp.reg[7] & 0x3F;
                fb[y][x] = ovl ? ovl & 0xFFFFFF : ((ovr_mask >> ub) & 1) ? ovr_rgb[ub] : rgb(vdp.cram[ub]);
            }
        }
        if (pack_info_on) {                             /* graphics packs (pack_frame) */
            int ub = -1, ul = -1, pri = (spr_ent[x] >> 15) & 1;
            for (k = 0; k < 6; k++) {                   /* the picture without sprites */
                int l = k % 3, pr = k / 3;
                if (l != 2 && layer_c[l] && layer_p[l] == pr) { ub = layer_c[l]; ul = l; }
            }
            if (ub < 0) ub = vdp.reg[7] & 0x3F;
            fb_plane[y][x] = ((ovr_mask >> ub) & 1) ? ovr_rgb[ub] : rgb(vdp.cram[ub]);
            ovl_px[y][x] = (ovl && ul == 0) ? ovl : 0;
            pic_id1[y][x] = 0;
            if (ul == 0 && pic >= 0) { pic_id1[y][x] = (uint8_t)(pic + 1); pic_lx[y][x] = (uint8_t)pic_x; pic_ly[y][x] = (uint8_t)pic_y; pic_sc8[y][x] = (uint8_t)(pic_sc >> 1); }
            pinfo_has[y][x] = ul >= 0 ? PI_PLANE : PI_NONE;
            if (ul >= 0) { pinfo_ent[y][x] = pent[ul]; pinfo_lx[y][x] = plxy[ul] & 7; pinfo_ly[y][x] = plxy[ul] >> 3; }
            spr_won[y][x] = best_l == 2;
            scov_has[y][x] = spr_cov[x] && (pri || !((layer_c[0] && layer_p[0]) || (layer_c[1] && layer_p[1])));
            if (scov_has[y][x]) { scov_ent[y][x] = spr_ent[x]; scov_lxy[y][x] = spr_lxy[x]; }
        }
    }
    (void)cells;
}

/* ------------------------------------------------------------------ interrupts */
void (*g4_lookup(uint32_t addr))(M68K *);

static void interrupt(int level, void (*handler)(M68K *))
{
    uint16_t sr = (uint16_t)((cpu.sr & 0xFF00) | (cpu.x << 4) | (cpu.n << 3) | (cpu.z << 2) | (cpu.v << 1) | cpu.cf);
    if (((cpu.sr >> 8) & 7) >= level) {                  /* masked */
        if (level == 6 && extract_music_active) {        /* --extract-music: the attract loop masks the vblank for
                                                            ~2.4 s while it changes screens; the music sequencer
                                                            (vblank 0x1B7D0: 0xFE004, 0xFE00C) must not stall, or a
                                                            note hangs in the recording -- run just those */
            M68K keep = cpu;
            JSR(&cpu, 0xE00000, 0x0FE004);
            JSR(&cpu, 0xE00000, 0x0FE00C);
            cpu = keep;
        }
        return;
    }
    push32(&cpu, 0xE00000);                              /* return address: not used, popped below */
    cpu.a[7] -= 2; wr16(cpu.a[7], sr);
    cpu.sr = (uint32_t)((sr & 0xF8FF) | 0x2000 | (level << 8));
    handler(&cpu);                                       /* the handler's rte is its C return */
    sr = rd16(cpu.a[7]); cpu.a[7] += 2; (void)pop32(&cpu);
    cpu.sr = sr; cpu.x = (sr >> 4) & 1; cpu.n = (sr >> 3) & 1; cpu.z = (sr >> 2) & 1; cpu.v = (sr >> 1) & 1; cpu.cf = sr & 1;
}

/* The game copies one of two HBlank handlers to $FF0000 (0x1DC30 / 0x1DC60) and patches
   their immediate operands as it runs, so they are done here, reading those operands from RAM.
   A stub is recognised by comparing RAM with its ROM source, skipping the patched bytes. */
static int stub_is(uint32_t rom, int len, int skip_lo, int skip_hi)
{
    int k;
    for (k = 0; k < len; k++)
        if ((k < skip_lo || k > skip_hi) && g4_mem[0xFF0000 + k] != g4_mem[rom + k]) return 0;
    return 1;
}

static void hblank_handler(M68K *c)
{
    if (stub_is(0x01B912, 0x24, 0x1A, 0x1B)) {
        /* levels (from 0x1B912): movem.l d0-d1/a0,-(a7); CRAM address 4 (colour 2); wait for HBlank;
           move.w #imm,(VDP data) with imm the word at $FF001A; jmp $1B936 (restores, rte) */
        movem_push(c, MV_D(0) | MV_D(1) | MV_A(0));
        c->a[0] = 0xC00004;
        vdp_ctrl(0xC004); vdp_ctrl(0x0000);
        c->d[1] = 4;
        vdp_data(rd16(0xFF001A));
        g4_lookup(0x01B936)(c);
        return;
    }
    if (stub_is(0x01B8D6, 0x30, 0x0C, 0x0D)) {
        /* title / menus (from 0x1B8D6): CRAM address $3E (colour 31) <- the word at $FF000C;
           then $FF000C <- word at $FFC246 + $FFEB6C; $FFEB6C += 2; rte */
        uint32_t a1;
        vdp_ctrl(0xC03E); vdp_ctrl(0x0000);
        vdp_data(rd16(0xFF000C));
        a1 = 0xFFC246 + sext16(rd16(0xFFEB6C));
        wr16(0xFF000C, rd16(a1));
        wr16(0xFFEB6C, (uint16_t)(rd16(0xFFEB6C) + 2));
        return;                                          /* registers untouched: a1 was saved and restored */
    }
    {
        int k;
        fprintf(stderr, "frame %ld: HBlank code at $FF0000 is not a known stub:", frame_no);
        for (k = 0; k < 48; k++) fprintf(stderr, " %02X", g4_mem[0xFF0000 + k]);
        fprintf(stderr, "\n"); exit(2);
    }
}

/* ------------------------------------------------------------------ window (SDL3, -DG4_SDL) */
static int use_window;
/* video settings (launcher: --resolution WxH, --display, --aspect, --pack) */
static int video_w = 960, video_h = 672, video_display, video_aspect;   /* display 0 window 1 fullscreen 2 borderless;
                                                          aspect 0 4:3 1 square pixels 2 stretch 3 sharp */
static char video_pack[128];                           /* graphics pack (loading: to come) */
/* sound output: --wav FILE (16-bit stereo at AUDIO_RATE, header patched at exit) */
static FILE *wav; static uint32_t wav_frames;
static void wav_close(void)
{
    uint32_t bytes = wav_frames * 4;
    uint8_t h[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,
                     AUDIO_RATE & 255, (AUDIO_RATE >> 8) & 255, AUDIO_RATE >> 16, 0,
                     (AUDIO_RATE * 4) & 255, ((AUDIO_RATE * 4) >> 8) & 255, (AUDIO_RATE * 4) >> 16, 0,
                     4,0,16,0,'d','a','t','a',0,0,0,0};
    uint32_t riff = bytes + 36; int i;
    for (i = 0; i < 4; i++) { h[4 + i] = (uint8_t)(riff >> (8 * i)); h[40 + i] = (uint8_t)(bytes >> (8 * i)); }
    if (!wav) return;
    fseek(wav, 0, SEEK_SET); fwrite(h, 1, 44, wav); fclose(wav); wav = NULL;
    fprintf(stderr, "wav: %u frames, peak %d\n", wav_frames, (int)audio_peak);
}
static void wav_open(const char *name)
{
    static const uint8_t zero[44];
    wav = fopen(name, "wb");
    if (!wav) { perror(name); exit(1); }
    fwrite(zero, 1, 44, wav);
    atexit(wav_close);
}
static void audio_play(const int16_t *s, int n);

/* test aid: G4YMLOG=file logs every sound-chip write (time in samples, port, register, value) */
static FILE *ymlog;
static void ymlog_write(unsigned port, unsigned addr, unsigned val) { fprintf(ymlog, "e %llu %02X%02X%02X\n", (unsigned long long)audio_clock, port, addr, val); }
static void audio_out(void)
{
    const int16_t *s; int n = audio_take(&s);
    if (n && getenv("G4MUTE")) memset((void *)s, 0, (size_t)n * 4);   /* test: silence, timing unchanged */
    if (wav && n) { fwrite(s, 4, (size_t)n, wav); wav_frames += (uint32_t)n; }
    if (extract_music_active) extract_audio(s, n);
    audio_play(s, n);
}

#ifdef G4_SDL
#include <SDL3/SDL.h>                    /* SDL3 (window, sound, gamepads); SDL2 until 2026-11-21 */
static SDL_Window *win; static SDL_Renderer *ren; static SDL_Texture *tex, *tex_g2;   /* tex_g2: Gauntlet II attract pictures */
static SDL_AudioStream *adev; static int no_sound, dev_rate, latency_frames;
static Uint64 next_frame, perf_hz;

/* Pace and sound. Frames run on a timer at the Mega Drive's own rate (59.92 Hz). The chips'
   sound (AUDIO_RATE) is resampled here to the sound device's rate, nudged by up to 0.5 % to
   keep the queue near its target (devices' clocks never agree exactly with ours). If the
   queue runs dry the target grows (to 250 ms); if far too much is queued, a frame's sound is
   dropped. WSLg's PulseAudio (sent on to Windows) takes samples in ~190 ms gulps at an uneven
   rate: following its apparent rate changed the pitch, so the pitch stays fixed and it gets
   occasional drops instead. */
#define FRAME_HZ 59.9227
static double rs_pos;                    /* resampler position between the last input frame and the next */
static int16_t rs_last[2];

static void audio_play(const int16_t *s, int n)
{
    static int16_t out[2 * 8192];
    static double avg; static int playing; static long last_raise = -999;
    int queued, target, m = 0, i;
    double step, adj;
    if (!adev || !n) return;
    queued = SDL_GetAudioStreamQueued(adev) / 4;
    if (playing && queued == 0 && latency_frames < dev_rate / 4 && frame_no - last_raise > 60) {   /* ran dry: allow more */
        latency_frames += dev_rate / 50; last_raise = frame_no;
        if (audio_debug) fprintf(stderr, "audio: underrun, latency now %d ms\n", latency_frames * 1000 / dev_rate);
    }
    target = latency_frames;
    avg = playing ? avg * 0.97 + queued * 0.03 : queued;                    /* devices take samples in bursts */
    if (playing && queued > target * 3 / 2) return;   /* too much queued: drop this frame's sound (pitch stays right) */
    adj = 0.005 * (target - avg) / target;                                  /* queue short: more samples */
    if (adj > 0.005) adj = 0.005; if (adj < -0.005) adj = -0.005;
    step = (double)AUDIO_RATE / ((double)dev_rate * (1.0 + adj));           /* input frames per output frame */
    /* linear interpolation; input frame -1 is the previous call's last */
    while (m < 8192) {
        int k = (int)rs_pos; double f = rs_pos - k;
        const int16_t *p0 = k == 0 ? rs_last : s + 2 * (k - 1), *p1 = s + 2 * k;
        if (k >= n) break;
        for (i = 0; i < 2; i++) out[2 * m + i] = (int16_t)(p0[i] + (p1[i] - p0[i]) * f);
        m++;
        rs_pos += step;
    }
    rs_pos -= n;
    rs_last[0] = s[2 * n - 2]; rs_last[1] = s[2 * n - 1];
    SDL_PutAudioStreamData(adev, out, m * 4);
    if (audio_debug && frame_no % 60 == 0)
        fprintf(stderr, "audio: device %d Hz, queued %d (avg %.0f, target %d), nudge %+.3f%%\n", dev_rate,
                queued, avg, target, adj * 100);
    if (!playing && queued + m >= target) { SDL_ResumeAudioStreamDevice(adev); playing = 1; }
}

static void window_open(void)
{
    SDL_AudioSpec want;
#ifdef _WIN32   /* window and taskbar icon: resource 1 of the exe (build_win.sh adds ghost.ico when present) */
    SDL_SetHint("SDL_WINDOWS_INTRESOURCE_ICON", "1");
    SDL_SetHint("SDL_WINDOWS_INTRESOURCE_ICON_SMALL", "1");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); exit(1); }
    if (!no_sound && SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_zero(want);
        want.format = SDL_AUDIO_S16; want.channels = 2;
        want.freq = 48000;                               /* we resample to this; SDL converts to the device's own */
        adev = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want, NULL, NULL);   /* starts paused */
        if (!adev) fprintf(stderr, "no sound: %s\n", SDL_GetError());
        else {
            const char *ms = getenv("G4LATENCY");               /* sound queue target, ms (default 60;
                                                                   160 under WSL, whose sound comes in gulps) */
            int wsl = getenv("WSL_DISTRO_NAME") != NULL;
            dev_rate = want.freq;
            latency_frames = dev_rate * (ms ? atoi(ms) : wsl ? 160 : 60) / 1000;
            if (latency_frames < 2048) latency_frames = 2048;
        }
    }
    win = SDL_CreateWindow("Gauntlet 4 (C build)", video_w, video_h, SDL_WINDOW_RESIZABLE | (getenv("G4HIDDEN") ? SDL_WINDOW_HIDDEN : 0));   /* centred
                                                          (G4HIDDEN: test aid, no window on the desktop) */
    if (win && video_display) {                          /* fullscreen: the display mode nearest the resolution;
                                                            borderless: a full-screen window at the desktop's own */
        if (video_display == 1) {
            SDL_DisplayMode m;
            if (SDL_GetClosestFullscreenDisplayMode(SDL_GetDisplayForWindow(win), video_w, video_h, 0.0f, false, &m)) SDL_SetWindowFullscreenMode(win, &m);
        } else SDL_SetWindowFullscreenMode(win, NULL);
        SDL_SetWindowFullscreen(win, true);
    }
    ren = SDL_CreateRenderer(win, NULL);                 /* paced by our timer, not vsync (off by default) */
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 224);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST); /* sharp pixels (SDL3 smooths by default) */
    perf_hz = SDL_GetPerformanceFrequency();
    next_frame = SDL_GetPerformanceCounter();
}

/* input, read once a frame into held[0..3] (players 1-4):
     keyboard player 1: arrows = d-pad, Z/X/C = A/B/C, Enter = Start
     keyboard player 2: W/A/S/D = d-pad, F/G/H = A/B/C, Space = Start
     gamepads (SDL Gamepad, hot-pluggable): players 1-4 in the order connected (a slot
       freed by unplugging is refilled first); d-pad or left stick, West/South/East face buttons
       (X/A/B on an Xbox pad, Square/Cross/Circle on a PlayStation pad) = A/B/C, North also = C,
       Start = Start
   Players 3 and 4 reach the game only through the Team Player (--teamplayer).
   Esc or closing the window quits */
static SDL_Gamepad *ctl[4];

static void controller_open(SDL_JoystickID id)
{
    int slot;
    SDL_Gamepad *g;
    if (!SDL_IsGamepad(id)) return;
    for (slot = 0; slot < 4 && ctl[slot]; slot++) {}
    if (slot == 4) return;
    g = SDL_OpenGamepad(id);
    if (!g) return;
    ctl[slot] = g;
    fprintf(stderr, "gamepad %d: %s\n", slot + 1, SDL_GetGamepadName(g));
}

static void controller_close(SDL_JoystickID id)
{
    int slot;
    for (slot = 0; slot < 4; slot++)
        if (ctl[slot] && SDL_GetGamepadID(ctl[slot]) == id) {
            fprintf(stderr, "gamepad %d disconnected\n", slot + 1);
            SDL_CloseGamepad(ctl[slot]); ctl[slot] = NULL;
        }
}

static uint8_t controller_held(SDL_Gamepad *g)
{
    enum { DEAD = 12000 };
    uint8_t h = 0; int x, y;
    if (!g) return 0;
    x = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX);
    y = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -DEAD) h |= P_UP;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > DEAD) h |= P_DOWN;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -DEAD) h |= P_LEFT;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > DEAD) h |= P_RIGHT;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_WEST)) h |= P_A;     /* Xbox X, PlayStation Square */
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_SOUTH)) h |= P_B;    /* Xbox A, Cross */
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_EAST)) h |= P_C;     /* Xbox B, Circle */
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_NORTH)) h |= P_C;    /* Xbox Y, Triangle */
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_START)) h |= P_START;
    return h;
}

static void window_input(void)
{
    SDL_Event ev; const bool *k; uint8_t h; int i;
    while (SDL_PollEvent(&ev)) {
        if (text_active) {                               /* typing (lobby): keys are letters, not buttons */
            if (ev.type == SDL_EVENT_TEXT_INPUT) { size_t l = strlen(text_buf); if (l + strlen(ev.text.text) < sizeof text_buf - 1) strcat(text_buf, ev.text.text); continue; }
            if (ev.type == SDL_EVENT_KEY_DOWN) {
                if (ev.key.key == SDLK_BACKSPACE && text_buf[0]) text_buf[strlen(text_buf) - 1] = 0;
                else if (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER) text_done = 1;
                else if (ev.key.key == SDLK_ESCAPE) text_done = -1;
                continue;
            }
        }
        if (ev.type == SDL_EVENT_QUIT || (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE)) { SDL_Quit(); exit(0); }
        if (ev.type == SDL_EVENT_GAMEPAD_ADDED) controller_open(ev.gdevice.which);
        if (ev.type == SDL_EVENT_GAMEPAD_REMOVED) controller_close(ev.gdevice.which);
    }
    k = SDL_GetKeyboardState(NULL);
    if (text_active) k = NULL;                          /* (the keyboard is typing) */
    h = 0;
    if (!k) { for (i = 0; i < 4; i++) held[i] |= controller_held(ctl[i]); return; }
    if (k[SDL_SCANCODE_UP]) h |= P_UP;       if (k[SDL_SCANCODE_DOWN]) h |= P_DOWN;
    if (k[SDL_SCANCODE_LEFT]) h |= P_LEFT;   if (k[SDL_SCANCODE_RIGHT]) h |= P_RIGHT;
    if (k[SDL_SCANCODE_Z]) h |= P_A;         if (k[SDL_SCANCODE_X]) h |= P_B;
    if (k[SDL_SCANCODE_C]) h |= P_C;         if (k[SDL_SCANCODE_RETURN]) h |= P_START;
    held[0] |= h;
    h = 0;
    if (k[SDL_SCANCODE_W]) h |= P_UP;        if (k[SDL_SCANCODE_S]) h |= P_DOWN;
    if (k[SDL_SCANCODE_A]) h |= P_LEFT;      if (k[SDL_SCANCODE_D]) h |= P_RIGHT;
    if (k[SDL_SCANCODE_F]) h |= P_A;         if (k[SDL_SCANCODE_G]) h |= P_B;
    if (k[SDL_SCANCODE_H]) h |= P_C;         if (k[SDL_SCANCODE_SPACE]) h |= P_START;
    held[1] |= h;
    for (i = 0; i < 4; i++) held[i] |= controller_held(ctl[i]);
}

/* while waiting for the network: keep the window alive (Esc / close still quit) */
static void net_wait_events(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev))
        if (ev.type == SDL_EVENT_QUIT || (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE)) { SDL_Quit(); exit(0); }
}
static void window_title(void)
{
    char t[64];
    snprintf(t, sizeof t, "Gauntlet 4 (C build) -- online, player %d of %d", net_player + 1, net_players);
    SDL_SetWindowTitle(win, t);
}

static SDL_FRect picture_rect(int pw, int ph)          /* where a pw x ph picture goes in the window */
{
    int ow = video_w, oh = video_h; SDL_FRect r; double want, sx, sy;
    SDL_GetCurrentRenderOutputSize(ren, &ow, &oh);
    if (video_aspect == 2) { r.x = 0; r.y = 0; r.w = (float)ow; r.h = (float)oh; return r; }
    if (video_aspect == 3) {                           /* sharp: the largest whole-number scale */
        int k = ow / pw < oh / ph ? ow / pw : oh / ph; if (k < 1) k = 1;
        r.w = (float)(pw * k); r.h = (float)(ph * k);
    } else {
        want = video_aspect == 0 ? 4.0 / 3.0 : (double)pw / (double)ph;   /* 4:3 as on a TV, or square pixels */
        sx = ow; sy = ow / want;
        if (sy > oh) { sy = oh; sx = oh * want; }
        r.w = (float)sx; r.h = (float)sy;
    }
    r.x = (ow - r.w) / 2; r.y = (oh - r.h) / 2;
    return r;
}
static void window_present(void)
{
    int W = screen_w(), gw, gh; SDL_FRect src = {0, 0, (float)W, 224}; Uint64 now;
    const uint32_t *g2 = mods_g2_attract_frame(&gw, &gh);
    SDL_RenderClear(ren);
    if (g2 && (pack_scale > 0 || pack_dumping)) {       /* graphics packs: still attract screens by fingerprint */
        static uint32_t last_key, crc_tab[256]; static int still; static SDL_Texture *tex_ap; static int aw, ah;
        uint32_t c = 0xFFFFFFFFu; const uint8_t *img; long i;
        if (!crc_tab[1]) { uint32_t k, v; int b; for (k = 0; k < 256; k++) { v = k; for (b = 0; b < 8; b++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1; crc_tab[k] = v; } }
        for (i = 0; i < (long)gw * gh; i++) {
            uint32_t p = g2[i] & 0xFFFFFF;
            c = crc_tab[(c ^ p) & 255] ^ (c >> 8); c = crc_tab[(c ^ (p >> 8)) & 255] ^ (c >> 8); c = crc_tab[(c ^ (p >> 16)) & 255] ^ (c >> 8);
        }
        c = ~c;
        still = c == last_key ? still + 1 : 0; last_key = c;
        if (pack_dumping && still == 30) pack_dump_attract(c, gw, gh, g2);   /* half a second unchanged: a still screen */
        if (pack_scale > 0 && (img = pack_get(PACK_ATTRACT, c, gw, gh))) {
            int sw = gw * pack_scale, sh = gh * pack_scale;
            if (!tex_ap || aw != sw || ah != sh) {
                if (tex_ap) SDL_DestroyTexture(tex_ap);
                tex_ap = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, sw, sh); aw = sw; ah = sh;
                if (tex_ap) SDL_SetTextureScaleMode(tex_ap, SDL_SCALEMODE_NEAREST);
            }
            if (tex_ap) { SDL_FRect d = picture_rect(gw, gh); SDL_UpdateTexture(tex_ap, NULL, img, sw * 4); SDL_RenderTexture(ren, tex_ap, NULL, &d); g2 = NULL; goto presented; }
        }
    }
    if (g2) {                                            /* the arcade picture at its own size, same window area */
        if (!tex_g2) {
            tex_g2 = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, gw, gh);
            if (tex_g2) SDL_SetTextureScaleMode(tex_g2, SDL_SCALEMODE_NEAREST);
        }
        if (tex_g2) { SDL_FRect d = picture_rect(gw, gh); SDL_UpdateTexture(tex_g2, NULL, g2, gw * 4); SDL_RenderTexture(ren, tex_g2, NULL, &d); }
    } else if (pack_scale > 0 && fb_hd) {               /* a graphics pack: its own resolution, same place */
        static SDL_Texture *tex_hd; static int tw, th;
        SDL_FRect d = picture_rect(W, 224);
        if (!tex_hd || tw != hd_w || th != hd_h) {
            if (tex_hd) SDL_DestroyTexture(tex_hd);
            tex_hd = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, hd_w, hd_h); tw = hd_w; th = hd_h;
            if (tex_hd) SDL_SetTextureScaleMode(tex_hd, SDL_SCALEMODE_NEAREST);
        }
        if (tex_hd) { SDL_UpdateTexture(tex_hd, NULL, fb_hd, hd_w * 4); SDL_RenderTexture(ren, tex_hd, NULL, &d); }
    } else {
        SDL_FRect d = picture_rect(W, 224);
        SDL_UpdateTexture(tex, NULL, fb, sizeof fb[0]);
        SDL_RenderTexture(ren, tex, &src, &d);
    }
presented:
    SDL_RenderPresent(ren);
    /* wait for the next frame time: sleep most of it, then spin the last ~2 ms */
    next_frame += (Uint64)((double)perf_hz / FRAME_HZ);
    now = SDL_GetPerformanceCounter();
    if (now > next_frame + perf_hz / 10) next_frame = now;          /* far behind (stall): don't race */
    while ((now = SDL_GetPerformanceCounter()) < next_frame) {
        Uint64 left_ms = (next_frame - now) * 1000 / perf_hz;
        if (left_ms > 2) SDL_Delay((Uint32)(left_ms - 2));
    }
}
#else
static void net_wait_events(void) {}
static void window_title(void) {}
static int no_sound;
static void audio_play(const int16_t *s, int n) { (void)s; (void)n; }
static void window_open(void) { fprintf(stderr, "built without SDL (-DG4_SDL)\n"); exit(1); }
static void window_input(void) {}
static void window_present(void) {}
#endif


/* ------------------------------------------------------------------ arcade colours (extra) */
/* Floors and walls in the arcade game's own colours, at full 24-bit precision. The shades come
   from arcade_colours.txt (made by arcade_colours.py from the arcade ROMs: per Gauntlet 4 Arcade
   record, 3 floor and 3 wall shades, dark to light). Gauntlet 4 puts a level's floor in CRAM
   0x35-0x37 (table ($42D6) by header byte 8/9 high nibbles), its walls in 0x25-0x27 (table
   ($42D2), low nibbles) and the walls one step darker in 0x15-0x17 (0x1DFE6, 0x202B8). An entry
   is shown in the arcade colour only while it holds Gauntlet 4's own colour for the level, so
   fades and other screens are untouched. Only the picture changes: online play is unaffected. */
static struct { uint32_t rec; uint8_t c[18]; } ac[512]; static int n_ac, arcade_colours;

static void arcade_colours_load(const char *path)
{
    FILE *f = fopen(path, "r"); char line[256];
    if (!f) return;
    while (fgets(line, sizeof line, f) && n_ac < 512) {
        unsigned rec, v[18]; int k;
        if (line[0] == '#') continue;
        if (sscanf(line, "%x %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u", &rec,
                   &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8],
                   &v[9], &v[10], &v[11], &v[12], &v[13], &v[14], &v[15], &v[16], &v[17]) != 19) continue;
        ac[n_ac].rec = rec;
        for (k = 0; k < 18; k++) ac[n_ac].c[k] = (uint8_t)v[k];
        n_ac++;
    }
    fclose(f);
    if (n_ac) fprintf(stderr, "arcade colours: %d levels from %s\n", n_ac, path);
}

static uint16_t md_darker(uint16_t c)                 /* 0x202B8 with -1: each channel one step down */
{
    int r = ((c >> 1) & 7) - 1, g = ((c >> 5) & 7) - 1, b = ((c >> 9) & 7) - 1;
    if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
    return (uint16_t)(r << 1 | g << 5 | b << 9);
}
static int md_lum(uint16_t c) { return 2 * ((c >> 1) & 7) + 4 * ((c >> 5) & 7) + ((c >> 9) & 7); }

/* three slots get three shades: the slots' own colours ranked dark to light */
static void ac_assign(const int slot[3], const uint16_t want[3], const uint8_t *shade, int darker)
{
    int order[3] = {0, 1, 2}, i, j;
    for (i = 0; i < 3; i++) for (j = i + 1; j < 3; j++)
        if (md_lum(want[order[j]]) < md_lum(want[order[i]])) { int t = order[i]; order[i] = order[j]; order[j] = t; }
    for (i = 0; i < 3; i++) {
        int s = slot[order[i]], k, c[3];
        if (vdp.cram[s] != want[order[i]]) continue;            /* fading or another screen */
        for (k = 0; k < 3; k++) { c[k] = shade[3 * i + k] - (darker ? 36 : 0); if (c[k] < 0) c[k] = 0; }
        ovr_rgb[s] = (uint32_t)(c[0] << 16 | c[1] << 8 | c[2]);
        ovr_mask |= 1ull << s;
    }
}

/* GAUNTLET III in arcade colours: the Atari 8-bit's own (its record header byte 2 = 0x80 | hue, g2levels.c):
   walls in that hue at brightness 4, 8 and 14 (0x29EB), the floor black (here three near-black greys so its
   pattern still shows). The colour itself from the Atari's NTSC scheme: hue 1-15 round the colour wheel in
   steps of 360/15 degrees, brightness 0-14 (our approximation, not a copied palette). */
static void atari_rgb(int hue, int lum, uint8_t out[3])
{
    double y = 0.08 + 0.92 * lum / 14.0, i = 0, q = 0, r, g, b;
    if (hue) { double a = ((hue - 1) * 24.0 - 58.0) * 3.14159265 / 180.0; i = 0.22 * cos(a); q = 0.22 * sin(a); }
    r = y + 0.956 * i + 0.621 * q; g = y - 0.272 * i - 0.647 * q; b = y - 1.106 * i + 1.703 * q;
    out[0] = (uint8_t)(r < 0 ? 0 : r > 1 ? 255 : r * 255); out[1] = (uint8_t)(g < 0 ? 0 : g > 1 ? 255 : g * 255);
    out[2] = (uint8_t)(b < 0 ? 0 : b > 1 ? 255 : b * 255);
}
static void arcade_colours_frame(void)
{
    uint32_t rec, ft, wt; uint8_t b8, b9; int i, j;
    static const int floor_s[3] = {0x35, 0x36, 0x37}, wall_s[3] = {0x25, 0x26, 0x27}, dark_s[3] = {0x15, 0x16, 0x17};
    uint16_t fw[3], ww[3], dw[3];
    ovr_mask = 0;
    static uint8_t a8[18];
    const uint8_t *shades;
    if (!mods_enabled || !arcade_colours || !n_ac) return;
    rec = rd32(0xFFE75C);
    if (mods_logo_kind() == 3) {                        /* GAUNTLET III: the Atari's colours */
        int hue = rd8(rec + 2) & 15;
        if (!(rd8(rec + 2) & 0x80)) return;
        for (j = 0; j < 3; j++) { a8[3 * j] = a8[3 * j + 1] = a8[3 * j + 2] = (uint8_t)(14 + 12 * j); }
        atari_rgb(hue, 4, a8 + 9); atari_rgb(hue, 8, a8 + 12); atari_rgb(hue, 14, a8 + 15);
        shades = a8;
    } else {
        for (i = 0; i < n_ac && ac[i].rec != rec; i++) {}
        if (i == n_ac) return;
        shades = ac[i].c;
    }
    b8 = rd8(rec + 8); b9 = rd8(rec + 9);
    ft = rd32(0x42D6) + (uint32_t)(b8 >> 4) * 128 + (uint32_t)(b9 >> 4) * 8;
    wt = rd32(0x42D2) + (uint32_t)(b8 & 15) * 128 + (uint32_t)(b9 & 15) * 8;
    for (j = 0; j < 3; j++) {
        fw[j] = (uint16_t)(rd16(ft + 2u * (uint32_t)j) & 0x0EEE);
        ww[j] = (uint16_t)(rd16(wt + 2u * (uint32_t)j) & 0x0EEE);
        dw[j] = md_darker(ww[j]);
    }
    ac_assign(floor_s, fw, shades, 0);
    ac_assign(wall_s, ww, shades + 9, 0);
    ac_assign(dark_s, dw, shades + 9, 1);
}

/* ------------------------------------------------------------------ pause menu (extra) */
/* While the game is paused (pause flag $FFE81E; the pause loop is 0x1FB80), a menu under
   the PAUSE box: RESUME / EXIT TO MENU. The menu takes the buttons; the game sees none,
   except what a choice sends it:
     RESUME        Start for one frame -- the game's own unpause
     EXIT TO MENU  A+B+C, then A+B+C+Start -- the game's own quit (0x1FC38: Start with A, B
                   and C held ends the game, play_level 0xA1E, back to the title)
   It runs on the combined buttons of every player after the online exchange, so every copy
   steps it the same way. Quest mode's pause opens its map screen instead: not there. */
static int pm_active, pm_cursor, pm_step, pm_choice; static uint8_t pm_prev;
int in_pause_loop;                        /* 0x1FB80 is running (the flag alone is also set
                                            elsewhere, e.g. by Start in Record's name entry) */
static void (*orig_pause_loop)(M68K *);
static void pause_loop(M68K *c) { in_pause_loop++; orig_pause_loop(c); in_pause_loop--; }

static int pause_menu_applies(void)
{
    uint16_t mode = rd16(0xFFE838);
    if (!mods_enabled || !in_pause_loop || !rd8(0xFFE81E)) return 0;
    if (mode == 1 || (mode == 2 && rd16(0xFFE966) == 2)) return 0;   /* these show a map */
    return 1;
}

/* RESUME, [COLOURS when arcade_colours.txt was found,] MUSIC, EFFECTS, VOICE (volumes 0-10, left /
   right; saved to launcher.cfg), EXIT TO MENU */
enum { PM_RESUME, PM_COLOURS, PM_MUSIC, PM_SFX, PM_VOICE, PM_EXIT };
static int pm_list[6];
static int pm_items(void)
{
    int n = 0;
    pm_list[n++] = PM_RESUME;
    if (n_ac) pm_list[n++] = PM_COLOURS;
    pm_list[n++] = PM_MUSIC; pm_list[n++] = PM_SFX; pm_list[n++] = PM_VOICE;
    pm_list[n++] = PM_EXIT;
    return n;
}
static void pm_volume(int kind, int d)
{
    int *v = kind == PM_MUSIC ? &audio_vol_music : kind == PM_SFX ? &audio_vol_sfx : &audio_vol_voice;
    *v += d; if (*v < 0) *v = 0; if (*v > 10) *v = 10;
    launch_save_volumes(launch_base, audio_vol_music, audio_vol_sfx, audio_vol_voice);
}

static uint8_t pm_inject;                 /* menu press for the heroes' button bytes (P_ bits) */
static void pause_menu_input(void)
{
    uint8_t all = (uint8_t)(held[0] | held[1] | held[2] | held[3]), pressed, send = 0;
    int i;
    if (!pause_menu_applies()) { pm_active = 0; return; }
    if (!pm_active) { pm_active = 1; pm_cursor = 0; pm_step = 0; pm_prev = all; }
    pressed = (uint8_t)(all & ~pm_prev);
    pm_prev = all;
    if (pm_step == 0) {
        int n = pm_items();
        if (pressed & P_UP) pm_cursor = (pm_cursor + n - 1) % n;
        if (pressed & P_DOWN) pm_cursor = (pm_cursor + 1) % n;
        {   int kind = pm_list[pm_cursor];
            if (kind >= PM_MUSIC && kind <= PM_VOICE) {
                if (pressed & P_LEFT) pm_volume(kind, -1);
                if (pressed & P_RIGHT) pm_volume(kind, +1);
            }
            else if (pressed & (P_START | P_A | P_C)) {
                if (kind == PM_COLOURS) arcade_colours ^= 1;                           /* colours: stay */
                else { pm_choice = kind == PM_EXIT ? 1 : 0; pm_step = 1; }
            }
        }
    }
    if (pm_step > 0) {
        if (pm_choice == 0) { send = P_START; pm_step = 0; pm_active = 0; }         /* resume */
        else if (pm_step == 1) { send = P_A | P_B | P_C; pm_step = 2; }             /* exit */
        else { send = P_A | P_B | P_C | P_START; pm_step = 0; pm_active = 0; }
    }
    for (i = 0; i < 4; i++) held[i] = 0;                 /* the controllers stay quiet while the menu is up */
    pm_inject = send;                                    /* ... and the menu's press goes straight to the
                                                            heroes (after the VBlank, see host_wait_vblank) */
}

/* text in the game's font (tile table 0x321CE, 0x323CE in play: ROM words per character;
   letters in colour 2 on 1), drawn in fixed colours: the level palettes vary */
static void pm_text(int x, int y, const char *s, uint32_t colour)
{
    uint32_t table = rd8(0xFFE798) ? 0x323CE : 0x321CE;
    for (; *s; s++, x += 8) {
        uint16_t tile = (uint16_t)(rd16(table + 2u * (uint8_t)*s) & 0x7FF);
        int r, k, bg = vdp.vram[tile * 32u + 31] & 15;  /* the font's background colour (1): its last pixel */
        for (r = 0; r < 8; r++)
            for (k = 0; k < 8; k++) {
                uint32_t a = tile * 32u + (uint32_t)r * 4u + (uint32_t)k / 2u;
                int px = (k & 1) ? (vdp.vram[a] & 15) : (vdp.vram[a] >> 4);
                if (px != bg && y + r < 224 && x + k < 320) fb[y + r][x + k] = colour;
            }
    }
}

static void logo_edit(int kind);
static void pause_menu_draw(void)
{
    char item[6][32]; int n = pm_items(), h = 6 + 12 * n;
    int x, y, i, cx = 108, top = 112 - h / 2 + 30;
    if (!pm_active || pm_step) return;
    for (i = 0; i < n; i++) {
        int k = pm_list[i], v = k == PM_MUSIC ? audio_vol_music : k == PM_SFX ? audio_vol_sfx : audio_vol_voice;
        const char *lab = k == PM_MUSIC ? "MUSIC" : k == PM_SFX ? "EFFECTS" : "VOICE";
        if (k == PM_RESUME) snprintf(item[i], sizeof item[i], "RESUME");
        else if (k == PM_EXIT) snprintf(item[i], sizeof item[i], "EXIT TO MENU");
        else if (k == PM_COLOURS) snprintf(item[i], sizeof item[i], "%s", arcade_colours ? "COLOURS: ARCADE" : "COLOURS: MEGA DRIVE");
        else snprintf(item[i], sizeof item[i], i == pm_cursor ? "%-7s < %2d >" : "%-7s   %2d  ", lab, v);
    }
    for (y = top; y < top + h; y++)
        for (x = cx - 84; x < cx + 84; x++) fb[y][x] = (y == top || y == top + h - 1 || x == cx - 84 || x == cx + 83) ? 0xFFFFFF : 0;
    for (i = 0; i < n; i++) {
        int w = (int)strlen(item[i]) * 8;
        pm_text(cx - w / 2, top + 6 + 12 * i, item[i], i == pm_cursor ? 0xFFE040 : 0xB0B0B0);
        if (i == pm_cursor) pm_text(cx - w / 2 - 12, top + 6 + 12 * i, ">", 0xFFE040);
    }
}

/* ------------------------------------------------------------------ frame */
static int shots[64], nshots; static const char *outdir = ".";
static struct { int button, from, to, player; } presses[32]; static int npresses;
static long max_frames = 600;

/* Graphics packs: the frame at the pack's resolution. A pixel from a background tile that the pack
   replaces (and that no menu or message was drawn over: fb == fb_base) takes the pack picture's
   pixel; everything else is the game's pixel, enlarged. With --dump-tiles each tile seen is saved. */
static uint32_t slot_key[2048 * 4]; static long slot_key_frame[2048 * 4];
static uint32_t tile_key(uint16_t ent)               /* a tile's fingerprint, at most once a frame */
{
    int slot = ent & 0x7FF, pal = (ent >> 13) & 3, k = slot * 4 + pal;
    if (slot_key_frame[k] != frame_no + 1) { slot_key[k] = pack_key(&vdp.vram[slot * 32], pal); slot_key_frame[k] = frame_no + 1; }
    return slot_key[k];
}
/* Colours of replaced pictures follow the game's palette: per CRAM entry a "normal" colour (ref) is
   kept; when the game dims, brightens or pulses an entry, a pack pixel drawn over that entry changes
   by the same amount (dimming: x cur/ref; brightening: towards white). A brighter colour or a big jump
   (a new palette, not a fade step) becomes the new normal at once, a dimmer one after 20 steady
   frames. Entries 64-67 = each palette line on average (for pack pixels where the original was
   see-through). */
static uint32_t pal_ref[68], pal_cur[68]; static uint8_t pal_steady[64], pal_moved[68]; static int pal_init;
static int lum(uint32_t c) { return (int)((c >> 16) & 255) * 3 + (int)((c >> 8) & 255) * 6 + (int)(c & 255); }
static void pal_track(void)
{
    int i, l, ch;
    for (i = 0; i < 64; i++) {
        uint32_t cur = ((ovr_mask >> i) & 1) ? ovr_rgb[i] : rgb(vdp.cram[i]);
        if (!pal_init) { pal_ref[i] = cur; pal_steady[i] = 0; }
        else if (cur == pal_cur[i]) { if (pal_steady[i] < 255) pal_steady[i]++; if (pal_steady[i] >= 20) pal_ref[i] = cur; }
        else {
            int big = 0;
            for (ch = 0; ch < 24; ch += 8) { int d = (int)((cur >> ch) & 255) - (int)((pal_cur[i] >> ch) & 255); if (d > 40 || d < -40) big = 1; }
            pal_steady[i] = 0;
            if (big || lum(cur) >= lum(pal_ref[i])) pal_ref[i] = cur;
        }
        pal_cur[i] = cur; pal_moved[i] = cur != pal_ref[i];
    }
    for (l = 0; l < 4; l++) {
        unsigned sr[3] = {0, 0, 0}, sc[3] = {0, 0, 0};
        for (i = 1; i < 16; i++) for (ch = 0; ch < 3; ch++) { sr[ch] += (pal_ref[l * 16 + i] >> (16 - 8 * ch)) & 255; sc[ch] += (pal_cur[l * 16 + i] >> (16 - 8 * ch)) & 255; }
        pal_ref[64 + l] = (sr[0] / 15) << 16 | (sr[1] / 15) << 8 | sr[2] / 15;
        pal_cur[64 + l] = (sc[0] / 15) << 16 | (sc[1] / 15) << 8 | sc[2] / 15;
        pal_moved[64 + l] = pal_ref[64 + l] != pal_cur[64 + l];
    }
    pal_init = 1;
}
static uint32_t pal_follow(uint32_t p, int idx)
{
    uint32_t out = 0; int ch;
    if (!pal_moved[idx]) return p;
    for (ch = 16; ch >= 0; ch -= 8) {
        int v = (p >> ch) & 255, r = (pal_ref[idx] >> ch) & 255, c = (pal_cur[idx] >> ch) & 255;
        if (c <= r) v = r ? v * c / r : v;
        else v = v + (255 - v) * (c - r) / (255 - r);
        out |= (uint32_t)v << ch;
    }
    return out;
}
static int pix_entry(uint16_t ent, int lx, int ly)     /* the CRAM entry the original pixel used */
{
    int ci = tile_px(ent, lx, ly);
    return ci ? ((ent >> 13) & 3) * 16 + ci : 64 + ((ent >> 13) & 3);
}
/* a replaced hero drawn in its player's colour (as hero_recolour does for the game's own): pack pixels
   close in hue to the hero's own colour take the player's hue at the same brightness */
static int hero_pack_recolour(int slot, uint32_t p, uint32_t *out)
{
    extern int16_t g4_hero_class_tab[4];
    static const int main_ci[4] = {11, 7, 5, 9};            /* Warrior red, Valkyrie blue, Wizard yellow, Elf green */
    static const int hue[4][3] = {{7, 1, 1}, {3, 5, 7}, {7, 7, 3}, {2, 7, 2}};
    int cls = g4_hero_class_tab[slot], i, h[2], v, mn, d;
    uint32_t c[2];
    if (cls < 0 || (cls & 3) == slot) return 0;
    c[0] = p; c[1] = rgb(vdp.cram[main_ci[cls & 3]]);
    for (i = 0; i < 2; i++) {                                /* hue in degrees; -1 = too grey */
        int r = (c[i] >> 16) & 255, g = (c[i] >> 8) & 255, b = c[i] & 255, mx = r > g ? r : g, hh;
        if (b > mx) mx = b;
        mn = r < g ? r : g; if (b < mn) mn = b;
        if (mx == 0 || (mx - mn) * 4 < mx) { h[i] = -1; continue; }
        if (mx == r) hh = 60 * (g - b) / (mx - mn); else if (mx == g) hh = 120 + 60 * (b - r) / (mx - mn); else hh = 240 + 60 * (r - g) / (mx - mn);
        h[i] = (hh + 360) % 360;
    }
    if (h[0] < 0 || h[1] < 0) return 0;
    d = h[0] - h[1]; if (d < 0) d = -d; if (d > 180) d = 360 - d;
    if (d > 40) return 0;
    v = (p >> 16) & 255; if (((p >> 8) & 255) > (uint32_t)v) v = (p >> 8) & 255; if ((p & 255) > (uint32_t)v) v = p & 255;
    *out = (uint32_t)((hue[slot][0] * v / 7) << 16 | (hue[slot][1] * v / 7) << 8 | (hue[slot][2] * v / 7));
    return 1;
}
static void pack_frame(void)
{
    int W = screen_w(), s = pack_scale > 0 ? pack_scale : 1, x, y, sx, sy, i;
    if (pack_dumping) {
        static uint8_t shp[16][32 * 32 * 4]; static uint8_t shp_dirty[16];   /* shapes 100-115, built from the screen */
        for (y = 0; y < 224; y++) for (x = 0; x < W; x++) {
            int kind = -1, id = pic_id1[y][x] - 1; uint16_t ent = 0; uint32_t prgb[16];
            if (spr_won[y][x] && scov_has[y][x]) { kind = PACK_SPRITE; ent = scov_ent[y][x]; }
            else if (!spr_won[y][x] && pinfo_has[y][x] == PI_PLANE && !ovl_px[y][x]) { kind = PACK_TILE; ent = pinfo_ent[y][x]; }
            if (id >= 100 && id < 116 && !spr_won[y][x] && (ovl_px[y][x] || id == 115)) {
                int w, h; uint8_t *q;
                if (mods_pic_size(id, &w, &h) && pic_lx[y][x] < w && pic_ly[y][x] < h) {
                    uint32_t c = ovl_px[y][x] ? ovl_px[y][x] & 0xFFFFFF : fb_plane[y][x];
                    q = &shp[id - 100][4 * (pic_ly[y][x] * w + pic_lx[y][x])];
                    if (!q[3]) { q[0] = (uint8_t)(c >> 16); q[1] = (uint8_t)(c >> 8); q[2] = (uint8_t)c; q[3] = 255; shp_dirty[id - 100] = 1; }
                }
            }
            if (kind < 0) continue;
            for (i = 0; i < 16; i++) { int ix = ((ent >> 13) & 3) * 16 + i; prgb[i] = ((ovr_mask >> ix) & 1) ? ovr_rgb[ix] : rgb(vdp.cram[ix]); }
            pack_dump_kind(kind, tile_key(ent), &vdp.vram[(ent & 0x7FF) * 32], prgb);
        }
        for (i = 0; i < 16; i++) if (shp_dirty[i]) { int w, h; shp_dirty[i] = 0; if (mods_pic_size(100 + i, &w, &h)) pack_dump_shape(100 + i, w, h, shp[i]); }
    }
    if (pack_scale <= 0) return;
    pal_track();
    if (!fb_hd || hd_w != W * s) { free(fb_hd); hd_w = W * s; hd_h = 224 * s; fb_hd = (uint32_t *)malloc(sizeof *fb_hd * (size_t)(hd_w * hd_h)); }
    for (y = 0; y < 224; y++) for (x = 0; x < W; x++) {
        const uint8_t *timg = NULL, *pimg = NULL, *simg = NULL; uint16_t tent = 0, sent = 0;
        int pw = 8, ph = 8, psc = 256, tidx = 0, sidx = 0, hslot = -1, tint = 0, won = spr_won[y][x];
        uint32_t game = fb[y][x];
        if (game == fb_base[y][x]) {                  /* nothing (menu, message) drawn on top of it */
            if (pinfo_has[y][x] == PI_PLANE) {
                tent = pinfo_ent[y][x]; timg = pack_get(PACK_TILE, tile_key(tent), 8, 8);
                if (timg) tidx = pix_entry(tent, pinfo_lx[y][x], pinfo_ly[y][x]);
            }
            if (pic_id1[y][x] && mods_pic_size(pic_id1[y][x] - 1, &pw, &ph)) { pimg = pack_pic_anim(pic_id1[y][x] - 1u, pw, ph, frame_no); psc = pic_sc8[y][x] * 2; }
            if (scov_has[y][x]) {
                sent = scov_ent[y][x]; simg = pack_get(PACK_SPRITE, tile_key(sent), 8, 8);
                if (simg) {
                    int t = sent & 0x7FF;
                    sidx = pix_entry(sent, scov_lxy[y][x] & 7, scov_lxy[y][x] >> 3);
                    if (!(sent & 0x6000) && t < 52) hslot = t < 36 ? t / 9 : (t - 36) / 4;
                    else tint = n_tint && in_tint(x, y);
                }
            }
        }
        for (sy = 0; sy < s; sy++) for (sx = 0; sx < s; sx++) {
            uint32_t c, bg; const uint8_t *p;
            if (game != fb_base[y][x]) { fb_hd[(y * s + sy) * hd_w + x * s + sx] = game; continue; }
            bg = fb_plane[y][x];
            if (timg) {                                /* the background tile */
                int ix = pinfo_lx[y][x] * s + sx, iy = pinfo_ly[y][x] * s + sy;
                if (tent & 0x0800) ix = 8 * s - 1 - ix;
                if (tent & 0x1000) iy = 8 * s - 1 - iy;
                p = timg + 4 * (iy * 8 * s + ix);
                if (p[3] >= 128) bg = pal_follow((uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2], tidx);
            }
            c = bg;
            if (pimg) {                                /* a Gauntlet II picture or shape: all of its box */
                p = pimg + 4 * ((pic_ly[y][x] * s + sy) * pw * s + pic_lx[y][x] * s + sx);
                if (p[3] >= 128) c = psc >= 255 ? ((uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2])
                                                : ((uint32_t)(p[0] * psc >> 8) << 16 | (uint32_t)(p[1] * psc >> 8) << 8 | (uint32_t)(p[2] * psc >> 8));
            } else if (ovl_px[y][x]) c = ovl_px[y][x] & 0xFFFFFF;   /* the port's own drawing */
            if (simg) {                                /* a replaced sprite: all of its square */
                int ix = (scov_lxy[y][x] & 7) * s + sx, iy = (scov_lxy[y][x] >> 3) * s + sy;
                if (sent & 0x0800) ix = 8 * s - 1 - ix;
                if (sent & 0x1000) iy = 8 * s - 1 - iy;
                p = simg + 4 * (iy * 8 * s + ix);
                if (p[3] >= 128) {
                    uint32_t rc;
                    c = pal_follow((uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2], sidx);
                    if (hslot >= 0 && hero_pack_recolour(hslot, c, &rc)) c = rc;
                    if (tint) c = maroon(c);
                }
            } else if (won) c = game;                  /* the game's own sprite pixel */
            fb_hd[(y * s + sy) * hd_w + x * s + sx] = c;
        }
    }
}
static void save_frame(void)
{
    char name[512]; FILE *f; int y, x, W = screen_w(), gw, gh;
    const uint32_t *g2 = mods_g2_attract_frame(&gw, &gh);
    snprintf(name, sizeof name, "%s/frame_%05ld.ppm", outdir, frame_no);
    f = fopen(name, "wb");
    if (!f) { perror(name); return; }
    if (g2) {                                            /* a Gauntlet II attract picture is showing */
        fprintf(f, "P6\n%d %d\n255\n", gw, gh);
        for (y = 0; y < gw * gh; y++) { fputc(g2[y] >> 16, f); fputc((g2[y] >> 8) & 255, f); fputc(g2[y] & 255, f); }
        fclose(f); return;
    }
    if (pack_scale > 0 && fb_hd) {                     /* a graphics pack: the picture at its resolution */
        fprintf(f, "P6\n%d %d\n255\n", hd_w, hd_h);
        for (y = 0; y < hd_w * hd_h; y++) { uint32_t p = fb_hd[y]; fputc(p >> 16, f); fputc((p >> 8) & 255, f); fputc(p & 255, f); }
        fclose(f); return;
    }
    fprintf(f, "P6\n%d 224\n255\n", W);
    for (y = 0; y < 224; y++)
        for (x = 0; x < W; x++) { uint32_t p = fb[y][x]; fputc(p >> 16, f); fputc((p >> 8) & 255, f); fputc(p & 255, f); }
    fclose(f);
}

/* time: a frame also runs when this many instructions have run without one (G4_TICK build):
   about one frame of 68000 time, so code that waits for interrupt-driven RAM sees time pass */
#define TICKS_PER_FRAME 25000
long g4_ticks = TICKS_PER_FRAME;
static int in_frame;
static void run_frame(void);
uint32_t g4_tick_pc;

/* sound time within a frame: lines of audio made so far. The frame loop brings it up to each
   line; code running inside the frame (interrupt handlers) also moves it on, one line per
   about one line of 68000 time, so a handler that waits for the Z80 sees the Z80 run */
#define INSTR_PER_LINE 60
static int audio_done;
static void audio_to(int line) { while (audio_done < line) { audio_line(); audio_done++; } }

void g4_tick_frame(M68K *c)
{
    (void)c;
    if (in_frame) { g4_ticks = INSTR_PER_LINE; audio_line(); audio_done++; return; }
    run_frame();
}

/* ---- services for the in-game lobby (g4mods.c lobby_run) */
static uint8_t lobby_prev;
uint8_t host_local_pressed(void)                         /* buttons newly pressed on this machine's controls */
{
    uint8_t all = (uint8_t)(held[0] | held[1] | held[2] | held[3]), p = (uint8_t)(all & ~lobby_prev);
    lobby_prev = all;
    return p;
}
void host_text_begin(void)
{
    char *e;
    text_done = 0; text_buf[0] = 0;
    launch_load_join(launch_base, text_buf, sizeof text_buf);   /* the last address used */
    if ((e = getenv("G4JOINADDR"))) { snprintf(text_buf, sizeof text_buf, "%s", e); text_done = 1; }   /* test aid */
    text_active = 1;
#ifdef G4_SDL
    if (use_window && win) SDL_StartTextInput(win);
#endif
}
int host_text_state(void) { return text_done; }          /* 0 typing, 1 Enter, -1 Esc */
const char *host_text(void) { return text_buf; }
void host_text_end(void)
{
    text_active = 0; text_done = 0;
#ifdef G4_SDL
    if (use_window && win) SDL_StopTextInput(win);
#endif
}
static uint32_t rom_hash_value;                          /* the ROM + level set, as online play checks */
uint32_t host_rom_hash(void) { return rom_hash_value; }
/* restart the program online: the same options, plus --host / --join with the lobby's outcome */
static int saved_argc; static char **saved_argv;
void host_relaunch(int hosting, int players, int seat, const char *addr)
{
    static char *av[64], pl[8], st[8], jn[300]; int n = 0, i;
    for (i = 0; i < saved_argc && n < 50; i++) {
        const char *a = saved_argv[i];
        if (!strcmp(a, "--host") || !strcmp(a, "--join") || !strcmp(a, "--players") || !strcmp(a, "--seat") || !strcmp(a, "--frames") ||
            !strcmp(a, "--press")) { i++; continue; }        /* (scripted test presses are not repeated) */
        if (!strcmp(a, "--online-start")) continue;
        av[n++] = saved_argv[i];
    }
    av[n++] = "--frames"; av[n++] = "0"; av[n++] = "--online-start";
    if (hosting) { snprintf(pl, sizeof pl, "%d", players); av[n++] = "--host"; av[n++] = "47100"; av[n++] = "--players"; av[n++] = pl; }
    else { snprintf(st, sizeof st, "%d", seat); snprintf(jn, sizeof jn, "%s:47100", addr); av[n++] = "--join"; av[n++] = jn; av[n++] = "--seat"; av[n++] = st; }
    av[n] = NULL;
    fprintf(stderr, "lobby: restarting online (%s)\n", hosting ? "host" : "join");
    if (wav) wav_close();
#ifdef G4_SDL
    {   SDL_Process *p = SDL_CreateProcess((const char *const *)av, false);   /* a fresh copy, then this one ends */
        if (!p) { fprintf(stderr, "lobby: could not restart: %s\n", SDL_GetError()); exit(1); }
        SDL_Quit(); exit(0); }
#else
    {   extern int execv(const char *, char *const *); execv(av[0], av); }
    perror("lobby: could not restart");
    exit(1);
#endif
}
static void run_frame(void)
{
    int y, k, hcount;
    g4_ticks = TICKS_PER_FRAME;
    if (in_frame) return;
    in_frame = 1;
    g4_ticks = INSTR_PER_LINE;
    status_reads_since_frame = 0;
    if (getenv("G4PADDEBUG") && frame_no % 30 == 0) {                /* the game's pad RAM */
        int i; fprintf(stderr, "f%ld ffeb0c=%04x port1:", frame_no, rd16(0xFFEB0C));
        for (i = 0; i < 16; i++) fprintf(stderr, " %02x", rd8(0xFFEB0E + i));
        fprintf(stderr, "  cnt=%d\n", port[0].counter);
    }
    if (getenv("G4PEEK") && frame_no % 60 == 0) {        /* G4PEEK=ffe758,ffe760: RAM words */
        char buf[256], *p; snprintf(buf, sizeof buf, "%s", getenv("G4PEEK"));
        fprintf(stderr, "f%ld", frame_no);
        for (p = strtok(buf, ","); p; p = strtok(NULL, ",")) fprintf(stderr, " %s=%04X", p, rd16((uint32_t)strtoul(p, NULL, 16) | 0xFF0000u));
        fprintf(stderr, "\n");
    }
    if (getenv("G4SNDDEBUG") && frame_no % 60 == 0) {
        fprintf(stderr, "f%ld ym68k=%ld ymz80=%ld dac=%ld maxq=%ld psg=%ld z80T=%ld z80pc=%04X busreq=%d\n", frame_no,
                audio_stat[0], audio_stat[1], audio_stat[2], audio_stat[3], audio_stat[4], audio_stat[5], audio_z80_pc(), z80_busreq);
        memset(audio_stat, 0, sizeof audio_stat);
    }
    if (net_active && getenv("G4NETBREAK") && frame_no == atol(getenv("G4NETBREAK")))
        g4_mem[0xFFF800] ^= 1;                           /* test only: make this copy differ */
    if (net_active && frame_no % 60 == 0)                /* state check: 68000 RAM and Z80 RAM */
        net_hash(frame_no, hash_bytes(hash_bytes(2166136261u, g4_mem + 0xFF0000, 0x10000), g4z80_ram, 0x2000));
    memset(held, 0, sizeof held);
    if (use_window) window_input();
    for (k = 0; k < npresses; k++)
        if (frame_no >= presses[k].from && frame_no <= presses[k].to) held[presses[k].player] |= (uint8_t)presses[k].button;
    mods_online = net_active;
    if (net_active) { int k; pad2_present = 0; for (k = 0; k < net_players; k++) if (net_seat[k] == 1) pad2_present = 1; }   /* online: the same on every copy */
    else if (held[1]) pad2_present = 1;                  /* stays once seen */
#ifdef G4_SDL
    else if (use_window && ctl[1]) pad2_present = 1;     /* a second gamepad */
#endif
    if (net_active) {                                    /* online: all local controls are this player */
        uint8_t local = (uint8_t)(held[0] | held[1] | held[2] | held[3]);
        net_frame(frame_no, local, held);
        if (mods_menu_host_only) { int k; for (k = 0; k < 4; k++) if (k != net_seat[0]) held[k] = 0; }   /* menus: the host only */
    }
    pause_menu_input();
    arcade_colours_frame();
    hcount = vdp.reg[10];
    for (y = 0; y < 224; y++) {
        vdp.line = y;
        render_line(y);
        audio_to(y + 1);
        if (--hcount < 0) {
            hcount = vdp.reg[10];
            if (vdp.reg[0] & 0x10) interrupt(4, hblank_handler);
        }
    }
    vdp.line = 224;
    if (vdp.reg[1] & 0x20) interrupt(6, g4_lookup(rd32(0x78)));
    mods_after_vblank(frame_no);                         /* pads read, game logic not yet run */
    audio_to(262);                                       /* the VBlank lines */
    audio_done = 0;
    audio_out();
    if (extract_music_active && extract_frame(frame_no)) exit(0);
    if (pack_info_on) memcpy(fb_base, fb, sizeof fb);   /* (graphics packs: before anything is drawn on top) */
    logo_edit(mods_logo_kind());
    pause_menu_draw();
    {   const char *ll[10]; int n = mods_lobby_lines(ll, 10), k, x, y;   /* the in-game lobby (ONLINE) */
        if (n) {
            for (y = 40; y < 52 + 12 * n; y++) for (x = 10; x < 210; x++) fb[y][x] = (y == 40 || y == 51 + 12 * n || x == 10 || x == 209) ? 0xFFFFFF : 0;
            for (k = 0; k < n; k++) if (ll[k][0]) pm_text(18, 46 + 12 * k, ll[k], k == 0 ? 0xFFE040 : 0xD0D0D0);
        }
    }
    {   const char *b = mods_banner(frame_no);           /* Gauntlet II pickup message */
        if (b) {
            int w = (int)strlen(b) * 8, x0 = 108 - w / 2, xx, yy;
            if (x0 < 2) x0 = 2;
            for (yy = 18; yy < 32; yy++) for (xx = x0 - 4; xx < x0 + w + 4 && xx < 216; xx++) if (xx >= 0) fb[yy][xx] = 0;
            pm_text(x0, 21, b, 0xFFE040);
        }
    }
    if (pack_info_on) pack_frame();                    /* graphics pack picture / tile dump */
    for (k = 0; k < nshots; k++) if (shots[k] == frame_no) {
        save_frame();
        if (getenv("G4RAMDUMP")) {                       /* debug: 68000 RAM next to the picture */
            char name[512]; FILE *rf;
            snprintf(name, sizeof name, "%s/ram_%05ld.bin", outdir, frame_no);
            if ((rf = fopen(name, "wb"))) { fwrite(g4_mem + 0xFF0000, 1, 0x10000, rf); fclose(rf); }
            snprintf(name, sizeof name, "%s/vram_%05ld.bin", outdir, frame_no);
            if ((rf = fopen(name, "wb"))) { fwrite(vdp.vram, 1, 0x10000, rf); fclose(rf); }
            snprintf(name, sizeof name, "%s/cram_%05ld.bin", outdir, frame_no);
            if ((rf = fopen(name, "wb"))) { fwrite(vdp.cram, 2, 64, rf); fclose(rf); }
        }
        if (getenv("G4VDPDEBUG"))
            fprintf(stderr, "frame %ld: reg1=%02X (display %s) reg7=%02X backdrop CRAM=%03X\n", frame_no, vdp.reg[1],
                    (vdp.reg[1] & 0x40) ? "on" : "off", vdp.reg[7], vdp.cram[vdp.reg[7] & 0x3F]);
        if (getenv("G4VDPDEBUG")) { int r; fprintf(stderr, "regs"); for (r = 0; r < 24; r++) fprintf(stderr, " %02X", vdp.reg[r]); fprintf(stderr, "\n"); }
    }
    if (use_window) window_present();
    frame_no++;
    if (max_frames && frame_no >= max_frames) { fprintf(stderr, "%ld frames run\n", frame_no); exit(0); }
    in_frame = 0;
    g4_ticks = TICKS_PER_FRAME;
}

/* The side panel's logo (window plane, its first tile is 0x683) reads "GAUNTLET 4". In Arcade and
   Record mode the host names the game actually being played: the "4" (the blue strokes joined to
   the point (70,7) of the logo, and the outline around them) is erased, and then "GAUNTLET" is
   centred (arcade Gauntlet mazes, kind 1) or a plain "II" is drawn in the logo's colours (the
   Gauntlet II mazes, kind 2). Only the picture the ROM drew is used. The logo's colours cycle
   (fading through black), so they are read every frame and the "II" follows them. */
static void logo_edit(int kind)
{
    static uint8_t four[24][82];                         /* the 4: 1 blue, 2 outline (the same every frame) */
    static int have_mask;
    uint16_t base = (uint16_t)((vdp.reg[3] & 0x3C) << 10);
    int r, col, X = -1, Y = 0, x, y, dx, dy, sp = 0;
    static int stack[24 * 82];
    uint32_t blue, white, bg;
    if (!kind) return;
    for (r = 0; r < 6 && X < 0; r++) for (col = 20; col < 40; col++) {
        uint16_t e = (uint16_t)((vdp.vram[(base + (r * 64 + col) * 2) & 0xFFFF] << 8) | vdp.vram[(base + (r * 64 + col) * 2 + 1) & 0xFFFF]);
        if ((e & 0x7FF) == 0x683) { X = col * 8; Y = r * 8; break; }
    }
    if (X < 0 || X + 82 > 320 || Y + 24 > 224) return;
    blue = fb[Y + 7][X + 70]; bg = fb[Y + 2][X + 78];
    if (blue == bg && !have_mask) return;                /* the colour cycle is at black: no shape yet */
    white = bg;
    for (y = 0; y < 24; y++) for (x = 0; x < 82; x++) if (fb[Y + y][X + x] != bg && fb[Y + y][X + x] != blue) { white = fb[Y + y][X + x]; y = 24; break; }
    if (blue == bg) goto erase;                          /* black moment of the cycle: the shape found before */
    memset(four, 0, sizeof four); have_mask = 1;
    four[7][70] = 1; stack[sp++] = 7 * 82 + 70;          /* the 4's blue strokes */
    while (sp) {
        int v = stack[--sp], k; static const int d[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1}};
        for (k = 0; k < (v % 82 >= 63 ? 8 : 4); k++) {   /* corners too right of the E (the tail's tip) */
            x = v % 82 + d[k][0]; y = v / 82 + d[k][1];
            if (x < 0 || x >= 82 || y < 0 || y >= 24 || four[y][x] || fb[Y + y][X + x] != blue) continue;
            four[y][x] = 1; stack[sp++] = y * 82 + x;
        }
    }
    for (y = 0; y < 24; y++) for (x = 0; x < 82; x++) {
        int near4 = 0, nearother = 0;
        if (four[y][x] == 1) continue;
        if (fb[Y + y][X + x] != white) continue;
        for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++) {
            int xx = x + dx, yy = y + dy;
            if (xx < 0 || xx >= 82 || yy < 0 || yy >= 24) continue;
            if (four[yy][xx] == 1) near4 = 1;
            else if (fb[Y + yy][X + xx] == blue) nearother = 1;
        }
        if (near4 && !nearother) four[y][x] = 2;         /* the 4's outline */
    }
erase:
    for (y = 0; y < 24; y++) for (x = 0; x < 82; x++) if (four[y][x]) fb[Y + y][X + x] = bg;
    if (kind == 1) {                                     /* centre "GAUNTLET" */
        for (y = 0; y < 24; y++) {
            for (x = 81; x >= 6; x--) fb[Y + y][X + x] = fb[Y + y][X + x - 6];
            for (x = 0; x < 6; x++) fb[Y + y][X + x] = bg;
        }
    } else {                                             /* "II" / "III": bars, outline and blue like the letters */
        int i;
        for (i = 0; i < (kind == 3 ? 3 : 2); i++) for (y = 2; y <= 16; y++) for (x = 0; x < 4; x++) {
            int edge = (y == 2 || y == 16 || x == 0 || x == 3);
            fb[Y + y][X + 66 + 5 * i + x] = edge ? white : blue;
        }
    }
}

/* The pause loop 0x1FB80 reads each active hero's button byte (+$22 of the records listed at
   $FF0B50, count-1 at $FF0B60; active low, S A C B R L D U). The menu's press is written there after
   the VBlank has read the pads, so it reaches whichever hero each controller plays (the record is
   the hero, not the controller number) and no empty slot's Start can add a hero. */
static void host_wait_vblank(M68K *c)
{
    (void)c; run_frame();
    if (pm_inject) {
        int n = (int16_t)rd16(0xFF0B60), k;
        uint8_t clear = (uint8_t)(((pm_inject & P_START) ? 0x80 : 0) | ((pm_inject & P_A) ? 0x40 : 0) |
                                  ((pm_inject & P_C) ? 0x20 : 0) | ((pm_inject & P_B) ? 0x10 : 0));
        for (k = 0; k <= n && k < 4; k++) {
            uint32_t rec = rd32(0xFF0B50 + 4u * (uint32_t)k);
            if (rec >= 0xFF0040 && rec < 0xFF0040 + 4 * 0x2C4) wr8(rec + 0x22, (uint8_t)(rd8(rec + 0x22) & ~clear));
        }
        pm_inject = 0;
    }
}
extern void (*g4_host_override)(uint32_t addr, void (**fn)(M68K *));
static uint32_t dbg_last_call;
uint32_t dbg_last_call_x(void) { return dbg_last_call; }
static void override(uint32_t addr, void (**fn)(M68K *))
{
    dbg_last_call = addr;
    if (addr == 0x020496) { *fn = host_wait_vblank; return; }
    if (addr == 0x01FB80 && mods_enabled) { if (*fn != pause_loop) { orig_pause_loop = *fn; *fn = pause_loop; } return; }
    mods_override(addr, fn);
}

/* ------------------------------------------------------------------ main */
static int button(const char *s)
{
    static const char *names[] = {"up", "down", "left", "right", "a", "b", "c", "start"};
    int i;
    for (i = 0; i < 8; i++) if (!strcmp(s, names[i])) return 1 << i;
    fprintf(stderr, "unknown button %s\n", s); exit(1);
}

/* The port is translated from the September 1993 ROM and uses its addresses everywhere. Refuse the
   August 1993 ROM (a different revision: code moved and changed) with a clear message, and warn
   about any other ROM (patched, hacked or unknown) but still try it. */
static uint32_t crc32_of(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu; size_t i; int k;
    for (i = 0; i < n; i++) { c ^= p[i]; for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1))); }
    return ~c;
}

static void rom_message(const char *title, const char *text)
{
    fprintf(stderr, "%s: %s\n", title, text);
#ifdef G4_SDL
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, title, text, NULL);
#endif
}

static void rom_check(const char *path, long size)
{
    uint32_t crc = crc32_of(g4_mem, 0x100000);
    char text[512];
    if (size == 0x100000 && crc == 0xF9D60510u) return;              /* September 1993: the right one */
    if (size == 0x100000 && crc == 0x3BF46DCEu) {
        snprintf(text, sizeof text, "%s is the August 1993 version of Gauntlet 4, which this program "
                 "does not support.\n\nPlease use the September 1993 version "
                 "(1,048,576 bytes, CRC32 f9d60510).", path);
        rom_message("Wrong Gauntlet 4 version", text);
        exit(1);
    }
    snprintf(text, sizeof text, "%s is not the September 1993 Gauntlet 4 ROM this program was made from "
             "(found %ld bytes, CRC32 %08x; expected 1,048,576 bytes, CRC32 f9d60510).\n\n"
             "It will try to run it anyway, but it may glitch or crash.", path, size, (unsigned)crc);
    rom_message("Unrecognised ROM", text);
}

int main(int argc, char **argv)
{
    FILE *f; int i, net_mode = 0, net_port = 0, net_want = 2, net_want_seat = 0, online_start = 0; char net_addr[256]; const char *colours_file = NULL, *g2_file = NULL;
    setvbuf(stderr, NULL, _IONBF, 0);                    /* messages at once, also into a pipe (Windows) */
    if (argc == 4 && !strcmp(argv[1], "--music-wav")) return music_to_wav(argv[2], argv[3]);   /* no ROM needed */
    if (argc == 3 && !strcmp(argv[1], "--scan")) return launch_scan_cli(argv[2]);              /* launcher: ROM finder */
    if (argc == 4 && !strcmp(argv[1], "--build-mazes")) {   /* data step (test): arcade_g2.json from a ROM folder */
        static launch_roms r; launch_scan(argv[2], &r);
        return data_g2_mazes_json(&r, argv[3]) ? 0 : 1;
    }
    if (argc == 4 && !strcmp(argv[1], "--sfx-wavs")) return data_g2_sfx_wavs(argv[2], argv[3]) ? 0 : 1;
    if (argc == 4 && !strcmp(argv[1], "--build-sfx")) { static launch_roms r; launch_scan(argv[2], &r); return data_g2_sfx(launch_zip_for(&r, "136043-1120.16r"), launch_zip_for(&r, "136043-1119.16s"), argv[3], 74) ? 0 : 1; }
    if (argc == 4 && !strcmp(argv[1], "--build-g3")) { static launch_roms r; launch_scan(argv[2], &r); fprintf(stderr, "Atari side 2: %s\nDeeper Dungeons: %s\n", r.atr_orig[0] ? r.atr_orig : "-", r.atr_dd[0] ? r.atr_dd : "-"); return data_g3_levels(&r, argv[3]) ? 0 : 1; }
    if (argc == 4 && !strcmp(argv[1], "--build-levels")) { static launch_roms r; launch_scan(argv[2], &r); return data_g2_levels(&r, argv[3]) ? 0 : 1; }
    if (argc == 5 && !strcmp(argv[1], "--build-all-colours")) {   /* test: both parts of arcade_colours.txt */
        static launch_roms r; launch_scan(argv[2], &r);
        return data_g1_colours(&r, argv[4]) && data_g2_colours(&r, argv[3], argv[4]) ? 0 : 1;
    }
    if (argc == 4 && !strcmp(argv[1], "--build-attract")) { static launch_roms r; launch_scan(argv[2], &r); return data_g2_attract(&r, argv[3], NULL) ? 0 : 1; }
    if (argc == 3 && !strcmp(argv[1], "--dragons")) { static launch_roms r; launch_scan(argv[2], &r); return data_g2_dragons_check(&r) ? 0 : 1; }
    if (argc == 4 && !strcmp(argv[1], "--build-sprites")) { static launch_roms r; launch_scan(argv[2], &r); return data_g2_sprites(&r, argv[3]) ? 0 : 1; }
    if (argc == 5 && !strcmp(argv[1], "--build-colours")) { static launch_roms r; launch_scan(argv[2], &r); return data_g2_colours(&r, argv[3], argv[4]) ? 0 : 1; }
    if (argc == 4 && !strcmp(argv[1], "--build-speech"))   /* data step (test): g2_speech.bin from a gauntlet2.zip */
        return data_speech(argv[2], argv[2], argv[3]) ? 0 : 1;
    if (argc < 2 || !strcmp(argv[1], "--launcher")) {     /* no ROM given: the launcher picks one (g4launch.c) */
        static launch_choice ch; static char *av[40], port_arg[16], join_arg[300];
        char base[1024]; const char *slash = strrchr(argv[0], '/'), *bs = strrchr(argv[0], '\\');
        int n = 0;
        if (bs > slash) slash = bs;
        snprintf(base, sizeof base, "%.*s", slash ? (int)(slash - argv[0] + 1) : 0, argv[0]);
        if (!launch_run(base, &ch)) return 0;
        fprintf(stderr, "launcher: start %s (mode %d)\n", ch.rom, ch.mode);
        av[n++] = argv[0]; av[n++] = ch.rom; av[n++] = "--window"; av[n++] = "--frames"; av[n++] = "0";
        {   static char vm[8], ve[8], vv[8];
            snprintf(vm, sizeof vm, "%d", ch.vol_music); snprintf(ve, sizeof ve, "%d", ch.vol_sfx); snprintf(vv, sizeof vv, "%d", ch.vol_voice);
            if (!ch.vol_music) av[n++] = "--nomusic";
            av[n++] = "--music-volume"; av[n++] = vm; av[n++] = "--sfx-volume"; av[n++] = ve; av[n++] = "--voice-volume"; av[n++] = vv; }
        {   static char res[32]; static const char *disp[3] = {"window", "fullscreen", "borderless"}, *asp[4] = {"4:3", "square", "stretch", "sharp"};
            snprintf(res, sizeof res, "%dx%d", launch_resolutions[ch.res][0], launch_resolutions[ch.res][1]);
            av[n++] = "--resolution"; av[n++] = res; av[n++] = "--display"; av[n++] = (char *)disp[ch.display];
            av[n++] = "--aspect"; av[n++] = (char *)asp[ch.aspect];
            if (ch.pack[0]) { av[n++] = "--pack"; av[n++] = ch.pack; } }
        if (ch.mode == LAUNCH_HOST) { snprintf(port_arg, sizeof port_arg, "%d", ch.players); av[n++] = "--host"; av[n++] = "47100"; av[n++] = "--players"; av[n++] = port_arg; }
        if (ch.mode == LAUNCH_JOIN) { snprintf(join_arg, sizeof join_arg, "%s%s", ch.join, strchr(ch.join, ':') ? "" : ":47100"); av[n++] = "--join"; av[n++] = join_arg; }
        av[n] = NULL; argc = n; argv = av;
    }
    {   const char *slash = strrchr(argv[0], '/'), *bs = strrchr(argv[0], '\\');
        if (bs > slash) slash = bs;
        snprintf(launch_base, sizeof launch_base, "%.*s", slash ? (int)(slash - argv[0] + 1) : 0, argv[0]);
        snprintf(mods_base, sizeof mods_base, "%s", launch_base);
        launch_load_volumes(launch_base, &audio_vol_music, &audio_vol_sfx, &audio_vol_voice); }   /* (options below win) */
    g4_mem = calloc(1, 0x1000000);
    f = fopen(argv[1], "rb");
    if (!f || fread(g4_mem, 1, 0x100000, f) == 0) {
        char text[512];
        snprintf(text, sizeof text, "Could not read the ROM %s.\n\nPut your Gauntlet 4 ROM (September 1993 "
                 "version) next to ghost.exe and name it gauntlet4.bin.", argv[1]);
        rom_message("ROM not found", text);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    rom_check(argv[1], ftell(f));
    fclose(f);
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) max_frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--teamplayer")) teamplayer = 1;
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav_open(argv[++i]);
        else if (!strcmp(argv[i], "--extract-music") && i + 1 < argc) extract_music_start(argv[++i]);
        else if (!strcmp(argv[i], "--progress") && i + 1 < argc) extract_progress_file(argv[++i]);
        else if (!strcmp(argv[i], "--nosound")) no_sound = 1;
        else if (!strcmp(argv[i], "--nomusic")) mods_no_music = 1;
        else if (!strcmp(argv[i], "--resolution") && i + 1 < argc) { int w, h; if (sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w >= 320 && h >= 224) { video_w = w; video_h = h; } }
        else if (!strcmp(argv[i], "--display") && i + 1 < argc) { const char *v = argv[++i]; video_display = !strcmp(v, "fullscreen") ? 1 : !strcmp(v, "borderless") ? 2 : 0; }
        else if (!strcmp(argv[i], "--aspect") && i + 1 < argc) { const char *v = argv[++i]; video_aspect = !strcmp(v, "square") ? 1 : !strcmp(v, "stretch") ? 2 : !strcmp(v, "sharp") ? 3 : 0; }
        else if (!strcmp(argv[i], "--pack") && i + 1 < argc) snprintf(video_pack, sizeof video_pack, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--dump-tiles") && i + 1 < argc) pack_dump_open(argv[++i]);
        else if (!strcmp(argv[i], "--music-volume") && i + 1 < argc) audio_vol_music = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sfx-volume") && i + 1 < argc) audio_vol_sfx = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--voice-volume") && i + 1 < argc) audio_vol_voice = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--original")) mods_enabled = 0;
        else if (!strcmp(argv[i], "--arcade-colours")) arcade_colours = 1;
        else if (!strcmp(argv[i], "--level-set") && i + 1 < argc) {
            const char *set = argv[++i];
            if (!strcmp(set, "gauntlet2")) level_set_g2 = 1;
            else if (!strcmp(set, "gauntlet3")) level_set_g2 = level_set_g3 = 1;
            else if (strcmp(set, "gauntlet4")) { fprintf(stderr, "--level-set gauntlet4 | gauntlet2 | gauntlet3\n"); return 1; }
        }
        else if (!strcmp(argv[i], "--g2-levels") && i + 1 < argc) g2_file = argv[++i];
        else if (!strcmp(argv[i], "--colours-file") && i + 1 < argc) colours_file = argv[++i];
        else if (!strcmp(argv[i], "--host") && i + 1 < argc) net_port = atoi(argv[++i]), net_mode = 1;
        else if (!strcmp(argv[i], "--players") && i + 1 < argc) net_want = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seat") && i + 1 < argc) net_want_seat = atoi(argv[++i]);   /* joiner: 1-3 (2P-4P) */
        else if (!strcmp(argv[i], "--online-start")) online_start = 1;
        else if (!strcmp(argv[i], "--delay") && i + 1 < argc) net_delay = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--join") && i + 1 < argc) {
            char *colon; net_mode = 2;
            snprintf(net_addr, sizeof net_addr, "%s", argv[++i]);
            colon = strrchr(net_addr, ':');
            if (!colon) { fprintf(stderr, "--join HOST:PORT\n"); return 1; }
            *colon = 0; net_port = atoi(colon + 1);
        }
        else if (!strcmp(argv[i], "--window")) { use_window = 1; if (max_frames == 600) max_frames = 0; }
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) outdir = argv[++i];
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) {
            char *p = argv[++i];
            while (*p && nshots < 64) { shots[nshots++] = (int)strtol(p, &p, 10); if (*p == ',') p++; }
        } else if (!strcmp(argv[i], "--press") && i + 1 < argc && npresses < 32) {
            char b[16]; int from, to, player = 1; const char *spec = argv[++i];
            if (spec[0] >= '1' && spec[0] <= '4' && spec[1] == ':') { player = spec[0] - '0'; spec += 2; }
            if (sscanf(spec, "%15[a-z]@%d-%d", b, &from, &to) != 3) { fprintf(stderr, "bad --press %s\n", argv[i]); return 1; }
            presses[npresses].button = button(b); presses[npresses].from = from; presses[npresses].to = to;
            presses[npresses].player = player - 1; npresses++;
        }
    }
    if (getenv("G4MAXFRAMES")) max_frames = atol(getenv("G4MAXFRAMES"));
    if (getenv("G4YMLOG") && (ymlog = fopen(getenv("G4YMLOG"), "w"))) audio_write_hook = ymlog_write;   /* test aid: stop after N frames */
    if (video_pack[0]) pack_open(launch_base, video_pack);   /* a graphics pack (g4pack.c) */
    pack_info_on = pack_scale > 0 || pack_dumping;
    port[0].th = port[0].tr = port[1].th = port[1].tr = 1;    /* lines float high until driven */
    audio_init();
    audio_debug = getenv("G4SNDDEBUG") != NULL;
    if (mods_enabled) {                                  /* Gauntlet II level set: given file, else next to the program;
                                                            without one the Level Set option does not appear */
        int ok = 0;
        if (g2_file) { if (!mods_load_g2(g2_file)) return 1; ok = 1; }
        else {
            FILE *t = fopen("gauntlet2_levels.bin", "rb");
            if (t) { fclose(t); ok = mods_load_g2("gauntlet2_levels.bin"); }
            else {
                char path[1024]; const char *slash = strrchr(argv[0], '/'), *bs = strrchr(argv[0], '\\');
                if (bs > slash) slash = bs;
                snprintf(path, sizeof path, "%.*sgauntlet2_levels.bin", slash ? (int)(slash - argv[0] + 1) : 0, argv[0]);
                if ((t = fopen(path, "rb"))) { fclose(t); ok = mods_load_g2(path); }
            }
        }
        {   /* GAUNTLET III (the Atari 8-bit mazes): next to the game, else next to the program */
            FILE *t = fopen("gauntlet3_levels.bin", "rb"); int ok3 = 0;
            if (t) { fclose(t); ok3 = mods_load_g3("gauntlet3_levels.bin"); }
            else {
                char path[1024]; const char *slash = strrchr(argv[0], '/'), *bs = strrchr(argv[0], '\\');
                if (bs > slash) slash = bs;
                snprintf(path, sizeof path, "%.*sgauntlet3_levels.bin", slash ? (int)(slash - argv[0] + 1) : 0, argv[0]);
                if ((t = fopen(path, "rb"))) { fclose(t); ok3 = mods_load_g3(path); }
            }
            if (level_set_g3) {
                if (!ok3) { fprintf(stderr, "level set: gauntlet3_levels.bin not found\n"); return 1; }
                mods_use_g3();
                ok = 1;
            }
        }
        if (!ok) {
            if (level_set_g2) { fprintf(stderr, "level set: gauntlet2_levels.bin not found\n"); return 1; }
        }
    } else level_set_g2 = 0;
    mods_init();                                         /* before the online ROM check: it covers the extras */
    if (mods_enabled) {                                  /* arcade colours: given file, else next to the program */
        if (colours_file) arcade_colours_load(colours_file);
        else {
            char path[1024]; const char *slash = strrchr(argv[0], '/'), *bs = strrchr(argv[0], '\\');
            if (bs > slash) slash = bs;
            arcade_colours_load("arcade_colours.txt");
            if (!n_ac && slash) { snprintf(path, sizeof path, "%.*sarcade_colours.txt", (int)(slash - argv[0] + 1), argv[0]); arcade_colours_load(path); }
        }
    }
    if (use_window) window_open();
    rom_hash_value = hash_bytes(hash_bytes(hash_bytes(2166136261u, g4_mem, 0x100000), g4_mem + 0x380000, mods_g2_size()), g4_mem + 0x300000, mods_g3_size());
    saved_argc = argc; saved_argv = argv;                 /* (host_relaunch) */
    if (net_mode) {
        uint32_t h = rom_hash_value;                     /* the level set must match too */
        net_idle = net_wait_events;
        if (net_mode == 1) net_host(net_port, net_want, h); else net_join(net_addr, net_port, h, net_want_seat);
        if (net_teamplayer) teamplayer = 1;
        if (online_start && getenv("G4ONLINEPRESS") && npresses < 32) {   /* test aid: one press after the restart */
            char b[16]; int from, to;
            if (sscanf(getenv("G4ONLINEPRESS"), "%15[a-z]@%d-%d", b, &from, &to) == 3) {
                presses[npresses].button = button(b); presses[npresses].from = from; presses[npresses].to = to; presses[npresses].player = 0; npresses++;
            }
        }
        if (online_start && net_player == 0 && npresses < 32) {   /* restarted from the lobby: the host's Start
                                                                     skips the title (it travels to everyone) */
            presses[npresses].button = P_START; presses[npresses].from = 400; presses[npresses].to = 403; presses[npresses].player = 0; npresses++;
        }
        if (use_window) window_title();
    }
    g4_io.read = io_read; g4_io.write = io_write;
    g4_host_override = override;
    cpu.a[7] = rd32(0); cpu.sr = 0x2700;
    g4_entry(&cpu);                                      /* reset: never returns; run_frame exits */
    return 0;
}
