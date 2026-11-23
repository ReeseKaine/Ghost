/* g2sfx.c -- the Gauntlet II sound effects for the launcher's "Build the data files" (user, 2026-11-22):
   the arcade sound board run from the player's own gauntlet2.zip -- its 6502 program (136043-1120.16r
   at $4000, 136043-1119.16s at $8000) on the port's own 6502 (cpu6502.h), the YM2151 through ymfm
   (host/ymfm, BSD 3-Clause, via ym2151_wrap.cpp) and a POKEY written here -- and each sound-test number
   recorded until it falls silent. Output g2_sfx.bin: "G2FX", u16 count, u16 sample rate, then per effect
   number 0..count-1 (u32 offset, u32 samples), then the samples (signed 16-bit, little-endian, mono).
   An effect that makes no sound has 0 samples. Nothing from the game is in this file.

   The board (our reading of its program, matching the usual Atari layout): RAM $0000-$0FFF; $1000 reply
   to the main CPU; $1010 the command (an NMI tells the 6502 one is there); $1020 mixer (bits 0-2 YM2151
   volume, 3-4 POKEY, 5-7 speech); $1030 status on read (bit 7 = no reply waiting, bit 4 = self-test off),
   control lines on write; $1800-$180F POKEY; $1810/$1811 YM2151 register/data; $1820 speech chip;
   $1830 acknowledges the timer IRQ (32V: 60 x 262 / 64 = about 245.6 a second), which runs the sound
   engine. CPU 14.318181 MHz / 8, YM2151 / 4 (one sample per 32 CPU cycles), POKEY / 8. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "g4data.h"

void *ym2151_new(void); void ym2151_free(void *p); void ym2151_write(void *p, int port, int data);
int ym2151_status(void *p); void ym2151_sample(void *p, int *left, int *right);

static uint8_t mem[0x10000];
static uint8_t cmd; static int mix = 0x1F;
static void *ym;
static long cycles;

/* ---------------------------------------------------------------- POKEY (ours) */
static struct {
    uint8_t audf[4], audc[4], audctl, skctl;
    int div[4], out[4], hp[4];
    uint32_t p4, p5, p9, p17;                          /* polynomial counters (shift registers) */
    int b4, b5, b9, b17, base_count;
} pk;
static void pokey_reset(void)
{
    memset(&pk, 0, sizeof pk);
    pk.p4 = 1; pk.p5 = 1; pk.p9 = 1; pk.p17 = 1; pk.skctl = 3;
}
static void pokey_write(int r, uint8_t v)
{
    r &= 15;
    if (r < 8) { if (r & 1) pk.audc[r >> 1] = v; else pk.audf[r >> 1] = v; }
    else if (r == 8) pk.audctl = v;
    else if (r == 9) { int k; for (k = 0; k < 4; k++) pk.div[k] = 0; }   /* STIMER */
    else if (r == 15) pk.skctl = v;
}
static int pokey_period(int k)                         /* divider length in CPU cycles' units of its clock */
{
    int fast = (k == 0 && (pk.audctl & 0x40)) || (k == 2 && (pk.audctl & 0x20));
    int joined = (k == 1 && (pk.audctl & 0x10)) || (k == 3 && (pk.audctl & 0x08));
    if (joined) {
        int lo = k - 1, f = pk.audf[lo] | pk.audf[k] << 8;
        int lofast = (lo == 0 && (pk.audctl & 0x40)) || (lo == 2 && (pk.audctl & 0x20));
        return lofast ? f + 7 : f + 1;
    }
    return fast ? pk.audf[k] + 4 : pk.audf[k] + 1;
}
static void pokey_channel_tick(int k)
{
    uint8_t c = pk.audc[k];
    if (!(c & 0x80) && !pk.b5) return;                 /* 5-bit poly gates the change */
    if (c & 0x20) pk.out[k] ^= 1;                      /* pure tone */
    else pk.out[k] = (c & 0x40) ? pk.b4 : ((pk.audctl & 0x80) ? pk.b9 : pk.b17);
    if (k == 2 && (pk.audctl & 0x04)) pk.hp[0] = pk.out[0];   /* high-pass: ch 3 samples ch 1 */
    if (k == 3 && (pk.audctl & 0x02)) pk.hp[1] = pk.out[1];
}
static void pokey_cycle(void)                          /* one 1.79 MHz cycle */
{
    int k, base_tick;
    pk.p4 = ((pk.p4 >> 1) | ((((pk.p4 >> 0) ^ (pk.p4 >> 1)) & 1) << 3)) & 0xF;  pk.b4 = pk.p4 & 1;
    pk.p5 = ((pk.p5 >> 1) | ((((pk.p5 >> 0) ^ (pk.p5 >> 2)) & 1) << 4)) & 0x1F; pk.b5 = pk.p5 & 1;
    pk.p9 = ((pk.p9 >> 1) | ((((pk.p9 >> 0) ^ (pk.p9 >> 4)) & 1) << 8)) & 0x1FF; pk.b9 = pk.p9 & 1;
    pk.p17 = ((pk.p17 >> 1) | ((((pk.p17 >> 0) ^ (pk.p17 >> 5)) & 1) << 16)) & 0x1FFFF; pk.b17 = pk.p17 & 1;
    if (++pk.base_count >= ((pk.audctl & 1) ? 114 : 28)) { pk.base_count = 0; base_tick = 1; } else base_tick = 0;
    for (k = 0; k < 4; k++) {
        int fast = (k == 0 && (pk.audctl & 0x40)) || (k == 2 && (pk.audctl & 0x20));
        int joined_hi = (k == 1 && (pk.audctl & 0x10)) || (k == 3 && (pk.audctl & 0x08));
        int joined_lo = (k == 0 && (pk.audctl & 0x10)) || (k == 2 && (pk.audctl & 0x08));
        int clk = joined_hi ? (((k == 1 && (pk.audctl & 0x40)) || (k == 3 && (pk.audctl & 0x20))) ? 1 : base_tick) : (fast ? 1 : base_tick);
        if (joined_lo || !clk) continue;
        if (++pk.div[k] >= pokey_period(k)) { pk.div[k] = 0; pokey_channel_tick(k); }
    }
}
static int pokey_level(void)                           /* 0..60 */
{
    int k, s = 0;
    for (k = 0; k < 4; k++) {
        int vol = pk.audc[k] & 15, on;
        if ((k == 0 && (pk.audctl & 0x10)) || (k == 2 && (pk.audctl & 0x08))) continue;   /* joined: the high channel sounds */
        if (pk.audc[k] & 0x10) on = 1;                 /* volume only */
        else {
            on = pk.out[k];
            if (k == 0 && (pk.audctl & 0x04)) on ^= pk.hp[0];
            if (k == 1 && (pk.audctl & 0x02)) on ^= pk.hp[1];
        }
        if (on) s += vol;
    }
    return s;
}

