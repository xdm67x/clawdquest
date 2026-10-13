// ---------------------------------------------------------------------------
// Clawd Pet — entry point.
//
// A transparent, borderless, always-on-top window: Clawd lives on the
// desktop. Drag him anywhere (the window follows the cursor); click him to
// open the quest card above him (stats, quest list, add bar, quit). While a
// chronometer runs he works; left alone he dozes off after a while.
// ---------------------------------------------------------------------------

#include <SDL3/SDL.h>
#include <ctype.h>

#include "app.h"
#include "audio.h"
#include "font.h"
#include "game.h"
#include "glutil.h"
#include "pet.h"
#include "ui.h"

int app_w = PET_W, app_h = PET_H;
int app_pw = PET_W, app_ph = PET_H;

static SDL_Window *win;
static bool panel;

// Debug aid (CLAWDQUEST_DUMP=1): coarse ASCII map of the just-rendered frame
// (alpha at every 8th pixel) — a headless sanity check that Clawd is framed
// inside the window and the rest stays transparent.
static void dump_frame(int pw, int ph) {
    unsigned char *px = SDL_malloc((size_t)pw * (size_t)ph * 4);
    if (!px) return;
    glReadPixels(0, 0, pw, ph, GL_RGBA, GL_UNSIGNED_BYTE, px);
    // The exact outer border (1px) of the framebuffer, raw.
    {
        char line[2048];
        int n = 0;
        unsigned char *p;
        n += SDL_snprintf(line + n, sizeof line - n, "border");
        for (int x = 0; x < pw; x++) { // bottom row
            p = px + ((size_t)x) * 4;
            if (p[3] > 0) n += SDL_snprintf(line + n, sizeof line - n, " B%d:%02x%02x%02x%02x", x, p[0], p[1], p[2], p[3]);
        }
        for (int x = 0; x < pw; x++) { // top row
            p = px + ((size_t)(ph - 1) * pw + x) * 4;
            if (p[3] > 0) n += SDL_snprintf(line + n, sizeof line - n, " T%d:%02x%02x%02x%02x", x, p[0], p[1], p[2], p[3]);
        }
        for (int y = 0; y < ph; y++) { // left column
            p = px + ((size_t)y * pw) * 4;
            if (p[3] > 0) n += SDL_snprintf(line + n, sizeof line - n, " L%d:%02x%02x%02x%02x", y, p[0], p[1], p[2], p[3]);
        }
        for (int y = 0; y < ph; y++) { // right column
            p = px + ((size_t)(y * pw + pw - 1)) * 4;
            if (p[3] > 0) n += SDL_snprintf(line + n, sizeof line - n, " R%d:%02x%02x%02x%02x", y, p[0], p[1], p[2], p[3]);
        }
        SDL_Log("%s", line);
    }
    float scale_x = (float)pw / (float)app_w;
    float scale_y = (float)ph / (float)app_h;
    float step = 2.0f;
    for (float y = 2; y < app_h; y += step) {
        char line[512];
        int n = 0;
        for (float x = 2; x < app_w; x += step) {
            int bx = (int)(x * scale_x), by = (int)((app_h - y) * scale_y);
            unsigned char *p = px + ((size_t)by * pw + bx) * 4;
            char ch = '.';
            if (p[3] >= 32) {
                if (p[0] > 180 && p[1] > 80 && p[1] < 170) ch = 'o'; // orange body
                else if (p[0] > 200 && p[1] < 130 && p[2] < 160) ch = 'A'; // apple red
                else if (p[0] < 80 && p[1] < 80) ch = 'k'; // dark (eyes/mouth)
                else if (p[0] > 200 && p[1] > 200) ch = 'w'; // white
                else ch = '?';
                if (p[3] < 200) ch = tolower(ch);
            }
            line[n++] = ch;
        }
        line[n] = 0;
        SDL_Log("y=%3.0f %s", y, line);
    }
    SDL_free(px);
}

// --- Interaction state -------------------------------------------------------
static bool pressing;        // mouse down on Clawd (outside the card)
static bool dragging;        // the press moved: the window follows the cursor
static bool drag_moved;
static float grab_x, grab_y; // cursor position - window position, at grab
static float press_gx, press_gy;
static Uint64 press_t;

// --- Window placement --------------------------------------------------------

static void pos_path(char *out, int n) {
    const char *home = SDL_getenv("HOME");
    if (home)
        SDL_snprintf(out, (size_t)n, "%s/.clawdquest/pet.pos", home);
    else
        out[0] = 0;
}

