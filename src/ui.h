// ---------------------------------------------------------------------------
// The quest card: a compact panel that opens above Clawd — level and XP,
// streak stats, the quest list with per-quest chronometers, and an add bar.
// Pops, toasts and confetti are anchored to the pet.
// ---------------------------------------------------------------------------
#pragma once

#include <SDL3/SDL.h>

void ui_init(void);
void ui_frame(float t, float dt); // draws the card (the pet is drawn first)
void ui_mouse_down(float x, float y);
void ui_mouse_up(float x, float y); // click lands here unless it was a drag
void ui_motion(float x, float y);   // hover + list drag
void ui_wheel(float dy);            // scroll the quest list (when over it)
void ui_text(const char *utf8);     // SDL_EVENT_TEXT_INPUT
void ui_key(SDL_Keycode key);       // Enter/Backspace/etc. while typing
void ui_pop(const char *text, float x, float y);
void ui_toast(const char *text);
void ui_confetti(int n);

void ui_set_panel(bool open);        // expand/collapse the quest card
bool ui_panel_open(void);
bool ui_on_card(float x, float y);   // hit-test the card (false when closed)
bool ui_should_quit(void);          // the Quit button was pressed
