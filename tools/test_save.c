// Headless tests for game state persistence (not part of the app).
// Runs under a throwaway HOME so the real save is never touched.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "game.h"

// Stubs for modules game.c talks to.
void ui_pop(const char *t, float x, float y) { (void)t; (void)x; (void)y; }
void ui_toast(const char *t) { (void)t; }
void ui_confetti(int n) { (void)n; }
void scene_boss_hit(bool b) { (void)b; }
void scene_boss_win(void) {}
void sfx_win(void) {}
void sfx_hit(void) {}
void sfx_add(void) {}

static const char *HOME = "build/testhome";
static const char *STORE =
    "{\"v\":1,\"xp\":400,\"total\":8,\"time\":40,\"streak\":1,"
    "\"last\":\"2026-10-11\",\"bosses\":2,\"badges\":[\"first\"],"
    "\"quests\":[],\"foe\":0,\"nextId\":9}\n";

static int fails;
static void check(const char *what, bool ok) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) fails++;
}

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); exit(1); }
    fputs(content, f);
    fclose(f);
}

int main(void) {
    char path[512];
    SDL_setenv_unsafe("HOME", HOME, true);
    SDL_CreateDirectory(HOME);
    SDL_snprintf(path, sizeof path, "%s/store.json", HOME);
    write_file(path, STORE);
    game_set_store(path);

    // First init loads the pinned store.
    game_init();
    check("store loaded", S.xp == 400 && S.total == 8 && S.bosses == 2 && S.nextId == 9);

    add_quest("first quest");
    add_quest("second quest");
    check("two quests added", S.nquests == 2 && S.nextId == 11);
    toggle_timer(S.quests[0].id);
    check("timer running", running && active == S.quests[0].id);
    game_tick();
    game_tick();
    game_tick();
    check("chronometer ticks", S.quests[0].secs == 3 && S.time == 43);
    complete(&S.quests[0], 100, 100);
    check("xp awarded", S.xp == 400 + BASE_XP && S.total == 9);
    game_save();

    // Reload from disk into a fresh state.
    game_init();
    check("quests reloaded", S.nquests == 2 && S.total == 9 && S.xp == 400 + BASE_XP);
    check("text roundtrip", strcmp(S.quests[1].text, "second quest") == 0);
    check("badge kept", has_badge("first"));
    undo(&S.quests[0]);
    game_save();
    game_init();
    check("undo reloaded", !S.quests[0].done && S.quests[0].id == 9);

    // Unpinned store defaults to ~/.clawdquest.
    game_set_store("");
    game_init();
    check("fresh store starts at defaults", S.xp == 0 && S.nquests == 0);
    add_quest("hello");
    game_save();
    SDL_snprintf(path, sizeof path, "%s/.clawdquest/store.json", HOME);
    FILE *f = fopen(path, "r");
    check("~/.clawdquest/store.json written", f != NULL);
    if (f) fclose(f);

    printf("%s\n", fails ? "FAILURES" : "ALL PASS");
    return fails;
}