static void pos_save(void) {
    char path[512];
    pos_path(path, sizeof path);
    if (!path[0]) return;
    int x = 0, y = 0;
    SDL_GetWindowPosition(win, &x, &y);
    char dir[512];
    SDL_snprintf(dir, sizeof dir, "%s", path);
    char *slash = SDL_strrchr(dir, '/');
    if (slash) {
        *slash = 0;
        SDL_CreateDirectory(dir);
    }
    SDL_IOStream *io = SDL_IOFromFile(path, "w");
    if (!io) return;
    char buf[64];
    SDL_snprintf(buf, sizeof buf, "%d %d\n", x, y);
    SDL_WriteIO(io, buf, SDL_strlen(buf));
    SDL_CloseIO(io);
}

// Keep the pet on a real display (points are global desktop coordinates).
static void pos_clamp(int *x, int *y) {
    SDL_Point p = { *x + PET_W / 2, *y + PET_H / 2 };
    SDL_DisplayID d = SDL_GetDisplayForPoint(&p);
    if (!d) d = SDL_GetPrimaryDisplay();
    SDL_Rect ub;
    if (!SDL_GetDisplayUsableBounds(d, &ub)) return;
    if (*x < ub.x) *x = ub.x;
    if (*y < ub.y) *y = ub.y;
    if (*x > ub.x + ub.w - PET_W) *x = ub.x + ub.w - PET_W;
    if (*y > ub.y + ub.h - PET_H) *y = ub.y + ub.h - PET_H;
}

static bool pos_load(int *x, int *y) {
    char path[512];
    pos_path(path, sizeof path);
    if (!path[0]) return false;
    SDL_IOStream *io = SDL_IOFromFile(path, "r");
    if (!io) return false;
    char buf[64] = { 0 };
    size_t sz = 0;
    char *data = (char *)SDL_LoadFile_IO(io, &sz, true);
    if (data) {
        SDL_strlcpy(buf, data, sizeof buf);
        SDL_free(data);
    }
    if (SDL_sscanf(buf, "%d %d", x, y) == 2) {
        pos_clamp(x, y);
        return true;
    }
    return false;
}

// --- Panel toggle ------------------------------------------------------------

static int save_pending; // frames until the placement is persisted