/* ---------------------------------------------------------------- the board */
static uint8_t rd(uint16_t a)
{
    if (a < 0x1000 || (a >= 0x2000 && a < 0x3000)) return mem[a & 0x0FFF];
    if (a >= 0x4000) return mem[a];
    switch (a & 0xF830) {
    case 0x1010: return cmd;
    case 0x1030: return 0x80 | 0x10 | 0x08;           /* no reply waiting, self-test off, speech ready */
    case 0x1800: return 0xFF;
    case 0x1810: return (uint8_t)ym2151_status(ym);
    }
    return 0xFF;
}
static void wr(uint16_t a, uint8_t v)
{
    if (a < 0x1000 || (a >= 0x2000 && a < 0x3000)) { mem[a & 0x0FFF] = v; return; }
    if (a >= 0x4000) return;
    switch (a & 0xF830) {
    case 0x1800: pokey_write(a & 15, v); break;
    case 0x1810: ym2151_write(ym, a & 1, v); break;
    case 0x1020: mix = v; break;
    }
}
#include "cpu6502.h"

#define CPU_HZ 1789772.7
#define IRQ_CYCLES 7287                                /* 1789772.7 / 245.6 */
#define OUT_RATE 27965                                 /* YM2151 rate (55930) / 2 */

static long next_irq;
static void run_cycles(long n, int16_t *out, long *nout, long max_out)   /* also renders audio */
{
    static int ymc, half, acc;
    long end = cycles + n;
    while (cycles < end) {
        int c = CYC6502[rd(PC)], k;
        if (!step()) { PC = rd16(0xFFFC); }          /* a bad opcode: restart (should not happen) */
        for (k = 0; k < c; k++) {
            pokey_cycle();
            if (++ymc == 32) {                         /* one YM2151 sample */
                int l, r, s;
                ymc = 0;
                ym2151_sample(ym, &l, &r);
                s = (int)(((double)(l + r) / 2.0) * ((mix & 7) / 7.0) + (pokey_level() - 30) * 160.0 * (((mix >> 3) & 3) / 3.0));
                if (!half) { acc = s; half = 1; }
                else {
                    half = 0; s = (acc + s) / 2;
                    if (out && *nout < max_out) {
                        if (s > 32767) s = 32767; if (s < -32768) s = -32768;
                        out[(*nout)++] = (int16_t)s;
                    }
                }
            }
        }
        cycles += c;
        if (cycles >= next_irq) { next_irq += IRQ_CYCLES; irq6502(0); }
    }
}
static void board_boot(void)
{
    if (ym) ym2151_free(ym);
    ym = ym2151_new(); pokey_reset(); mix = 0x1F;
    memset(mem, 0, 0x4000);
    A = X = Y = 0; S = 0xFF; P = 0x24; PC = rd16(0xFFFC); cycles = 0; next_irq = IRQ_CYCLES;
    run_cycles((long)(CPU_HZ * 0.5), NULL, NULL, 0);   /* its start-up */
}

