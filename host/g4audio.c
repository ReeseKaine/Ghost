/*
 * g4audio.c -- sound for the native Gauntlet 4 host: the YM2612 (Nuked-OPN2, host/ym), an
 * SN76489 PSG, and the translated Z80 sample player (c/gen_z80) running in step with them.
 *
 * Time: NTSC master clock 53.693175 MHz, 3420 master clocks per scanline. One YM2612 output
 * sample = 24 Nuked clocks = 1008 master clocks (53267 Hz); the Z80 (master / 15) gets
 * 67.2 T-states per sample, run just before it. The host calls audio_line() once per scanline.
 *
 * YM2612 writes: the chip takes an address or data write only when it is not busy (a data
 * write keeps it busy for 32 of its clocks), and Nuked needs at least a clock between two
 * writes. The 68000 here writes in bursts with no time passing, so writes from both CPUs go
 * through one queue that drains at the chip's own pace. Each CPU's address/data pairs stay
 * together, so the two cannot split each other's pairs.
 *
 * Z80 control (68000 side): $A11100 bus request (the Z80 stops while the 68000 holds its bus),
 * $A11200 reset (low = held in reset; going high restarts the program at 0).
 */
#include <stdio.h>
#include <string.h>
#include "../c/g4_z80.h"
#include "ym/ym3438.h"
#include "g4audio.h"

#define MASTER_PER_LINE   3420
#define MASTER_PER_SAMPLE 1008
#define Z80_MASTER        15

/* ------------------------------------------------------------------ YM2612 + write queue */
static ym3438_t ym;
static struct { uint8_t port, val; } q[8192];
static unsigned q_head, q_tail;          /* entries q_head..q_tail-1 */
static int since_write = 99;             /* chip clocks since the last write */
long audio_stat[8];
int audio_debug;                      /* debug: pairs from 68k, from Z80, DAC writes, max queue, PSG writes, Z80 T-states */
static int latch_port[2], latch_addr[2], latch_valid[2];   /* per CPU: 0 = 68000, 1 = Z80 */

static void queue_put(uint8_t port, uint8_t val)
{
    if (q_tail - q_head >= sizeof q / sizeof q[0]) return;   /* overflow: drop (never seen) */
    q[q_tail % (sizeof q / sizeof q[0])].port = port;
    q[q_tail % (sizeof q / sizeof q[0])].val = val;
    q_tail++;
}

/* music extraction (g4extract.c): every sound-chip data write, stamped with the sample clock */
void (*audio_write_hook)(unsigned chip_port, unsigned addr, unsigned val);
uint64_t audio_clock;                        /* samples made since power-on */
/* an address write is remembered; the pair goes into the queue with its data write */
void audio_ym_write(int cpu, unsigned port, uint8_t v)
{
    port &= 3;
    if (!(port & 1)) { latch_port[cpu] = (int)port; latch_addr[cpu] = v; latch_valid[cpu] = 1; return; }
    if (!latch_valid[cpu]) return;
    audio_stat[cpu]++;
    if (audio_debug && cpu == 1 && latch_port[cpu] == 0 && (latch_addr[cpu] == 0x24 || latch_addr[cpu] == 0x25 || latch_addr[cpu] == 0x2B || latch_addr[cpu] == 0xB6))
        fprintf(stderr, "Z80 YM %02X = %02X\n", latch_addr[cpu], v);
    if (latch_port[cpu] == 0 && latch_addr[cpu] == 0x2A) audio_stat[2]++;
    if ((long)(q_tail - q_head) > audio_stat[3]) audio_stat[3] = (long)(q_tail - q_head);
    if (audio_write_hook) audio_write_hook((unsigned)latch_port[cpu], latch_addr[cpu], v);
    queue_put((uint8_t)latch_port[cpu], (uint8_t)latch_addr[cpu]);
    queue_put((uint8_t)(latch_port[cpu] | 1), v);
    /* the address stays latched: further data writes go to the same register (DAC streaming) */
}

static void ym_clock(int32_t *l, int32_t *r)
{
    Bit16s out[2];
    if (q_head != q_tail && since_write >= 2 && !ym.write_busy) {
        unsigned k = q_head % (sizeof q / sizeof q[0]);
        OPN2_Write(&ym, q[k].port, q[k].val);
        q_head++; since_write = 0;
    } else since_write++;
    OPN2_Clock(&ym, out);
    *l += out[0]; *r += out[1];
}

/* status: timer flags and busy. The Z80 sees the queue as busy too; the 68000 must not
   (it would wait for time that does not pass while it runs) */
