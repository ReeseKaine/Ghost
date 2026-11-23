/* g4music.c -- music the player supplies as MP3 files (user, 2026-11-21): instead of emulating
   the arcade Gauntlet II sound board, the player records the tunes from their own copy and puts
   them in a "music" folder next to the level file. Decoded with dr_mp3 (mp3/dr_mp3.h, public
   domain or MIT-0) into 16-bit stereo in memory; g4audio.c plays it. Which file plays when:
   g4mods.c. */
#define DR_MP3_IMPLEMENTATION
#include "mp3/dr_mp3.h"
#define QOA_IMPLEMENTATION
#include "mp3/qoa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "g4music.h"

int music_decode(const char *path, music_track *t)
{
    drmp3_config cfg; drmp3_uint64 n = 0; drmp3_int16 *pcm;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;                                    /* no file: not an error, the slot stays silent */
    fclose(f);
    pcm = drmp3_open_file_and_read_pcm_frames_s16(path, &cfg, &n, NULL);
    if (!pcm || !n || cfg.channels < 1 || cfg.channels > 2 || !cfg.sampleRate) {
        if (pcm) drmp3_free(pcm, NULL);
        fprintf(stderr, "music: %s could not be read as an MP3\n", path);
        return 0;
    }
    t->pcm = pcm; t->frames = n; t->channels = (int)cfg.channels; t->rate = cfg.sampleRate; t->loop = 1; t->loop_at = 0; t->gain = 0;
    fprintf(stderr, "music: %s (%.1f s, %u Hz, %s)\n", path, (double)n / cfg.sampleRate, cfg.sampleRate,
            cfg.channels == 2 ? "stereo" : "mono");
    return 1;
}

/* Gauntlet 4's own tracks, recorded by --extract-music into g4_music.bin ("G4MU", see write_file()
   in g4extract.c): the file is read once, each track decoded from QOA when first played. */
static uint8_t *g4mu; static long g4mu_size; static int g4mu_tried;
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
int music_extracted(const char *path, int track, music_track *t)
{
    int k, n;
    if (!g4mu_tried) {
        FILE *f = fopen(path, "rb"); g4mu_tried = 1;
        if (f) {
            fseek(f, 0, SEEK_END); g4mu_size = ftell(f); fseek(f, 0, SEEK_SET);
            g4mu = (uint8_t *)malloc((size_t)g4mu_size);
            if (!g4mu || g4mu_size < 8 || fread(g4mu, 1, (size_t)g4mu_size, f) != (size_t)g4mu_size || memcmp(g4mu, "G4MU", 4)
                || (g4mu[4] | g4mu[5] << 8) != 1) {
                fprintf(stderr, "music: %s is not a G4MU version 1 file\n", path); free(g4mu); g4mu = NULL;
            } else fprintf(stderr, "music: %d Gauntlet 4 tracks in %s\n", g4mu[6] | g4mu[7] << 8, path);
            fclose(f);
        }
    }
    if (!g4mu || !t) return 0;
    n = g4mu[6] | g4mu[7] << 8;
    for (k = 0; k < n && 8 + 20L * (k + 1) <= g4mu_size; k++) {
        const uint8_t *e = g4mu + 8 + 20 * k; uint32_t off = le32(e + 4), len = le32(e + 8);
        qoa_desc q; short *pcm;
        if (e[0] != track) continue;
        if (off > (uint32_t)g4mu_size || len > (uint32_t)g4mu_size - off) return 0;
        pcm = qoa_decode(g4mu + off, (int)len, &q);
        if (!pcm || q.channels != 2) { free(pcm); return 0; }
        t->pcm = pcm; t->frames = q.samples; t->channels = 2; t->rate = q.samplerate;
        t->loop = e[1] & 1; t->loop_at = le32(e + 16); t->gain = 256;
        if (t->loop_at >= t->frames) t->loop_at = 0;
        return 1;
    }
    return 0;
}

/* --music-wav g4_music.bin DIR: every track as DIR/g4_XX.wav to listen to (intro + the loop twice,
   so the jump back can be heard). Made from the player's own file, on their machine. */
static void put_le(FILE *f, uint32_t v, int n) { while (n--) { fputc((int)(v & 255), f); v >>= 8; } }
int music_to_wav(const char *bin, const char *dir)
{
    int k, n, made = 0;
    music_extracted(bin, -1, NULL);                       /* loads the file (no track -1) */
    if (!g4mu) { fprintf(stderr, "music: %s not found or not a G4MU file\n", bin); return 1; }
    n = g4mu[6] | g4mu[7] << 8;
    for (k = 0; k < n; k++) {
        music_track t; char path[1200]; FILE *f; uint64_t frames;
        int id = g4mu[8 + 20 * k];
        if (!music_extracted(bin, id, &t)) continue;
        frames = t.frames + (t.loop ? t.frames - t.loop_at : 0);
        snprintf(path, sizeof path, "%s/g4_%02X.wav", dir, id);
        if (!(f = fopen(path, "wb"))) { perror(path); free(t.pcm); return 1; }
        fwrite("RIFF", 1, 4, f); put_le(f, (uint32_t)(36 + frames * 4), 4); fwrite("WAVEfmt ", 1, 8, f);
        put_le(f, 16, 4); put_le(f, 1, 2); put_le(f, 2, 2); put_le(f, t.rate, 4); put_le(f, t.rate * 4, 4); put_le(f, 4, 2); put_le(f, 16, 2);
        fwrite("data", 1, 4, f); put_le(f, (uint32_t)(frames * 4), 4);
        fwrite(t.pcm, 4, (size_t)t.frames, f);
        if (t.loop) fwrite(t.pcm + 2 * t.loop_at, 4, (size_t)(t.frames - t.loop_at), f);
        fclose(f); free(t.pcm); made++;
        printf("%s  %.1f s%s\n", path, (double)frames / t.rate, t.loop ? "  (the loop plays twice)" : "");
    }
    printf("%d tracks written to %s\n", made, dir);
    return 0;
}
