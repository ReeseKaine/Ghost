/* g4audio.h -- sound for the native host (see g4audio.c) */
#ifndef G4AUDIO_H
#define G4AUDIO_H
#include <stdint.h>
#define AUDIO_RATE 53267                 /* Hz: NTSC YM2612 output rate (master / 1008) */
void    audio_init(void);
void    audio_line(void);                /* one scanline of time: Z80, YM2612, PSG */
int     audio_take(const int16_t **samples);   /* stereo frames made since the last call */
void    audio_ym_write(int cpu, unsigned port, uint8_t v);   /* cpu 0 = 68000, 1 = Z80 */
uint8_t audio_ym_read68k(unsigned port);
void    audio_psg_write(uint8_t v);
void    audio_z80_busreq(int on);
void    audio_z80_reset(int asserted);
unsigned audio_z80_pc(void);         /* debug: where the Z80 stopped last (FFFF: in reset) */
void    audio_voice_queue(const int8_t *samples, uint32_t n, uint32_t rate);   /* play after what is queued */
void    audio_voice_stop(void);
void    audio_sfx_play(const int16_t *samples, uint32_t n, uint32_t rate);   /* an effect (up to 8 at once) */
void    audio_sfx_stop(void);
int     audio_voice_busy(void);
extern int audio_voice_gain;
void    audio_music_play(const int16_t *pcm, uint64_t frames, int channels, unsigned rate, int loop);
void    audio_music_play_ex(const int16_t *pcm, uint64_t frames, int channels, unsigned rate, int loop, uint64_t loop_at, int gain);
void    audio_music_stop(void);
extern void (*audio_write_hook)(unsigned chip_port, unsigned addr, unsigned val);   /* port 0/1 YM2612, 4 PSG */
extern uint64_t audio_clock;
int     audio_music_playing(void);
extern int audio_music_gain;
void    audio_music_pause(int on);       /* the game's pause */
void    audio_music_fade(int samples);   /* fade out over that many samples, then stop */
extern int audio_vol_music, audio_vol_sfx, audio_vol_voice;   /* 0-10 */
extern int audio_gain;
extern int32_t audio_peak;
extern long audio_stat[8];
extern int audio_debug;
#endif
