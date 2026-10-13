// ---------------------------------------------------------------------------
// Clawd the desktop pet: a flat 2D pixel sprite in a transparent, borderless,
// always-on-top window. One pixel-map variant per state (idle/blink/work/
// doze/wake/happy/fed/eat/dizzy), animated with bob/tilt/jump/spin, a soft
// shadow, sparks, and an apple he eats whenever a quest is completed.
// ---------------------------------------------------------------------------
#pragma once

#include <stdbool.h>

void pet_init(void);
// Steady state, synced each frame. fed = all quests done; dormant = no quests.
void pet_set(bool fed, bool dormant, bool working);
void pet_feed(void);   // an apple flies in and he eats it, then a happy hop
void pet_win(void);    // all-quests-done celebration
void pet_click(void);  // a pat on the head (quick click, not a drag)
void pet_drag(bool on);// the window is being dragged around the desktop
void pet_interact(void); // any user interaction (wakes him, resets doze)
void pet_frame(float t, float dt); // draw sprite, shadow, apple, sparks, zZz
