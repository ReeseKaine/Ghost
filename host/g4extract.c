/* g4extract.c -- --extract-music FILE: record Gauntlet 4's music tracks from the player's own ROM
   (user, 2026-11-22; a launcher building block). The game runs as usual, but every request for
   music, sound effects or voices from the game itself is dropped (g4mods.c, mods_music_extract);
   this file asks the sound driver for one track at a time, the way 0xFE010 does ($FFF004 = track,
   $FFF00C = 2; $FFF00C = 1 stops), and records the sound with nothing else playing -- so no
   instrument is ever dropped for a sound effect.

   Where a track loops is found from the sound chips' register writes (audio_write_hook): once the
   last stretch of writes has repeated three times, the repeat is the loop (find_loop).
   Tracks that end (jingles) stop at the first 5 seconds of silence. Each track is kept as intro +
   one loop, compressed with QOA (mp3/qoa.h, MIT), in one file (see write_file). The game plays
   them instead of the Mega Drive music when the file is there (g4mods.c, music slots).
   Nothing here contains any of the game's music: it is all made on the player's machine. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../c/g4_m68k.h"
#include "g4audio.h"
#include "mp3/qoa.h"
#include "g4extract.h"

#define FIRST_TRACK 0x00
#define LAST_TRACK  0x13                 /* 0x14 = stop */
#define SECS(n) ((uint64_t)(n) * AUDIO_RATE)
#define MAX_LEN SECS(540)                /* give up looking for a loop after 9 minutes */
#define MIN_LOOP SECS(6)                 /* shortest loop accepted */

typedef struct { uint32_t t; uint32_t key; } ev_t;        /* time (samples into the track), port<<16|addr<<8|val */
static ev_t *ev; static size_t nev, cap_ev;
static int16_t *pcm; static uint64_t npcm, cap_pcm;
static uint64_t base;                                     /* audio_clock when the track started */
static int track = FIRST_TRACK - 1, phase, wait_frames, recording;
static long last_check;
static char out_path[1024], progress_path[1024];
static void progress(const char *what)                   /* --progress FILE: for the launcher's display */
{
    FILE *f;
    if (!progress_path[0] || !(f = fopen(progress_path, "w"))) return;
    fprintf(f, "%s\n", what); fclose(f);
}
void extract_progress_file(const char *path) { snprintf(progress_path, sizeof progress_path, "%s", path); }
typedef struct { int track, loop; uint64_t samples, loop_at; uint8_t *qoa; unsigned qoa_len; } done_t;
static done_t done[64]; static int ndone;
int extract_music_active;

static void on_write(unsigned port, unsigned addr, unsigned val)
{
    if (!recording) return;
    if (port < 4 && (addr >= 0x24 && addr <= 0x27)) return;     /* timers: written every tick */
    if (port < 4 && (addr == 0x2A || addr == 0x2B)) return;     /* DAC samples */
    if (nev == cap_ev) { cap_ev = cap_ev ? cap_ev * 2 : 65536; ev = (ev_t *)realloc(ev, cap_ev * sizeof *ev); }
    ev[nev].t = (uint32_t)(audio_clock - base); ev[nev].key = port << 16 | addr << 8 | val; nev++;
}

void extract_audio(const int16_t *s, int n)              /* the frame's samples (stereo) */
{
    if (!recording || n <= 0) return;
    if (npcm + (uint64_t)n > cap_pcm) {
        cap_pcm = cap_pcm ? cap_pcm * 2 : SECS(60);
        while (cap_pcm < npcm + (uint64_t)n) cap_pcm *= 2;
        pcm = (int16_t *)realloc(pcm, (size_t)cap_pcm * 4);
    }
    memcpy(pcm + 2 * npcm, s, (size_t)n * 4); npcm += (uint64_t)n;
}

static int loud(uint64_t from, uint64_t to)               /* any 0.1 s stretch with sound (RMS about its mean > 300)? */
{
    uint64_t w = AUDIO_RATE / 10, i, j;
    for (i = from; i < to && i < npcm; i += w) {
        double sl = 0, sr = 0, ql = 0, qr = 0, m; uint64_t e = i + w < to ? i + w : to;
        if (e > npcm) e = npcm;
        if (e <= i) break;
        for (j = i; j < e; j++) { sl += pcm[2 * j]; sr += pcm[2 * j + 1]; ql += (double)pcm[2 * j] * pcm[2 * j]; qr += (double)pcm[2 * j + 1] * pcm[2 * j + 1]; }
        m = (double)(e - i);                               /* variance: a steady offset is not sound */
        if ((ql - sl * sl / m) / m > 300.0 * 300.0 || (qr - sr * sr / m) / m > 300.0 * 300.0) return 1;
    }
    return 0;
}

