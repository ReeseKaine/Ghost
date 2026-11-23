/* g2run.c -- running pieces of the arcade Gauntlet II program (the player's own gauntlet2.zip) on
   the Musashi 68000 emulator (host/musashi, MIT licence), for the launcher's data steps. The
   C counterpart of the Python tools' Unicorn set-up (render_gauntlet2.py, import_gauntlet2.py):
   the program image as MAME loads it, a flat 24-bit memory (everything not loaded reads 0, as
   Unicorn's freshly mapped pages do), routines called with a stack whose return address is a
   STOP address, and address hooks that can stand in for a routine.
   First user: the maze list (data_g2_mazes = import_gauntlet2.py). Nothing from the game is in
   this file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "musashi/m68k.h"
#include "g4data.h"
#include "g2run.h"

static uint8_t *mem;                                   /* 16 MB */
static uint8_t prog[0x80000], banks[0x8000];           /* the program, and the 4 slapstic banks */
#define STOP 0x7FF00
static int stopped; static long budget, executed; static uint32_t until_pc = STOP, stop_pc;
#define NOP_AT 0x7FF10
typedef struct { uint32_t addr; void (*fn)(void); } hook_t;
static hook_t hooks[16]; static int nhooks;

unsigned int m68k_read_memory_8(unsigned int a) { return mem[a & 0xFFFFFF]; }
unsigned int m68k_read_memory_16(unsigned int a) { a &= 0xFFFFFF; return (unsigned)mem[a] << 8 | mem[(a + 1) & 0xFFFFFF]; }
unsigned int m68k_read_memory_32(unsigned int a) { return m68k_read_memory_16(a) << 16 | m68k_read_memory_16(a + 2); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { mem[a & 0xFFFFFF] = (uint8_t)v; }
void m68k_write_memory_16(unsigned int a, unsigned int v) { a &= 0xFFFFFF; mem[a] = (uint8_t)(v >> 8); mem[(a + 1) & 0xFFFFFF] = (uint8_t)v; }
void m68k_write_memory_32(unsigned int a, unsigned int v) { m68k_write_memory_16(a, v >> 16); m68k_write_memory_16(a + 2, v & 0xFFFF); }
unsigned int m68k_read_disassembler_8(unsigned int a) { return m68k_read_memory_8(a); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return m68k_read_memory_16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return m68k_read_memory_32(a); }

void g2run_hook(unsigned int pc)                       /* Musashi: before every instruction */
{
    int k;
    pc &= 0xFFFFFF;
    if (pc == until_pc || ++executed > budget) {
        /* stop here -- but Musashi still runs the instruction at pc after this hook, so point it at
           a harmless NOP instead (a STOP there would leave the CPU halted for every later run) */
        stopped = 1; stop_pc = pc; m68k_end_timeslice(); m68k_set_reg(M68K_REG_PC, NOP_AT); return;
    }
    for (k = 0; k < nhooks; k++) if (hooks[k].addr == pc) { hooks[k].fn(); return; }
}

uint32_t g2_rd32(uint32_t a) { return m68k_read_memory_32(a); }
unsigned g2_rd16(uint32_t a) { return m68k_read_memory_16(a); }
void g2_wr32(uint32_t a, uint32_t v) { m68k_write_memory_32(a, v); }
void g2_wr16(uint32_t a, unsigned v) { m68k_write_memory_16(a, v); }
const uint8_t *g2_mem(void) { return mem; }
const uint8_t *g2_prog(void) { return prog; }
uint32_t g2_reg(int r) { return m68k_get_reg(NULL, (m68k_register_t)r); }
void g2_set_reg(int r, uint32_t v) { m68k_set_reg((m68k_register_t)r, v); }
void g2_hook(uint32_t addr, void (*fn)(void)) { if (nhooks < 16) { hooks[nhooks].addr = addr; hooks[nhooks].fn = fn; nhooks++; } }
void g2_unhook_all(void) { nhooks = 0; }

/* the program as MAME loads it (ROM_CONTINUE: each 32 KB chip pair's first half above its second) */
static int load_pair(const launch_roms *r, const char *e, const char *o, uint32_t hi, uint32_t lo, int whole)
{
    size_t na, nb, i; uint8_t *a = zip_read(launch_zip_for(r, e), e, &na), *b = zip_read(launch_zip_for(r, o), o, &nb);
    if (!a || !b || na != nb) { free(a); free(b); return 0; }
    if (whole) for (i = 0; i < na; i++) { prog[hi + 2 * i] = a[i]; prog[hi + 2 * i + 1] = b[i]; }
    else {
        size_t h = na / 2;
        for (i = 0; i < h; i++) { prog[hi + 2 * i] = a[i]; prog[hi + 2 * i + 1] = b[i]; prog[lo + 2 * i] = a[h + i]; prog[lo + 2 * i + 1] = b[h + i]; }
    }
    free(a); free(b);
    return 1;
}
int g2_load(const launch_roms *r)
{
    static int inited;
    if (!mem) mem = (uint8_t *)calloc(1, 0x1000000);
    if (!mem) return 0;
    if (!load_pair(r, "136037-1307.9a", "136037-1308.9b", 0x8000, 0x0, 0) ||
        !load_pair(r, "136043-1105.10a", "136043-1106.10b", 0x38000, 0, 1) ||
        !load_pair(r, "136043-1109.7a", "136043-1110.7b", 0x48000, 0x40000, 0) ||
        !load_pair(r, "136043-1121.6a", "136043-1122.6b", 0x58000, 0x50000, 0)) return 0;
    memcpy(banks, prog + 0x38000, 0x8000);
    if (!inited) { m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68020); inited = 1; }
    return 1;
}
/* another program image (the 1985 Gauntlet, g1colours.c): 0x80000 bytes, slapstic banks at 0x38000 */
int g2_load_image(const uint8_t *img)
{
    static int inited2;
    if (!mem) mem = (uint8_t *)calloc(1, 0x1000000);
    if (!mem) return 0;
    memcpy(prog, img, 0x80000); memcpy(banks, prog + 0x38000, 0x8000);
    if (!inited2) { m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68020); inited2 = 1; }
    return 1;
}
/* a fresh machine for one run, as a new Unicorn instance: program with that slapstic bank in the
   window, everything else zero, STOP instruction in place */
