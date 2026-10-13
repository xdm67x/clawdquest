// Shared globals: logical window size in points (the card and the pet are
// laid out in these) and the framebuffer size in pixels (for scissor
// clipping). The pet window has two fixed sizes (points):
// PET_* is Clawd alone; PANEL_* adds the quest card above him.
#pragma once

extern int app_w, app_h;
extern int app_pw, app_ph;

#define PET_W 228
#define PET_H 184
#define PANEL_W 340
#define PANEL_H 470

// Clawd's framing inside the window: feet this far above the bottom edge,
// body this many pixels tall (kept constant in both window sizes).
#define PET_FEET_MARGIN 28
#define PET_PX 124
// Screen-space y of the top of Clawd's head (points, y down from 0).
#define PET_HEAD_TOP(h) ((h) - PET_FEET_MARGIN - PET_PX)
