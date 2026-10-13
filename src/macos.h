// macOS window tweaks for the desktop pet.
#pragma once

struct SDL_Window;

// Stop AppKit from drawing its drop shadow around the borderless window —
// it reads as a warm rectangle around the character on the desktop (the
// pet draws its own soft shadow under the feet instead).
void mac_window_disable_shadow(struct SDL_Window *win);
