// ---------------------------------------------------------------------------
// Clawd the desktop pet: the animated voxel character in a transparent,
// borderless, always-on-top window. One pixel-map variant per state
// (idle/blink/work/doze/wake/happy/dizzy); variants swap inside an animated
// root with pop-in, hit squash, per-variant bob/jump/tilt/spin, hit sparks,
// a soft floor shadow and floating zZz while dozing.
// ---------------------------------------------------------------------------
#pragma once

#include <stdbool.h>

void pet_init(void);
void pet_resize(int w, int h); // logical window size, points
// Steady state, synced each frame. dead = board cleared; dormant = no quests.
void pet_set(bool dead, bool dormant, bool working);
void pet_hit(bool big);  // reaction to a completed quest (big = heavy quest)
void pet_win(void);      // board-cleared celebration
void pet_click(void);    // a pat on the head (quick click, not a drag)
void pet_drag(bool on);  // the window is being dragged around the desktop
void pet_interact(void); // any user interaction (wakes him, resets doze)
void pet_frame(float t, float dt); // draw character, shadow, sparks, zZz