/* A loop: a period p (in writes) such that the last 3p writes are three identical copies, at
   least MIN_LOOP long and with about the same length (5 %: the sound processor's timing wobbles
   a little, the game holds it while it talks to it). Short phrases repeat too, so of all such p
   the one whose repeating reaches back furthest wins: the real loop repeats right back to the end
   of the intro. Silent repeats (the driver's idle writes) do not count. */
static int find_loop(uint64_t *loop_at, uint64_t *loop_len)
{
    size_t p, k, n = nev, s, best_s = (size_t)-1, best_p = 0;
    for (p = 16; 3 * p <= n; p++) {
        uint32_t d1 = ev[n - 1].t - ev[n - 1 - p].t, d2 = ev[n - 1 - p].t - ev[n - 1 - 2 * p].t;
        if (d1 < MIN_LOOP) continue;
        if ((uint64_t)d1 * 20 > (uint64_t)d2 * 21 || (uint64_t)d2 * 20 > (uint64_t)d1 * 21) continue;
        for (k = 0; k < 2 * p; k++)
            if (ev[n - 1 - k].key != ev[n - 1 - k - p].key) break;
        if (k < 2 * p) continue;
        s = n - 1 - 2 * p;                                 /* walk back to where the repeating starts */
        while (s > 0 && ev[s - 1].key == ev[s - 1 + p].key) s--;
        if (s < best_s) { best_s = s; best_p = p; }
    }
    if (!best_p) return 0;
    *loop_at = ev[best_s].t; *loop_len = ev[best_s + best_p].t - ev[best_s].t;
    if (!loud(*loop_at, *loop_at + *loop_len)) return 0;  /* idle ticking after the music ended */
    return 1;
}

static void keep(int loop, uint64_t samples, uint64_t loop_at)
{
    qoa_desc q; void *enc; unsigned len = 0; done_t *d;
    if (samples > npcm) samples = npcm;
    if (samples < SECS(1) / 2 || ndone >= 64) return;
    memset(&q, 0, sizeof q); q.channels = 2; q.samplerate = AUDIO_RATE; q.samples = (unsigned)samples;
    enc = qoa_encode(pcm, &q, &len);
    if (!enc) { fprintf(stderr, "extract: track %02X: QOA encoding failed\n", track); return; }
    d = &done[ndone++]; d->track = track; d->loop = loop; d->samples = samples; d->loop_at = loop_at;
    d->qoa = (uint8_t *)enc; d->qoa_len = len;
    fprintf(stderr, "extract: track %02X: %.1f s%s", track, (double)samples / AUDIO_RATE, loop ? "" : ", plays once\n");
    if (loop) fprintf(stderr, ", loops from %.2f s (loop %.1f s)\n", (double)loop_at / AUDIO_RATE, (double)(samples - loop_at) / AUDIO_RATE);
}

static void put32(FILE *f, uint32_t v) { uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; fwrite(b, 1, 4, f); }
static void put16(FILE *f, unsigned v) { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; fwrite(b, 1, 2, f); }
/* "G4MU" file (little-endian): "G4MU", u16 version 1, u16 count, then per track 20 bytes
   (u8 track number, u8 flags: bit 0 loops, u16 0, u32 offset of its QOA data from the file start,
   u32 QOA byte length, u32 samples (stereo frames at the QOA rate), u32 loop start (in samples)),
   then the QOA files one after another. */
static void write_file(void)
{
    FILE *f = fopen(out_path, "wb"); int k; uint32_t off = 8 + 20u * (uint32_t)ndone;
    if (!f) { perror(out_path); progress("fail"); exit(1); }
    fwrite("G4MU", 1, 4, f); put16(f, 1); put16(f, (unsigned)ndone);
    for (k = 0; k < ndone; k++) {
        uint8_t b[4] = { (uint8_t)done[k].track, (uint8_t)(done[k].loop ? 1 : 0), 0, 0 }; fwrite(b, 1, 4, f);
        put32(f, off); put32(f, done[k].qoa_len); put32(f, (uint32_t)done[k].samples); put32(f, (uint32_t)done[k].loop_at);
        off += done[k].qoa_len;
    }
    for (k = 0; k < ndone; k++) fwrite(done[k].qoa, 1, done[k].qoa_len, f);
    fclose(f);
    {   char t[32]; snprintf(t, sizeof t, "done %d", ndone); progress(t); }
    fprintf(stderr, "extract: %d tracks written to %s (%u bytes)\n", ndone, out_path, off);
}

