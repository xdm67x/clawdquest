// ---------------------------------------------------------------------------
// Quest Log — C port entry point.
//
// Wires SDL3 (window, events, vsync, audio) to the GL 3.2 renderer and the
// game loop: a 1-second chronometer heartbeat while a quest runs, then the
// 3D scene and the 2D HUD each frame.
// ---------------------------------------------------------------------------

#include <SDL3/SDL.h>
#include "glutil.h"

#include "app.h"
#include "audio.h"
#include "font.h"
#include "game.h"
#include "scene.h"
#include "ui.h"

int app_w = 1280, app_h = 800;

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

    SDL_Window *win = SDL_CreateWindow("Quest Log", 1280, 800,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                       SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!win) {
        SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
        return 1;
    }
    if (!SDL_GL_CreateContext(win)) {
        SDL_Log("SDL_GL_CreateContext: %s", SDL_GetError());
        return 1;
    }
    SDL_GL_SetSwapInterval(1); // vsync

    game_init(); // must finish before the first frame (loads the save)
    font_init();
    scene_init();
    ui_init();
    audio_init();

    // Text input is always on: the capture bar owns the keyboard.
    SDL_StartTextInput(win);

    int pw = 1280, ph = 800;
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    glViewport(0, 0, pw, ph);
    SDL_GetWindowSize(win, &app_w, &app_h);
    scene_resize(app_w, app_h);

    Uint64 last = SDL_GetTicksNS();
    float tick_acc = 0;
    bool done = false;

    while (!done) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_EVENT_QUIT:
                    done = true;
                    break;
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    glViewport(0, 0, e.window.data1, e.window.data2);
                    break;
                case SDL_EVENT_WINDOW_RESIZED:
                    SDL_GetWindowSize(win, &app_w, &app_h);
                    scene_resize(app_w, app_h);
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    ui_motion(e.motion.x, e.motion.y);
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (e.button.button == SDL_BUTTON_LEFT)
                        ui_click(e.button.x, e.button.y);
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (e.key.key == SDLK_ESCAPE) done = true;
                    else ui_key(e.key.key);
                    break;
                case SDL_EVENT_TEXT_INPUT:
                    ui_text(e.text.text);
                    break;
                default:
                    break;
            }
        }

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

        glClearColor(0.043f, 0.031f, 0.086f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        scene_frame(t, dt);
        ui_frame(t, dt);

        SDL_GL_SwapWindow(win);
    }

    // Persist the session's accumulated focus time before leaving.
    game_save();
    SDL_Quit();
    return 0;
}