void g2_reset(int bank)
{
    memset(mem, 0, 0x1000000);
    memcpy(mem, prog, 0x80000);
    memcpy(mem + 0x38000, banks + bank * 0x2000, 0x2000);
    mem[STOP] = 0x4E; mem[STOP + 1] = 0x72; mem[STOP + 2] = 0x27; mem[STOP + 3] = 0x00;
    mem[NOP_AT] = 0x4E; mem[NOP_AT + 1] = 0x71;           /* nop (see g2run_hook) */
    g2_unhook_all();
}
/* call entry(args...) with the stack at 0x904E00 and run until it returns (or max instructions) */
int g2_call(uint32_t entry, const uint32_t *args, int nargs, long max_instr)
{
    uint32_t sp = 0x904E00; int k;
    m68k_write_memory_32(sp, STOP);
    for (k = 0; k < nargs; k++) m68k_write_memory_32(sp + 4 + 4 * (uint32_t)k, args[k]);
    m68k_pulse_reset();
    m68k_set_reg(M68K_REG_SR, 0x2700); m68k_set_reg(M68K_REG_A7, sp); m68k_set_reg(M68K_REG_PC, entry);
    stopped = 0; executed = 0; budget = max_instr; until_pc = STOP;
    while (!stopped) m68k_execute(100000);
    m68k_set_reg(M68K_REG_PC, stop_pc);
    return stop_pc == STOP;
}
/* run from pc until pc reaches `until` (Unicorn's emu_start(begin, until, count)): registers are
   kept from the run before, as with one long-lived Unicorn machine; the caller sets SR/A7 first */
int g2_run(uint32_t pc, uint32_t until, long max_instr)
{
    m68k_set_reg(M68K_REG_PC, pc);
    stopped = 0; executed = 0; budget = max_instr; until_pc = until & 0xFFFFFF;
    while (!stopped) m68k_execute(100000);
    until_pc = STOP;
    m68k_set_reg(M68K_REG_PC, stop_pc);
    return stop_pc == (until & 0xFFFFFF);
}
void g2_set_bank(int k) { memcpy(mem + 0x38000, banks + k * 0x2000, 0x2000); }   /* slapstic window */
uint8_t *g2_mem_w(void) { return mem; }