void extract_music_start(const char *path)
{
    snprintf(out_path, sizeof out_path, "%s", path);
    extract_music_active = 1;
    audio_write_hook = on_write;
}

/* once per frame, after the frame's sound was handed out; 1 = all done */
int extract_frame(long frame_no)
{
    static int one = -2; static long one_secs;
    if (one == -2) {                                       /* test aid: G4EXTRACTONE=XX G4EXTRACTSECS=n records only
                                                              track XX for n seconds and dumps its write times */
        one = getenv("G4EXTRACTONE") ? (int)strtol(getenv("G4EXTRACTONE"), NULL, 16) : -1;
        one_secs = getenv("G4EXTRACTSECS") ? atol(getenv("G4EXTRACTSECS")) : 400;
        if (one >= 0) track = one - 1;
    }
    if (one >= 0 && phase == 2 && npcm >= SECS(one_secs)) {
        FILE *f = fopen(out_path, "w"); size_t k; uint64_t i;
        for (k = 0; k < nev; k++) fprintf(f, "e %u %06X\n", ev[k].t, ev[k].key);
        for (i = 0; i + AUDIO_RATE / 4 <= npcm; i += AUDIO_RATE / 4) {   /* loudness every 0.25 s */
            double sq = 0; uint64_t j;
            for (j = i; j < i + AUDIO_RATE / 4; j++) sq += (double)pcm[2 * j] * pcm[2 * j];
            fprintf(f, "r %.2f %.0f\n", (double)i / AUDIO_RATE, sq / (AUDIO_RATE / 4));   /* mean square */
        }
        fclose(f); return 1;
    }
    if (frame_no < 400) return 0;                          /* let the game boot (sound driver loaded) */
    wr8(0xFFF011, 0);                                      /* music not muted ($FFF011 = not $FFE82A; the
                                                              attract loop mutes it for its demo) */
    if (phase == 0) {                                      /* stop, let the last notes die away */
        recording = 0;
        if (++track > LAST_TRACK) { write_file(); return 1; }
        {   char t[32]; snprintf(t, sizeof t, "track %d %d", track - FIRST_TRACK + 1, LAST_TRACK - FIRST_TRACK + 1); progress(t); }
        wr8(0xFFF00C, 1);
        wait_frames = 120; phase = 1;
        return 0;
    }
    if (phase == 1) {
        if (--wait_frames > 0) return 0;
        nev = 0; npcm = 0; base = audio_clock; last_check = frame_no; recording = 1;
        wr8(0xFFF004, (uint8_t)track); wr8(0xFFF00C, 2);   /* as 0xFE010: the track, then "play" */
        phase = 2;
        return 0;
    }
    if (frame_no - last_check < 60 || one >= 0) return 0;  /* check once a second */
    last_check = frame_no;
    if (npcm >= SECS(6) && !loud(0, npcm)) {               /* nothing at all: no such track */
        fprintf(stderr, "extract: track %02X: silent, skipped\n", track);
        phase = 0; return 0;
    }
    if (npcm >= SECS(6) && !loud(npcm - SECS(5), npcm)) {
        uint64_t end = npcm - SECS(5);                     /* it ended: keep up to the last sound + 0.5 s */
        while (end >= AUDIO_RATE / 10 && !loud(end - AUDIO_RATE / 10, end)) end -= AUDIO_RATE / 10;
        keep(0, end + SECS(1) / 2, 0);
        phase = 0; return 0;
    }
    {   uint64_t at, len;
        if (nev >= 48 && find_loop(&at, &len) && at + len <= npcm) { keep(1, at + len, at); phase = 0; return 0; }
    }
    if (npcm >= MAX_LEN) {
        fprintf(stderr, "extract: track %02X: no loop found in %d s, kept whole\n", track, (int)(MAX_LEN / AUDIO_RATE));
        keep(1, npcm, 0); phase = 0;
    }
    return 0;
}
