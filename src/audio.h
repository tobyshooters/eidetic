#ifndef AUDIO_H
#define AUDIO_H

#include "db.h"

int
is_audio(char* path);
Cell*
cell_read_audio(char* path);
int
cell_write_pcm(Cell* cell, char* out_path);

#endif