uint8_t audio_ym_read68k(unsigned port) { (void)port; return (uint8_t)(OPN2_Read(&ym, 0) & 0x7F); }
static uint8_t ym_read_z80(uint16_t port)
{
    uint8_t s = OPN2_Read(&ym, port & 3);
    if (q_head != q_tail) s |= 0x80;
    return s;
}

/* ------------------------------------------------------------------ SN76489 PSG */
static struct {
    uint16_t period[4];                  /* tone 0-2: 10-bit; 3: noise control */
    uint8_t  vol[4];                     /* attenuation 0 (loud) .. 15 (off) */
    int32_t  count[4];                   /* in 1/16 of a PSG clock step, fixed point */
    uint8_t  out[4];
    uint16_t lfsr;
    int latch;                           /* register being written: channel * 2 + (1 = volume) */
} psg;
static const int psg_vol[16] = { 8191, 6506, 5168, 4105, 3261, 2590, 2057, 1634,
                                 1298, 1031, 819, 650, 517, 410, 326, 0 };

void audio_psg_write(uint8_t v)
{
    int ch;
    if (audio_write_hook) audio_write_hook(4, 0, v);
    audio_stat[4]++;
    if (v & 0x80) {
        psg.latch = (v >> 4) & 7;
        ch = psg.latch >> 1;
        if (psg.latch & 1) psg.vol[ch] = v & 15;
        else if (ch == 3) { psg.period[3] = v & 7; psg.lfsr = 0x8000; }
        else psg.period[ch] = (uint16_t)((psg.period[ch] & 0x3F0) | (v & 15));
    } else {
        ch = psg.latch >> 1;
        if (psg.latch & 1) psg.vol[ch] = v & 15;
        else if (ch == 3) { psg.period[3] = v & 7; psg.lfsr = 0x8000; }
        else psg.period[ch] = (uint16_t)((psg.period[ch] & 0x00F) | ((v & 0x3F) << 4));
    }
}

/* one output sample: the PSG clock (3.579545 MHz / 16) makes 4.2 steps per sample */
static int32_t psg_sample(void)
{
    /* counters run in master clocks: one PSG step = 240 master clocks, a tone half-wave =
       period steps */
    int ch; int32_t mix = 0;
    for (ch = 0; ch < 4; ch++) {
        int32_t per;
        if (ch < 3) per = psg.period[ch] ? psg.period[ch] : 1;
        else {
            int r = psg.period[3] & 3;
            per = r == 3 ? (psg.period[2] ? psg.period[2] : 1) : (0x10 << r);
        }
        psg.count[ch] -= MASTER_PER_SAMPLE;
        while (psg.count[ch] <= 0) {
            psg.count[ch] += per * 240;
            if (ch < 3) psg.out[ch] ^= 1;
            else {
                /* the noise shifter steps on every other toggle of its source */
                static int half;
                half ^= 1;
                if (half) {
                    uint16_t fb = (psg.period[3] & 4) ? (uint16_t)(((psg.lfsr ^ (psg.lfsr >> 3)) & 1))
                                                      : (uint16_t)(psg.lfsr & 1);
                    psg.out[3] = psg.lfsr & 1;
                    psg.lfsr = (uint16_t)((psg.lfsr >> 1) | (fb << 15));
                }
            }
        }
        if (ch < 3 && psg.period[ch] <= 1) mix += psg_vol[psg.vol[ch]];   /* period 0/1: constant level */
        else if (psg.out[ch]) mix += psg_vol[psg.vol[ch]];
    }
    return mix / 3;                      /* a PSG channel at full volume: about a third of an FM channel */
}

/* ------------------------------------------------------------------ Z80 */
static Z80 z80;
static int z80_reset = 1, z80_busreq;    /* reset asserted at power on */
static int z80_freed;                    /* the 68000 let go of the bus since the Z80 last ran */
static int32_t z80_carry;                /* master clocks owed to the Z80 */

unsigned audio_z80_pc(void) { return z80_reset ? 0xFFFFu : z80.pc; }
void audio_z80_busreq(int on) { z80_busreq = on; if (!on) z80_freed = 1; }
void audio_z80_reset(int asserted)
{
    if (asserted && !z80_reset) { memset(&z80, 0, sizeof z80); }
    if (!asserted && z80_reset) { memset(&z80, 0, sizeof z80); z80.pc = 0; z80_carry = 0; }
    z80_reset = asserted;
    if (asserted) latch_valid[1] = 0;    /* only the Z80 resets; the YM2612 keeps its state */
}

