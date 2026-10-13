#include "game.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "audio.h"
#include "json.h"
#include "pet.h"
#include "ui.h"

State S;
int active = -1;
bool running = false;

// --- config.js -------------------------------------------------------------

static const char *TITLES[] = {
    "Procrastinator", "Apprentice", "Doer", "Grinder",
    "Slayer", "Champion", "Legend", "Mythic",
};

int lvl(long xp) { return (int)(SDL_sqrt((double)xp / 50.0)) + 1; }
long lvlStart(int n) { return 50L * (n - 1) * (n - 1); }

const char *title_of(int l) {
    int i = l - 1;
    if (i < 0) i = 0;
    int max = (int)(sizeof TITLES / sizeof TITLES[0]) - 1;
    return TITLES[i > max ? max : i];
}

void today(char out[16]) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    snprintf(out, 16, "%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
}

static void yesterday(char out[16]) {
    time_t t = time(NULL) - 86400;
    struct tm tmv;
    localtime_r(&t, &tmv);
    snprintf(out, 16, "%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
}

void fmt_time_h(char *out, int n, long s) {
    if (s < 3600) snprintf(out, (size_t)n, "%ldm", s / 60);
    else snprintf(out, (size_t)n, "%.1fh", (double)s / 3600.0);
}

void fmt_mmss(char *out, int n, long s) {
    snprintf(out, (size_t)n, "%02ld:%02ld", s / 60, s % 60);
}

bool has_badge(const char *id) {
    for (int i = 0; i < S.nbadges; i++)
        if (strcmp(S.badges[i], id) == 0) return true;
    return false;
}

// --- store.js --------------------------------------------------------------

static char store_path[512];

static void defaults(void) {
    memset(&S, 0, sizeof S);
    S.v = 1;
    S.nextId = 1;
    S.streak = 0;
}

static JVal *read_json_file(const char *path) {
    SDL_IOStream *io = SDL_IOFromFile(path, "r");
    if (!io) return NULL;
    size_t sz = 0;
    char *buf = (char *)SDL_LoadFile_IO(io, &sz, true);
    if (!buf) return NULL;
    JVal *root = json_parse(buf);
    SDL_free(buf);
    return root;
}

// Merge a saved state object over the defaults (new fields keep their default).
static void apply_save(JVal *root) {
    if (!root) return;
    defaults();

    JVal *j;
    if ((j = json_get(root, "xp"))) S.xp = (long)j->num;
    if ((j = json_get(root, "total"))) S.total = (long)j->num;
    if ((j = json_get(root, "time"))) S.time = (long)j->num;
    if ((j = json_get(root, "streak"))) S.streak = (int)j->num;
    if ((j = json_get(root, "bosses"))) S.bosses = (int)j->num;
    if ((j = json_get(root, "foe"))) S.foe = (int)j->num;
    if ((j = json_get(root, "nextId"))) S.nextId = (int)j->num;
    if ((j = json_get(root, "last")) && j->t == J_STR)
        SDL_strlcpy(S.last, j->str, sizeof S.last);
    if ((j = json_get(root, "badges")) && j->t == J_ARR)
        for (JVal *b = j->child; b && S.nbadges < 16; b = b->next)
            if (b->t == J_STR)
                SDL_strlcpy(S.badges[S.nbadges++], b->str, sizeof S.badges[0]);
    if ((j = json_get(root, "quests")) && j->t == J_ARR)
        for (JVal *q = j->child; q && S.nquests < 256; q = q->next) {
            Quest *out = &S.quests[S.nquests];
            JVal *f;
            out->id = (f = json_get(q, "id")) ? (int)f->num : 0;
            out->done = (f = json_get(q, "done")) ? f->num != 0 : false;
            out->secs = (f = json_get(q, "secs")) ? (long)f->num : 0;
            if ((f = json_get(q, "text")) && f->t == J_STR)
                SDL_strlcpy(out->text, f->str, sizeof out->text);
            S.nquests++;
        }
}

void game_save(void) {
    if (!store_path[0]) return;
    JVal *root = jnew_(J_OBJ);
    jnum_(root, "v", S.v);
    jnum_(root, "xp", (double)S.xp);
    jnum_(root, "total", (double)S.total);
    jnum_(root, "time", (double)S.time);
    jnum_(root, "streak", S.streak);
    jnum_(root, "bosses", S.bosses);
    jnum_(root, "foe", S.foe);
    jnum_(root, "nextId", S.nextId);
    jstr_(root, "last", S.last);

    JVal *badges = jnew_(J_ARR);
    for (int i = 0; i < S.nbadges; i++) {
        JVal *b = jnew_(J_STR);
        b->str = SDL_strdup(S.badges[i]);
        jadd_(badges, b);
    }
    jkey_(badges, "badges");
    jadd_(root, badges);

    JVal *quests = jnew_(J_ARR);
    for (int i = 0; i < S.nquests; i++) {
        JVal *q = jnew_(J_OBJ);
        jnum_(q, "id", S.quests[i].id);
        jstr_(q, "text", S.quests[i].text);
        jnum_(q, "done", S.quests[i].done ? 1 : 0);
        jnum_(q, "secs", (double)S.quests[i].secs);
        jadd_(quests, q);
    }
    jkey_(quests, "quests");
    jadd_(root, quests);

    char *out = json_write(root);
    if (out) {
        SDL_IOStream *io = SDL_IOFromFile(store_path, "w");
        if (io) {
            SDL_WriteIO(io, out, SDL_strlen(out));
            SDL_CloseIO(io);
        }
        SDL_free(out);
    }
    json_free(root);
}

void game_set_store(const char *path) {
    snprintf(store_path, sizeof store_path, "%s", path);
}

void game_init(void) {
    defaults();
    const char *home = SDL_getenv("HOME");

    // The store lives in ~/.clawdquest.
    if (!store_path[0] && home) {
        char dir[512];
        snprintf(dir, sizeof dir, "%s/.clawdquest", home);
        SDL_CreateDirectory(dir);
        snprintf(store_path, sizeof store_path, "%s/store.json", dir);
    }

    JVal *root = read_json_file(store_path);
    if (root) {
        apply_save(root);
        json_free(root);
    }
}

// --- game.js -----------------------------------------------------------------

void award(long amount, float x, float y) {
    int before = lvl(S.xp);
    S.xp += amount;
    char txt[32];
    snprintf(txt, sizeof txt, "+%ld XP", amount);
    ui_pop(txt, x, y);
    if (lvl(S.xp) > before) {
        char toast[80];
        snprintf(toast, sizeof toast, "LEVEL %d — %s!", lvl(S.xp), title_of(lvl(S.xp)));
        ui_toast(toast);
        sfx_win();
        ui_confetti(140);
    }
}

static void touch_streak(void) {
    char t[16];
    today(t);
    if (strcmp(S.last, t) == 0) return;
    char y[16];
    yesterday(y);
    S.streak = strcmp(S.last, y) == 0 ? S.streak + 1 : 1;
    SDL_strlcpy(S.last, t, sizeof S.last);
}

static const struct { const char *id; } BADGES[] = {
    { "first" }, { "ten" }, { "fifty" }, { "focus" }, { "five" },
    { "streak3" }, { "streak7" }, { "lvl5" },
};

static bool badge_ok(const char *id) {
    if (strcmp(id, "first") == 0) return S.total >= 1;
    if (strcmp(id, "ten") == 0) return S.total >= 10;
    if (strcmp(id, "fifty") == 0) return S.total >= 50;
    if (strcmp(id, "focus") == 0) return S.time >= 1500;
    if (strcmp(id, "five") == 0) return S.time >= 18000;
    if (strcmp(id, "streak3") == 0) return S.streak >= 3;
    if (strcmp(id, "streak7") == 0) return S.streak >= 7;
    if (strcmp(id, "lvl5") == 0) return lvl(S.xp) >= 5;
    return false;
}

static void check_badges(void) {
    for (unsigned i = 0; i < sizeof BADGES / sizeof BADGES[0]; i++)
        if (!has_badge(BADGES[i].id) && badge_ok(BADGES[i].id) && S.nbadges < 16)
            SDL_strlcpy(S.badges[S.nbadges++], BADGES[i].id, sizeof S.badges[0]);
}

void complete(Quest *q, float x, float y) {
    q->done = true;
    S.total++;
    touch_streak();
    long mins = (q->secs + 30) / 60; // Math.round
    award(BASE_XP + mins * XP_PER_MIN, x, y);

    // A finished quest is an apple for Clawd.
    pet_feed();
    sfx_hit();
    if (active == q->id) { active = -1; running = false; }

    bool all_done = true;
    for (int i = 0; i < S.nquests; i++)
        if (!S.quests[i].done) { all_done = false; break; }
    if (all_done) {
        ui_confetti(300);
        pet_win();
        sfx_win();
        ui_toast("All quests done!");
    }

    check_badges();
    game_save();
}

void undo(Quest *q) {
    q->done = false;
    game_save();
}

void add_quest(const char *text) {
    if (S.nquests >= 256) return;
    // Board fully cleared? Start a fresh board.
    bool all_done = S.nquests > 0;
    for (int i = 0; i < S.nquests; i++)
        if (!S.quests[i].done) { all_done = false; break; }
    if (all_done) S.nquests = 0;
    Quest *q = &S.quests[S.nquests++];
    q->id = S.nextId++;
    SDL_strlcpy(q->text, text, sizeof q->text);
    q->done = false;
    q->secs = 0;
    sfx_add();
    game_save();
}

void del_quest(Quest *q) {
    if (active == q->id) { active = -1; running = false; }
    int i = (int)(q - S.quests);
    if (i < 0 || i >= S.nquests) return;
    memmove(&S.quests[i], &S.quests[i + 1], (size_t)(S.nquests - i - 1) * sizeof(Quest));
    S.nquests--;
    game_save();
}

void toggle_timer(int quest_id) {
    for (int i = 0; i < S.nquests; i++) {
        if (S.quests[i].id != quest_id || S.quests[i].done) continue;
        if (active == quest_id && running) running = false;
        else { active = quest_id; running = true; }
        return;
    }
}

// --- timer.js ----------------------------------------------------------------

void game_tick(void) {
    if (!running || active < 0) return;
    Quest *q = NULL;
    for (int i = 0; i < S.nquests; i++)
        if (S.quests[i].id == active && !S.quests[i].done) { q = &S.quests[i]; break; }
    if (!q) {
        running = false;
        active = -1;
        return;
    }
    q->secs++;
    S.time++;
    // Like the original timer heartbeat: no per-second save; the session's
    // time is persisted on the next state change or on quit.
}
