#define _POSIX_C_SOURCE 200809L
#include "audio.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "stb_image_write.h"

#define AUDIO_WIDTH 256
#define AUDIO_RATE 8000
#define MAX_AUDIO_SAMPLES (AUDIO_RATE * 600)

int
is_audio(char* path)
{
  return has_ext(path, ".mp3") || has_ext(path, ".wav") ||
         has_ext(path, ".ogg") || has_ext(path, ".flac") ||
         has_ext(path, ".aac") || has_ext(path, ".opus");
}

Cell*
cell_read_audio(char* path)
{
  char resolved[512];
  if (!resolve_path(path, resolved, sizeof(resolved))) {
    return NULL;
  }

  char cmd[1024];
  snprintf(cmd, sizeof(cmd),
           "ffmpeg -i %s -f s16le -ar %d -ac 1 pipe:1 2>/dev/null",
           resolved, AUDIO_RATE);
  FILE* fp = popen(cmd, "r");
  if (!fp) {
    return NULL;
  }

  int16_t* pcm = malloc(MAX_AUDIO_SAMPLES * sizeof(int16_t));
  int nsamples = 0;
  int16_t sample;
  while (nsamples < MAX_AUDIO_SAMPLES &&
         fread(&sample, sizeof(int16_t), 1, fp) == 1) {
    pcm[nsamples++] = sample;
  }
  pclose(fp);

  if (nsamples == 0) {
    free(pcm);
    return NULL;
  }

  int h = (nsamples + AUDIO_WIDTH - 1) / AUDIO_WIDTH;
  uint8_t* pixels = calloc(AUDIO_WIDTH * h * 3, 1);

  for (int i = 0; i < nsamples; i++) {
    uint16_t u = (uint16_t)pcm[i];
    int idx = i * 3;
    pixels[idx] = (u >> 8) & 0xFF;
    pixels[idx + 1] = u & 0xFF;
    pixels[idx + 2] = 0;
  }
  free(pcm);

  char out[256];
  char* dot = strrchr(path, '.');
  if (dot) {
    int base = dot - path;
    snprintf(out, sizeof(out), "%.*s_pcm.png", base, path);
  } else {
    snprintf(out, sizeof(out), "%s_pcm.png", path);
  }

  char out_path[512];
  snprintf(out_path, sizeof(out_path), "images/%s", out);
  mkdir("images", 0755);
  stbi_write_png(out_path, AUDIO_WIDTH, h, 3, pixels, AUDIO_WIDTH * 3);
  free(pixels);

  return cell_read_image(out);
}

int
cell_write_pcm(Cell* cell, char* out_path)
{
  if (!cell || cell->type != VAL_IMAGE || !cell->img_data) {
    return -1;
  }

  int nsamples = cell->img_width * cell->img_height;
  FILE* fp = fopen(out_path, "wb");
  if (!fp) {
    return -1;
  }

  for (int i = 0; i < nsamples; i++) {
    int idx = i * 3;
    uint16_t u = ((uint16_t)cell->img_data[idx] << 8) | cell->img_data[idx + 1];
    int16_t sample = (int16_t)u;
    fwrite(&sample, sizeof(int16_t), 1, fp);
  }
  fclose(fp);
  return 0;
}