static void z80_bad(Z80 *z, uint16_t pc)
{
    static int told;
    if (!told++) fprintf(stderr, "Z80: jump to %04X, which the translation does not have; Z80 stopped\n", pc);
    z->budget = -0x40000000; z80_reset = 1;
}
static void z80_ym_write(uint16_t port, uint8_t v) { audio_ym_write(1, port, v); }
static void z80_other_write(uint16_t a, uint8_t v) { if (a == 0x7F11) audio_psg_write(v); }

static void z80_run(int32_t master)
{
    z80_carry += master;
    /* the Z80 runs in a slice in which it had its bus at some point: the 68000 often takes the
       bus for a moment and gives it back many times within one sample's time */
    if (z80_reset || (z80_busreq && !z80_freed)) { z80_carry = 0; return; }
    z80_freed = 0;
    z80.budget += z80_carry / Z80_MASTER;
    z80_carry %= Z80_MASTER;
    if (z80.budget > 0) { audio_stat[5] += z80.budget; g4z80_run(&z80); }
}

/* ------------------------------------------------------------------ output */
static int16_t buf[2 * 4096];
static int nbuf;
static int32_t line_carry;
int audio_gain = 12;                     /* YM sum of 24 clocks (at most about 6 * 4 * 256) * gain -> 16 bit */
static int32_t dc_in[2], dc_out[2];      /* DC blocker: the console's output capacitors */
int32_t audio_peak;

void audio_init(void)
{
    OPN2_SetChipType(ym3438_mode_ym2612);
    OPN2_Reset(&ym);
    psg.lfsr = 0x8000; psg.vol[0] = psg.vol[1] = psg.vol[2] = psg.vol[3] = 15;
    g4z80_io.ym_read = ym_read_z80;
    g4z80_io.ym_write = z80_ym_write;
    g4z80_io.other_write = z80_other_write;
    g4z80_io.bad = z80_bad;
}

/* A voice channel (the Gauntlet II narration, g4mods.c): signed 8-bit clips at their own rate,
   played one after another from a short queue and mixed into both speakers. */
static const int8_t *voice_q[8]; static uint32_t voice_n[8], voice_r[8];
static int voice_head, voice_count; static uint32_t voice_pos;   /* 16.16 position in the clip */
int audio_voice_gain = 110;
void audio_voice_queue(const int8_t *samples, uint32_t n, uint32_t rate)
{
    if (voice_count < 8 && samples && n) { int k = (voice_head + voice_count) & 7; voice_q[k] = samples; voice_n[k] = n; voice_r[k] = rate; voice_count++; }
}
void audio_voice_stop(void) { voice_count = 0; voice_pos = 0; }
int audio_voice_busy(void) { return voice_count; }
/* Effects channel (the Gauntlet II sound effects, g2_sfx.bin through g4mods.c): signed 16-bit clips at
   their own rate (a different rate = a different pitch), up to 8 at once, at the Effects volume. */
static struct { const int16_t *s; uint32_t n, rate, pos; } fx[8];
void audio_sfx_play(const int16_t *samples, uint32_t n, uint32_t rate)
{
    int k, pick = 0; uint32_t most = 0;
    if (!samples || !n) return;
    for (k = 0; k < 8; k++) {                          /* a free slot, else the one furthest along */
        if (!fx[k].s) { pick = k; break; }
        if ((fx[k].pos >> 16) * 256 / (fx[k].n ? fx[k].n : 1) >= most) { most = (fx[k].pos >> 16) * 256 / (fx[k].n ? fx[k].n : 1); pick = k; }
    }
    fx[pick].s = samples; fx[pick].n = n; fx[pick].rate = rate; fx[pick].pos = 0;
}
void audio_sfx_stop(void) { int k; for (k = 0; k < 8; k++) fx[k].s = NULL; }
static int32_t sfx_sample(void)
{
    int32_t v = 0; int k;
    for (k = 0; k < 8; k++) if (fx[k].s) {
        uint32_t i = fx[k].pos >> 16;
        if (i >= fx[k].n) { fx[k].s = NULL; continue; }
        v += fx[k].s[i] / 2;
        fx[k].pos += (uint32_t)(((uint64_t)fx[k].rate << 16) / AUDIO_RATE);
    }
    return v;
}
static int32_t voice_sample(void)
{
    int32_t v;
    while (voice_count && (voice_pos >> 16) >= voice_n[voice_head]) { voice_head = (voice_head + 1) & 7; voice_count--; voice_pos = 0; }
    if (!voice_count) return 0;
    v = voice_q[voice_head][voice_pos >> 16] * audio_voice_gain;
    voice_pos += (uint32_t)(((uint64_t)voice_r[voice_head] << 16) / AUDIO_RATE);
    return v;
}

