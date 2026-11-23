/* g4music.h -- player-supplied MP3 music (see g4music.c) */
#ifndef G4MUSIC_H
#define G4MUSIC_H
#include <stdint.h>
typedef struct { int16_t *pcm; uint64_t frames; int channels; unsigned rate; int loop; uint64_t loop_at; int gain; } music_track;
/* gain: of 256, 0 = the player-MP3 default (audio_music_gain) */
int music_decode(const char *path, music_track *t);   /* 1 = loaded; 0 = no file or not an MP3 */
int music_extracted(const char *path, int track, music_track *t);   /* from a g4_music.bin (g4extract.c); 1 = found */
int music_to_wav(const char *bin, const char *dir);   /* --music-wav: each track as a WAV */
#endif