/* --sfx-wavs FILE DIR (for listening): every effect of g2_sfx.bin as DIR/sfx_NNN.wav */
int data_g2_sfx_wavs(const char *bin, const char *dir)
{
    FILE *f = fopen(bin, "rb"); long n; uint8_t *b; int cnt, rate, k, made = 0;
    if (!f) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = (uint8_t *)malloc((size_t)n);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n || n < 8 || memcmp(b, "G2FX", 4)) { fclose(f); free(b); return 0; }
    fclose(f);
    cnt = b[4] << 8 | b[5]; rate = b[6] << 8 | b[7];
    for (k = 0; k < cnt; k++) {
        const uint8_t *e = b + 8 + 8 * k; uint32_t off = (uint32_t)e[0] << 24 | (uint32_t)e[1] << 16 | (uint32_t)e[2] << 8 | e[3];
        uint32_t len = (uint32_t)e[4] << 24 | (uint32_t)e[5] << 16 | (uint32_t)e[6] << 8 | e[7], v; char path[1200]; uint8_t h[44];
        if (!len || off + 2L * len > n) continue;
        snprintf(path, sizeof path, "%s/sfx_%03d.wav", dir, k);
        if (!(f = fopen(path, "wb"))) continue;
        memcpy(h, "RIFF", 4); v = 36 + 2 * len; memcpy(h + 4, &v, 4); memcpy(h + 8, "WAVEfmt ", 8);
        v = 16; memcpy(h + 16, &v, 4); h[20] = 1; h[21] = 0; h[22] = 1; h[23] = 0;
        v = (uint32_t)rate; memcpy(h + 24, &v, 4); v = (uint32_t)rate * 2; memcpy(h + 28, &v, 4); h[32] = 2; h[33] = 0; h[34] = 16; h[35] = 0;
        memcpy(h + 36, "data", 4); v = 2 * len; memcpy(h + 40, &v, 4);
        fwrite(h, 1, 44, f); fwrite(b + off, 2, len, f); fclose(f); made++;
    }
    free(b);
    fprintf(stderr, "%d effects written to %s\n", made, dir);
    return made > 0;
}
int data_g2_sfx(const char *zip_16r, const char *zip_16s, const char *out_path, int count)
{
    uint8_t *a, *b, *file; size_t na = 0, nb = 0, total; int n, i; FILE *f;
    int16_t **clip; long *clen; long max_len = (long)(OUT_RATE * 8.0); int peak = 1;
    a = zip_read(zip_16r, "136043-1120.16r", &na); b = zip_read(zip_16s, "136043-1119.16s", &nb);
    if (!a || !b || na != 0x4000 || nb != 0x8000) { free(a); free(b); return 0; }
    memcpy(mem + 0x4000, a, na); memcpy(mem + 0x8000, b, nb); free(a); free(b);
    clip = (int16_t **)calloc((size_t)count, sizeof *clip); clen = (long *)calloc((size_t)count, sizeof *clen);
    for (n = 1; n < count; n++) {
        int16_t *buf = (int16_t *)malloc(sizeof *buf * (size_t)max_len); long got = 0, last_loud = -1, w;
        board_boot();
        cmd = (uint8_t)n; irq6502(1);
        for (w = 0; got < max_len; w++) {                /* 1/20 s at a time until it falls silent */
            long before = got, k; int loud = 0;
            run_cycles((long)(CPU_HZ / 20), buf, &got, max_len);
            {   int lo = 32767, hi = -32768;                /* "sound" = the signal moves (the POKEY rests off zero) */
                for (k = before; k < got; k++) { if (buf[k] < lo) lo = buf[k]; if (buf[k] > hi) hi = buf[k]; }
                loud = got > before && hi - lo > 600; }
            if (loud) last_loud = got;
            if (last_loud < 0 && got > OUT_RATE) break;              /* nothing in the first second */
            if (last_loud >= 0 && got - last_loud > OUT_RATE / 2) break;   /* half a second of quiet */
        }
        if (last_loud < 0) { free(buf); continue; }
        clen[n] = last_loud + OUT_RATE / 20;                 /* a short tail */
        if (clen[n] > got) clen[n] = got;
        {   long k, dc = 0; for (k = 0; k < clen[n]; k++) dc += buf[k]; dc /= clen[n] ? clen[n] : 1;   /* centre it */
            for (k = 0; k < clen[n]; k++) { int v = buf[k] - (int)dc; buf[k] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); if (abs(buf[k]) > peak) peak = abs(buf[k]); }
            for (k = 0; k < 200 && k < clen[n]; k++) buf[k] = (int16_t)(buf[k] * k / 200);   /* no click */
            for (k = 0; k < 400 && k < clen[n]; k++) buf[clen[n] - 1 - k] = (int16_t)(buf[clen[n] - 1 - k] * k / 400); }
        clip[n] = buf;
    }
    if (ym) { ym2151_free(ym); ym = NULL; }
    total = 8 + 8 * (size_t)count;
    for (n = 0; n < count; n++) total += 2 * (size_t)clen[n];
    file = (uint8_t *)malloc(total);
    memcpy(file, "G2FX", 4); file[4] = (uint8_t)(count >> 8); file[5] = (uint8_t)count; file[6] = OUT_RATE >> 8; file[7] = OUT_RATE & 0xFF;
    {   size_t pos = 8 + 8 * (size_t)count; double g = 29000.0 / peak;   /* one gain for all: their levels stay as on the board */
        for (n = 0; n < count; n++) {
            uint8_t *e = file + 8 + 8 * n; long k;
            e[0] = (uint8_t)(pos >> 24); e[1] = (uint8_t)(pos >> 16); e[2] = (uint8_t)(pos >> 8); e[3] = (uint8_t)pos;
            e[4] = (uint8_t)(clen[n] >> 24); e[5] = (uint8_t)(clen[n] >> 16); e[6] = (uint8_t)(clen[n] >> 8); e[7] = (uint8_t)clen[n];
            for (k = 0; k < clen[n]; k++) { int v = (int)lrint(clip[n][k] * g); file[pos++] = (uint8_t)v; file[pos++] = (uint8_t)(v >> 8); }
            free(clip[n]);
        }
    }
    free(clip); free(clen);
    f = fopen(out_path, "wb");
    i = f && fwrite(file, 1, total, f) == total;
    if (f) fclose(f);
    free(file);
    return i;
}