static void set_panel(bool open) {
    if (panel == open) return;
    panel = open;
    ui_set_panel(open);
    pet_interact();

    int w = open ? PANEL_W : PET_W, h = open ? PANEL_H : PET_H;
    int x = 0, y = 0, cw = 0, ch = 0;
    SDL_GetWindowPosition(win, &x, &y);
    SDL_GetWindowSize(win, &cw, &ch);
    // Grow upward around Clawd: he stays bottom-center of the window, so the
    // feet stay fixed on screen.
    SDL_SetWindowSize(win, w, h);
    SDL_SetWindowPosition(win, x + cw / 2 - w / 2, y + ch - h);
    save_pending = 15; // window ops are async: persist once they settle
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8); // the desktop shows through

    win = SDL_CreateWindow("Clawd Pet", PET_W, PET_H,
                           SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS |
                           SDL_WINDOW_TRANSPARENT | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                           SDL_WINDOW_HIDDEN);
    if (!win) {
        SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
        return 1;
    }
    SDL_SetWindowAlwaysOnTop(win, true);

    // Restore the last placement, or park him at the bottom-right corner.
    {
        int x = 0, y = 0;
        if (pos_load(&x, &y)) {
            SDL_SetWindowPosition(win, x, y);
        } else {
            SDL_Rect ub;
            if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &ub)) {
                SDL_SetWindowPosition(win, ub.x + ub.w - PET_W - 48,
                                      ub.y + ub.h - PET_H - 48);
            }
        }
    }

    if (!SDL_GL_CreateContext(win)) {
        SDL_Log("SDL_GL_CreateContext: %s", SDL_GetError());
        return 1;
    }
    SDL_GL_SetSwapInterval(1); // vsync

    // Core profile has no default vertex array object: every draw that sets
    // vertex attributes would silently fail without one bound.
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    game_init(); // must finish before the first frame (loads the save)
    font_init();
    pet_init();
    ui_init();
    audio_init();

    SDL_ShowWindow(win);

    // Debug aid (CLAWDQUEST_PANEL=1): open the quest card at startup so the
    // panel layout can be checked headlessly (pairs with CLAWDQUEST_DUMP).
    if (SDL_getenv("CLAWDQUEST_PANEL")) set_panel(true);

    // Debug aid (CLAWDQUEST_FEED=1): feed Clawd an apple at startup.
    if (SDL_getenv("CLAWDQUEST_FEED")) pet_feed();

    // Text input is always on: the add bar owns the keyboard while the card
    // is open.
    SDL_StartTextInput(win);

    int pw = PET_W, ph = PET_H;
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    app_pw = pw;
    app_ph = ph;
    glViewport(0, 0, pw, ph);
    SDL_GetWindowSize(win, &app_w, &app_h);

    Uint64 last = SDL_GetTicksNS();
    float tick_acc = 0;
    bool done = false;
    int frames = 0;

    while (!done) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_EVENT_QUIT:
                    done = true;
                    break;
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    glViewport(0, 0, e.window.data1, e.window.data2);
                    app_pw = e.window.data1;
                    app_ph = e.window.data2;
                    break;
                case SDL_EVENT_WINDOW_RESIZED:
                    SDL_GetWindowSize(win, &app_w, &app_h);
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    ui_motion(e.motion.x, e.motion.y);
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (e.button.button != SDL_BUTTON_LEFT) break;
                    if (ui_on_card(e.button.x, e.button.y)) {
                        ui_mouse_down(e.button.x, e.button.y);
                    } else if (!pressing) {
                        pressing = true;
                        drag_moved = false;
                        float gx = 0, gy = 0;
                        SDL_GetGlobalMouseState(&gx, &gy);
                        int wx = 0, wy = 0;
                        SDL_GetWindowPosition(win, &wx, &wy);
                        grab_x = gx - wx;
                        grab_y = gy - wy;
                        press_gx = gx;
                        press_gy = gy;
                        press_t = SDL_GetTicksNS();
                        SDL_CaptureMouse(true);
                        pet_interact();
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (e.button.button != SDL_BUTTON_LEFT) break;
                    ui_mouse_up(e.button.x, e.button.y);
                    if (pressing) {
                        pressing = false;
                        SDL_CaptureMouse(false);
                        double dur = (double)(SDL_GetTicksNS() - press_t) / 1e9;
                        if (!drag_moved && dur < 0.45) {
                            // A pat on the head: toggle the quest card.
                            set_panel(!ui_panel_open());
                            pet_click();
                            sfx_pet();
                            pet_interact();
                        }
                        if (dragging) {
                            dragging = false;
                            pet_drag(false);
                            pos_save();
                        }
                    }
                    break;
                case SDL_EVENT_MOUSE_WHEEL:
                    if (panel) ui_wheel(e.wheel.y);
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (e.key.key == SDLK_ESCAPE) {
                        if (panel) set_panel(false);
                    } else if (panel) {
                        ui_key(e.key.key);
                    }
                    pet_interact();
                    break;
                case SDL_EVENT_TEXT_INPUT:
                    if (panel) ui_text(e.text.text);
                    break;
                default:
                    break;
            }
        }

        // While pressed on Clawd, follow the cursor with the window.
        if (pressing) {
            float gx = 0, gy = 0;
            Uint32 btns = SDL_GetGlobalMouseState(&gx, &gy);
            if (btns & SDL_BUTTON_LMASK) {
                float dx = gx >= press_gx ? gx - press_gx : press_gx - gx;
                float dy = gy >= press_gy ? gy - press_gy : press_gy - gy;
                if (!drag_moved && dx + dy > 6) drag_moved = true;
                if (drag_moved && !dragging) {
                    dragging = true;
                    pet_drag(true);
                }
                if (dragging)
                    SDL_SetWindowPosition(win, (int)(gx - grab_x), (int)(gy - grab_y));
            } else {
                // Missed the up event (safety): treat as released.
                pressing = false;
                SDL_CaptureMouse(false);
                if (dragging) {
                    dragging = false;
                    pet_drag(false);
                    pos_save();
                }
            }
        }

        if (save_pending && --save_pending == 0) pos_save();
        if (ui_should_quit()) done = true;

        Uint64 now = SDL_GetTicksNS();
        float dt = (float)((double)(now - last) / 1e9);
        last = now;
        if (dt > 0.05f) dt = 0.05f; // clamp: no jumps after a stall
        float t = (float)((double)now / 1e9);

        // Chronometer heartbeat, one second at a time while running.
        if (running) {
            tick_acc += dt;
            while (tick_acc >= 1.0f) {
                tick_acc -= 1.0f;
                game_tick();
            }
        } else {
            tick_acc = 0;
        }

        glClearColor(0.0f, 0.0f, 0.0f, 0.0f); // transparent desktop pet
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        pet_frame(t, dt);
        ui_frame(t, dt);

        if (SDL_getenv("CLAWDQUEST_DUMP") && frames == 60) dump_frame(app_pw, app_ph);
        frames++;

        SDL_GL_SwapWindow(win);
    }

    // Persist the session's accumulated focus time and the pet's spot.
    game_save();
    pos_save();
    SDL_Quit();
    return 0;
}
