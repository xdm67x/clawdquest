// ---------------------------------------------------------------------------
// The stage: the OpenGL arena around Clawd — the 3D pixel-art voxel boss,
// starfield, floor rings and hit sparks. Port of the original boss.js.
//
// Clawd has one pixel-map variant per game state (doze/wake/idle/work/happy/
// dizzy); variants swap inside an animated root, with pop-in, hit squash,
// per-variant bob/jump/tilt/spin and camera shake.
// ---------------------------------------------------------------------------
#pragma once

#include <stdbool.h>

void scene_init(void);
void scene_resize(int w, int h); // logical window size, points
// Sync with game state each frame. working = a chronometer is running.
void scene_set(int foe, float hp, bool dead, bool dormant, bool working);
void scene_boss_hit(bool big); // big = heavy hit (long-worked quest)
void scene_boss_win(void);
void scene_frame(float t, float dt); // draw everything
