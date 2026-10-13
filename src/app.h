// Shared globals: logical window size in points (the card and the pet are
// laid out in these) and the framebuffer size in pixels (for scissor
// clipping). The pet window has two fixed sizes (points):
// PET_* is Clawd alone; PANEL_* adds the quest card above him.
#pragma once

extern int app_w, app_h;
extern int app_pw, app_ph;

// Clawd is deliberately tiny: just big enough to spot on the desktop without
// pulling attention. PET_SPR is the drawn sprite size in points; the atlas is
// rasterized at 2x that (src/pet.c guards the two against drifting apart).
#define PET_SPR 64
#define PET_W 88
#define PET_H 92
#define PANEL_W 340
#define PANEL_H 404

// Clawd's framing inside the window: feet this far above the bottom edge.
#define PET_FEET_MARGIN 12
// Screen-space y of the top of Clawd's head (points, y down from 0).
#define PET_HEAD_TOP(h) ((h) - PET_FEET_MARGIN - PET_SPR)
