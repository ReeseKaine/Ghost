/* g4data.h -- the launcher's data steps (see g4data.c) */
#ifndef G4DATA_H
#define G4DATA_H
#include <stddef.h>
#include <stdint.h>
uint8_t *zip_read(const char *zip, const char *name, size_t *size);   /* a member, unpacked and CRC-checked */
int data_g2_sfx(const char *zip_16r, const char *zip_16s, const char *out_path, int count);   /* g2_sfx.bin (g2sfx.c); 1 = done */
int data_g2_sfx_wavs(const char *bin, const char *dir);    /* --sfx-wavs: each effect as a .wav */
int data_speech(const char *zip_16r, const char *zip_16s, const char *out_path);   /* g2_speech.bin; 1 = done */
#endif
