// ---------------------------------------------------------------------------
// 2D HUD: hero panel, boss bar, quest board, capture bar, pops/toasts/confetti.
// Port of the original ui.js + fx.js; the CSS palette and layout are baked in.
// ---------------------------------------------------------------------------
#pragma once

#include <SDL3/SDL.h>

void ui_init(void);
void ui_frame(float t, float dt); // draws the HUD (3D scene is drawn first)
void ui_click(float x, float y);
void ui_motion(float x, float y); // hover tracking
void ui_text(const char *utf8);   // SDL_EVENT_TEXT_INPUT
void ui_key(SDL_Keycode key);     // Enter/Backspace/etc. while typing
void ui_pop(const char *text, float x, float y);
void ui_toast(const char *text);
void ui_confetti(int n);
