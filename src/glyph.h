#ifndef GLYPH_H
#define GLYPH_H

#include <stdint.h>

#define FONT_HEIGHT 6
#define FONT_WIDTH 3
#define FONT_NBITS 18

void
write_glyph(uint8_t* data, int img_width, char ch, int x, int y,
             uint8_t r, uint8_t g, uint8_t b);
uint32_t
read_glyph(uint8_t* data, int img_width, int x, int y);

void
write_text(uint8_t* data, int img_width, char* text, int x, int y,
            uint8_t r, uint8_t g, uint8_t b);
char*
read_text(uint8_t* data, int img_width, int img_height, int x, int y);

extern const uint32_t font_data[128];

#endif
