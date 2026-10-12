// ---------------------------------------------------------------------------
// Text rendering from the embedded atlas (src/font_atlas.h, built by mkfont).
// One texture, one small shader; quads are streamed through a dynamic VBO.
// ---------------------------------------------------------------------------
#pragma once

#include "math.h"

void font_init(void);
// Draw at baseline y; size is nominal text height in pixels (CSS-like).
float font_draw(const char *utf8, float x, float y, float size, Vec4 color, int bold);
float font_measure(const char *utf8, float size);
