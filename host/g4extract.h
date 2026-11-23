/* g4extract.h -- --extract-music (see g4extract.c) */
#ifndef G4EXTRACT_H
#define G4EXTRACT_H
#include <stdint.h>
extern int extract_music_active;
void extract_music_start(const char *path);
void extract_progress_file(const char *path);   /* --progress: "track N of", "done N", "fail" */
void extract_audio(const int16_t *s, int n);
int  extract_frame(long frame_no);
#endif