/* ------------------------------------------------------------------ the maze list */
#define WINDOW 0x38000
#define DECODE 0x4C1BC
#define PUT    0x45E40
#define SETUP  0x44AC2
static uint32_t bank_long(int k, uint32_t a) { uint32_t o = (uint32_t)k * 0x2000 + a - WINDOW; return (uint32_t)banks[o] << 24 | (uint32_t)banks[o + 1] << 16 | (uint32_t)banks[o + 2] << 8 | banks[o + 3]; }
static int dec_calls; static uint32_t dec_end;
static void put_hook(void)                             /* 0x45E40(index, cell, count): count it, return index + count */
{
    uint32_t sp = g2_reg(M68K_REG_A7), ret = g2_rd32(sp), idx = g2_rd32(sp + 4) & 0xFFFF, cnt = g2_rd32(sp + 12) & 0xFFFF;
    dec_calls++;
    g2_set_reg(M68K_REG_D0, (idx + cnt) & 0xFFFF);
    g2_set_reg(M68K_REG_A7, sp + 4); g2_set_reg(M68K_REG_PC, ret);
}
static void end_hook(void) { dec_end = g2_reg(M68K_REG_A2); }
static int g2_decode(uint32_t window, int bank, g2_maze *m)   /* 1 = decoded (length known) */
{
    uint32_t args[1]; int k;
    g2_reset(bank); g2_hook(PUT, put_hook); g2_hook(0x4C436, end_hook);
    dec_calls = 0; dec_end = 0; args[0] = window;
    g2_call(DECODE, args, 1, 2000000);
    m->calls = dec_calls; m->length = dec_end ? (int)(dec_end - window) : -1;
    for (k = 0; k < 11; k++) m->header[k] = mem[(window + (uint32_t)k) & 0xFFFFFF];
    return dec_end != 0;
}
static void g2_setup_map(uint32_t window, int bank, uint8_t *map)
{
    uint32_t args[1]; int k;
    g2_reset(bank); args[0] = window;
    g2_call(SETUP, args, 1, 20000000);
    for (k = 0; k < 1024; k++) map[k] = (uint8_t)(g2_rd16(0x903800 + 2u * (uint32_t)k) >> 10);
}
int data_g2_mazes(const launch_roms *r, g2_maze **out)
{
    uint32_t table, windows[256]; int nw = 0, k, i;
    static g2_maze chain[4][256]; static uint32_t chain_w[4][256]; int nchain[4] = {0, 0, 0, 0};
    g2_maze *mz;
    if (!g2_load(r)) return 0;
    table = bank_long(0, WINDOW);
    while (nw < 256) { uint32_t w = bank_long(0, table + 4u * (uint32_t)nw); if (w < 0x38000 || w >= 0x3A000) break; windows[nw++] = w; }
    for (k = 0; k < 4; k++) {                          /* each bank's records back to back: follow the chain */
        uint32_t w = k == 0 ? table + 4u * (uint32_t)nw : WINDOW;
        for (;;) {
            int inset = 0, seen = 0, j; g2_maze m; uint32_t e, nxt = 0;
            for (j = 0; j < nw; j++) if (windows[j] == w) inset = 1;
            for (j = 0; j < nchain[k]; j++) if (chain_w[k][j] == w) seen = 1;
            if (!inset || seen) break;
            memset(&m, 0, sizeof m);
            if (!g2_decode(w, k, &m) || m.length < 0) break;
            chain[k][nchain[k]] = m; chain_w[k][nchain[k]] = w; nchain[k]++;
            e = w + (uint32_t)m.length;
            for (j = 0; j < nw; j++) if (windows[j] >= e && windows[j] <= e + 2 && (!nxt || windows[j] < nxt)) nxt = windows[j];
            if (!nxt) break;
            w = nxt;
        }
    }
    mz = (g2_maze *)calloc((size_t)nw, sizeof *mz);
    for (i = 0; i < nw; i++) {
        int in[4], nb = 0, bank = -1, j;
        for (k = 0; k < 4; k++) { in[k] = 0; for (j = 0; j < nchain[k]; j++) if (chain_w[k][j] == windows[i]) in[k] = 1; if (in[k]) nb++; }
        if (nb > 1) {                                  /* in several chains: a neighbour's bank decides */
            int prev = i ? mz[i - 1].bank : 0, nxtb = -1;
            if (i + 1 < nw) { int c = 0, b = -1; for (k = 0; k < 4; k++) for (j = 0; j < nchain[k]; j++) if (chain_w[k][j] == windows[i + 1]) { c++; b = k; } if (c == 1) nxtb = b; }
            if (prev >= 0 && in[prev]) { for (k = 0; k < 4; k++) in[k] = k == prev; nb = 1; }
            else if (nxtb >= 0 && in[nxtb]) { for (k = 0; k < 4; k++) in[k] = k == nxtb; nb = 1; }
        }
        if (nb == 1) for (k = 0; k < 4; k++) if (in[k]) bank = k;
        mz[i].index = i; mz[i].window = windows[i]; mz[i].bank = bank;
        if (bank >= 0) {
            for (j = 0; j < nchain[bank]; j++) if (chain_w[bank][j] == windows[i]) { mz[i].calls = chain[bank][j].calls; mz[i].length = chain[bank][j].length; memcpy(mz[i].header, chain[bank][j].header, 11); }
            g2_setup_map(windows[i], bank, mz[i].map);
        }
    }
    *out = mz;
    return nw;
}
/* the same as import_gauntlet2.py's arcade_g2.json (test: compare the two) */
int data_g2_mazes_json(const launch_roms *r, const char *path)
{
    g2_maze *mz; int n = data_g2_mazes(r, &mz), i, k; FILE *f;
    if (n <= 0 || !(f = fopen(path, "w"))) return 0;
    fprintf(f, "{\"mazes\": [");
    for (i = 0; i < n; i++) {
        fprintf(f, "%s{\"index\": %d, \"level\": %d, \"window\": %u, \"bank\": ", i ? ", " : "", i, i + 1, mz[i].window);
        if (mz[i].bank < 0) { fprintf(f, "null}"); continue; }
        fprintf(f, "%d, \"calls\": %d, \"length\": %d, \"header\": [", mz[i].bank, mz[i].calls, mz[i].length);
        for (k = 0; k < 11; k++) fprintf(f, "%s%d", k ? ", " : "", mz[i].header[k]);
        fprintf(f, "], \"map\": [");
        for (k = 0; k < 1024; k++) fprintf(f, "%s%d", k ? ", " : "", mz[i].map[k]);
        fprintf(f, "]}");
    }
    fprintf(f, "]}\n"); fclose(f); free(mz);
    return n;
}
