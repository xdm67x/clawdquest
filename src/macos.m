// macOS window tweaks for the desktop pet: AppKit would draw its drop
// shadow around the borderless window bounds, which shows up as a warm
// rectangle around the character on the desktop. The pet already paints its
// own soft shadow under the feet, so the window shadow is disabled.
#include "macos.h"

#ifdef __APPLE__
#import <AppKit/AppKit.h>
#import <SDL3/SDL.h>

void mac_window_disable_shadow(struct SDL_Window *win) {
    NSWindow *nsw = (__bridge NSWindow *)
        SDL_GetPointerProperty(SDL_GetWindowProperties(win),
                               SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    if (nsw) {
        [nsw setHasShadow:NO];
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "no cocoa window: shadow not disabled");
    }
}
#else
void mac_window_disable_shadow(struct SDL_Window *win) { (void)win; }
#endif
