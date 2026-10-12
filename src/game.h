// ---------------------------------------------------------------------------
// Game rules and persistence — a port of the original config.js / store.js /
// game.js / timer.js. Same formulas, same save schema.
// ---------------------------------------------------------------------------
#pragma once

#include <stdbool.h>

typedef struct {
    int id;
    char text[256];
    bool done;
    long secs; // focused seconds on this quest
} Quest;

typedef struct {
    int v;          // save format version
    long xp;        // total experience
    long total;      // quests completed, all time
    long time;      // total seconds focused
    int streak;     // consecutive active days
    char last[16];  // last active day, YYYY-MM-DD ("" = none)
    int bosses;     // bosses defeated
    char badges[16][16]; // trophy ids unlocked
    int nbadges;
    Quest quests[256];
    int nquests;
    int foe;    // boards cleared so far; Clawd wakes again each time
    int nextId; // id for the next quest
} State;

extern State S;
extern int active;    // id of the quest the chronometer targets
extern bool running;  // the heartbeat only advances while this is true

#define BASE_XP 10
#define XP_PER_MIN 4
#define HEAVY_HIT_SECS 600

int lvl(long xp);
long lvlStart(int n);
const char *title_of(int l); // player rank name for level l
void today(char out[16]);   // YYYY-MM-DD
void fmt_time_h(char *out, int n, long s);   // "42m" / "3.5h"
void fmt_mmss(char *out, int n, long s);     // "mm:ss"
bool has_badge(const char *id);

void game_init(void);   // load save
void game_save(void);   // persist immediately
void game_set_store(const char *path); // test hook: pin the save file location ("" = ~/.clawdquest)
void award(long amount, float x, float y);
void complete(Quest *q, float x, float y);
void undo(Quest *q);
void add_quest(const char *text);
void del_quest(Quest *q);
void toggle_timer(int quest_id);
void game_tick(void); // 1s chronometer heartbeat