/* A music channel (player-supplied MP3s, g4music.c): 16-bit PCM at its own rate, looped or once. */
static const int16_t *mus_pcm; static uint64_t mus_frames, mus_pos, mus_loop_at; static unsigned mus_rate; static int mus_ch, mus_loop, mus_gain;
static int mus_paused, mus_fade, mus_fade_len;   /* paused with the game; fade: samples left of a fade-out */
/* volumes 0-10 (launcher / pause menu): the music channel, the game's own sound chips, the voice */
int audio_vol_music = 7, audio_vol_sfx = 10, audio_vol_voice = 10;
void audio_music_pause(int on) { mus_paused = on; }
void audio_music_fade(int samples) { if (mus_pcm && !mus_fade) { mus_fade = mus_fade_len = samples > 0 ? samples : 1; } }
int audio_music_gain = 160;                  /* of 256: player MP3s */
void audio_music_play_ex(const int16_t *pcm, uint64_t frames, int channels, unsigned rate, int loop, uint64_t loop_at, int gain)
{
    mus_pcm = pcm; mus_frames = frames; mus_ch = channels; mus_rate = rate; mus_loop = loop; mus_pos = 0;
    mus_loop_at = loop_at < frames ? loop_at : 0; mus_gain = gain; mus_paused = 0; mus_fade = 0;
}
void audio_music_play(const int16_t *pcm, uint64_t frames, int channels, unsigned rate, int loop)
{
    audio_music_play_ex(pcm, frames, channels, rate, loop, 0, audio_music_gain);
}
void audio_music_stop(void) { mus_pcm = NULL; mus_paused = 0; mus_fade = 0; }
int audio_music_playing(void) { return mus_pcm != NULL; }
static void music_sample(int32_t *l, int32_t *r)       /* 32.32 position, nearest sample */
{
    uint64_t i;
    if (!mus_pcm || mus_paused) { *l = *r = 0; return; }
    i = mus_pos >> 32;
    if (i >= mus_frames) {
        if (!mus_loop) { mus_pcm = NULL; *l = *r = 0; return; }
        mus_pos -= (mus_frames - mus_loop_at) << 32; i = mus_pos >> 32;    /* back to the loop point */
        if (i >= mus_frames) { mus_pos = mus_loop_at << 32; i = mus_loop_at; }
    }
    {   int g = mus_gain * audio_vol_music / 10;
        if (mus_fade) { g = (int)((int64_t)g * mus_fade / mus_fade_len); if (--mus_fade == 0) { mus_pcm = NULL; *l = *r = 0; return; } }
        *l = mus_pcm[i * (uint64_t)mus_ch] * g >> 8;
        *r = mus_pcm[i * (uint64_t)mus_ch + (uint64_t)(mus_ch - 1)] * g >> 8; }
    mus_pos += ((uint64_t)mus_rate << 32) / AUDIO_RATE;
}

static int16_t clip(int32_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

void audio_line(void)
{
    line_carry += MASTER_PER_LINE;
    while (line_carry >= MASTER_PER_SAMPLE) {
        int32_t l = 0, r = 0, p; int k;
        line_carry -= MASTER_PER_SAMPLE;
        audio_clock++;
        z80_run(MASTER_PER_SAMPLE);
        for (k = 0; k < 24; k++) ym_clock(&l, &r);
        p = psg_sample();
        {   int32_t vs = voice_sample() * audio_vol_voice / 10, g = (voice_count ? audio_gain / 2 : audio_gain) * audio_vol_sfx / 10;   /* the game at half while narrating */
            int32_t ml, mr; music_sample(&ml, &mr);
            p = p * audio_vol_sfx / 10;
            int32_t fxs = sfx_sample() * audio_vol_sfx / 10;
            l = l * g + (voice_count ? p / 2 : p) + vs + ml + fxs; r = r * g + (voice_count ? p / 2 : p) + vs + mr + fxs; }
        /* one-pole high-pass, about 5 Hz: y = x - x1 + 0.9994 * y1 */
        dc_out[0] = l - dc_in[0] + (int32_t)(((int64_t)dc_out[0] * 65497) >> 16); dc_in[0] = l; l = dc_out[0];
        dc_out[1] = r - dc_in[1] + (int32_t)(((int64_t)dc_out[1] * 65497) >> 16); dc_in[1] = r; r = dc_out[1];
        if (l > audio_peak) audio_peak = l; if (-l > audio_peak) audio_peak = -l;
        if (nbuf < (int)(sizeof buf / sizeof buf[0]) / 2) { buf[2 * nbuf] = clip(l); buf[2 * nbuf + 1] = clip(r); nbuf++; }
    }
}

int audio_take(const int16_t **samples)
{
    int n = nbuf;
    *samples = buf; nbuf = 0;
    return n;
}
