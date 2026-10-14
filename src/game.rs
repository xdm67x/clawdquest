//! Game rules and persistence. The save schema matches the original release,
//! so existing progress loads unchanged; serde_json provides the typed
//! reader/writer, and missing fields keep their defaults.

use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

use crate::fx::FxSink;

pub const BASE_XP: i64 = 10;
pub const XP_PER_MIN: i64 = 4;

const MAX_QUESTS: usize = 256;
const MAX_BADGES: usize = 16;

#[derive(Clone, Debug, PartialEq)]
pub struct Quest {
    pub id: i32,
    pub text: String,
    pub done: bool,
    pub secs: i64, // focused seconds on this quest
}

#[derive(Clone, Debug)]
pub struct State {
    pub v: i32,              // save format version
    pub xp: i64,             // total experience
    pub total: i64,          // quests completed, all time
    pub time: i64,           // total seconds focused
    pub streak: i32,         // consecutive active days
    pub last: String,        // last active day, YYYY-MM-DD ("" = none)
    pub badges: Vec<String>, // trophy ids unlocked (capped at 16)
    pub quests: Vec<Quest>,  // capped at 256
    pub bosses: i32,         // legacy field (kept so old saves load unchanged)
    pub foe: i32,            // legacy field (kept so old saves load unchanged)
    pub next_id: i32,        // id for the next quest
    // The chronometer: which quest is active and whether it is running.
    pub active: i32,
    pub running: bool,
}

impl Default for State {
    fn default() -> Self {
        Self {
            v: 1,
            xp: 0,
            total: 0,
            time: 0,
            streak: 0,
            last: String::new(),
            badges: Vec::new(),
            quests: Vec::new(),
            bosses: 0,
            foe: 0,
            next_id: 1,
            active: -1,
            running: false,
        }
    }
}

// --- save schema (matches the original field order) ---------------------------

#[derive(Serialize, Deserialize, Default)]
#[serde(default)]
struct Save {
    v: i32,
    xp: i64,
    total: i64,
    time: i64,
    streak: i32,
    bosses: i32,
    foe: i32,
    #[serde(rename = "nextId")]
    next_id: i32,
    last: String,
    badges: Vec<String>,
    quests: Vec<SaveQuest>,
}

#[derive(Serialize, Deserialize, Default)]
#[serde(default)]
struct SaveQuest {
    id: i32,
    text: String,
    // Older saves stored done as 0/1; accept both that and true/false.
    #[serde(deserialize_with = "de_done")]
    done: bool,
    secs: i64,
}

impl From<SaveQuest> for Quest {
    fn from(q: SaveQuest) -> Self {
        Quest {
            id: q.id,
            text: q.text,
            done: q.done,
            secs: q.secs,
        }
    }
}

impl From<&Quest> for SaveQuest {
    fn from(q: &Quest) -> Self {
        SaveQuest {
            id: q.id,
            text: q.text.clone(),
            done: q.done,
            secs: q.secs,
        }
    }
}

impl From<Save> for State {
    fn from(save: Save) -> Self {
        State {
            v: save.v,
            xp: save.xp,
            total: save.total,
            time: save.time,
            streak: save.streak,
            last: save.last,
            badges: save.badges.into_iter().take(MAX_BADGES).collect(),
            quests: save
                .quests
                .into_iter()
                .take(MAX_QUESTS)
                .map(Quest::from)
                .collect(),
            bosses: save.bosses,
            foe: save.foe,
            next_id: save.next_id,
            ..State::default()
        }
    }
}

impl From<&State> for Save {
    fn from(s: &State) -> Self {
        Save {
            v: s.v,
            xp: s.xp,
            total: s.total,
            time: s.time,
            streak: s.streak,
            bosses: s.bosses,
            foe: s.foe,
            next_id: s.next_id,
            last: s.last.clone(),
            badges: s.badges.clone(),
            quests: s.quests.iter().map(SaveQuest::from).collect(),
        }
    }
}

fn de_done<'de, D>(d: D) -> Result<bool, D::Error>
where
    D: serde::Deserializer<'de>,
{
    struct V;
    impl serde::de::Visitor<'_> for V {
        type Value = bool;
        fn expecting(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
            f.write_str("bool or number")
        }
        fn visit_bool<E>(self, v: bool) -> Result<bool, E> {
            Ok(v)
        }
        fn visit_i64<E>(self, v: i64) -> Result<bool, E> {
            Ok(v != 0)
        }
        fn visit_u64<E>(self, v: u64) -> Result<bool, E> {
            Ok(v != 0)
        }
        fn visit_f64<E>(self, v: f64) -> Result<bool, E> {
            Ok(v != 0.0)
        }
    }
    d.deserialize_any(V)
}

// --- formulas ------------------------------------------------------------------

const TITLES: [&str; 8] = [
    "Procrastinator",
    "Apprentice",
    "Doer",
    "Grinder",
    "Slayer",
    "Champion",
    "Legend",
    "Mythic",
];

pub fn lvl(xp: i64) -> i32 {
    ((xp as f64 / 50.0).sqrt() as i32) + 1
}

pub fn lvl_start(n: i32) -> i64 {
    let n = n as i64;
    50 * (n - 1) * (n - 1)
}

pub fn title_of(l: i32) -> &'static str {
    let i = (l - 1).clamp(0, TITLES.len() as i32 - 1);
    TITLES[i as usize]
}

pub fn today() -> String {
    chrono::Local::now().format("%Y-%m-%d").to_string()
}

fn yesterday() -> String {
    (chrono::Local::now() - chrono::Duration::days(1))
        .format("%Y-%m-%d")
        .to_string()
}

pub fn fmt_time_h(s: i64) -> String {
    if s < 3600 {
        format!("{}m", s / 60)
    } else {
        format!("{:.1}h", s as f64 / 3600.0)
    }
}

pub fn fmt_mmss(s: i64) -> String {
    format!("{:02}:{:02}", s / 60, s % 60)
}

// --- the game -------------------------------------------------------------------

pub struct Game {
    pub s: State,
    /// Pinned save file; None = ~/.clawdquest/store.json (test hook).
    store: Option<PathBuf>,
    home: Option<PathBuf>,
}

impl Game {
    pub fn new() -> Self {
        Self {
            s: State::default(),
            store: None,
            home: std::env::var_os("HOME").map(PathBuf::from),
        }
    }

    /// Test hook: pin the save file location (None = ~/.clawdquest).
    #[cfg_attr(not(test), allow(dead_code))]
    pub fn set_store(&mut self, path: Option<PathBuf>) {
        self.store = path;
    }

    fn store_path(&self) -> Option<PathBuf> {
        if let Some(p) = &self.store {
            return Some(p.clone());
        }
        self.home
            .as_ref()
            .map(|h| h.join(".clawdquest").join("store.json"))
    }

    pub fn init(&mut self) {
        self.s = State::default();
        let Some(path) = self.store_path() else {
            return;
        };
        if self.store.is_none() {
            // The store lives in ~/.clawdquest.
            if let Some(dir) = path.parent() {
                let _ = std::fs::create_dir_all(dir);
            }
        }
        if let Ok(text) = std::fs::read_to_string(&path) {
            match serde_json::from_str::<Save>(&text) {
                Ok(save) => self.s = State::from(save),
                Err(e) => eprintln!("clawdquest: ignoring save {path:?}: {e}"), // malformed; keep defaults
            }
        }
    }

    pub fn save(&self) {
        let Some(path) = self.store_path() else {
            return;
        };
        let store = Save::from(&self.s);
        if let Ok(out) = serde_json::to_string(&store) {
            if let Some(dir) = Path::new(&path).parent() {
                let _ = std::fs::create_dir_all(dir);
            }
            let _ = std::fs::write(&path, out);
        }
    }

    pub fn has_badge(&self, id: &str) -> bool {
        self.s.badges.iter().any(|b| b == id)
    }

    fn touch_streak(&mut self) {
        let t = today();
        if self.s.last == t {
            return;
        }
        let y = yesterday();
        self.s.streak = if self.s.last == y {
            self.s.streak + 1
        } else {
            1
        };
        self.s.last = t;
    }

    fn badge_ok(&self, id: &str) -> bool {
        let s = &self.s;
        match id {
            "first" => s.total >= 1,
            "ten" => s.total >= 10,
            "fifty" => s.total >= 50,
            "focus" => s.time >= 1500,
            "five" => s.time >= 18000,
            "streak3" => s.streak >= 3,
            "streak7" => s.streak >= 7,
            "lvl5" => lvl(s.xp) >= 5,
            _ => false,
        }
    }

    fn check_badges(&mut self) {
        const BADGES: [&str; 8] = [
            "first", "ten", "fifty", "focus", "five", "streak3", "streak7", "lvl5",
        ];
        for id in BADGES {
            if !self.has_badge(id) && self.badge_ok(id) && self.s.badges.len() < MAX_BADGES {
                self.s.badges.push(id.to_string());
            }
        }
    }

    pub fn award(&mut self, amount: i64, x: f32, y: f32, fx: &mut dyn FxSink) {
        let before = lvl(self.s.xp);
        self.s.xp += amount;
        fx.pop(&format!("+{amount} XP"), x, y);
        if lvl(self.s.xp) > before {
            let l = lvl(self.s.xp);
            fx.toast(&format!("LEVEL {l} \u{2022} {}!", title_of(l)));
            fx.sfx_win();
            fx.confetti(140);
        }
    }

    pub fn complete(&mut self, qi: usize, x: f32, y: f32, fx: &mut dyn FxSink) {
        let id = self.s.quests[qi].id;
        let mins = (self.s.quests[qi].secs + 30) / 60; // Math.round
        self.s.quests[qi].done = true;
        self.s.total += 1;
        self.touch_streak();
        self.award(BASE_XP + mins * XP_PER_MIN, x, y, fx);

        // A finished quest is an apple for Clawd.
        fx.feed();
        fx.sfx_hit();
        if self.s.active == id {
            self.s.active = -1;
            self.s.running = false;
        }

        let all_done = self.s.quests.iter().all(|q| q.done);
        if all_done {
            fx.confetti(300);
            fx.win();
            fx.sfx_win();
            fx.toast("All quests done!");
        }

        self.check_badges();
        self.save();
    }

    pub fn undo(&mut self, qi: usize) {
        self.s.quests[qi].done = false;
        self.save();
    }

    pub fn add_quest(&mut self, text: &str, fx: &mut dyn FxSink) {
        if self.s.quests.len() >= MAX_QUESTS {
            return;
        }
        // Board fully cleared? Start a fresh board.
        let all_done = !self.s.quests.is_empty() && self.s.quests.iter().all(|q| q.done);
        if all_done {
            self.s.quests.clear();
        }
        let id = self.s.next_id;
        self.s.next_id += 1;
        self.s.quests.push(Quest {
            id,
            text: text.to_string(),
            done: false,
            secs: 0,
        });
        fx.sfx_add();
        self.save();
    }

    pub fn del_quest(&mut self, qi: usize) {
        let id = self.s.quests[qi].id;
        if self.s.active == id {
            self.s.active = -1;
            self.s.running = false;
        }
        self.s.quests.remove(qi);
        self.save();
    }

    pub fn toggle_timer(&mut self, quest_id: i32) {
        for q in &self.s.quests {
            if q.id != quest_id || q.done {
                continue;
            }
            if self.s.active == quest_id && self.s.running {
                self.s.running = false;
            } else {
                self.s.active = quest_id;
                self.s.running = true;
            }
            return;
        }
    }

    /// 1s chronometer heartbeat.
    pub fn tick(&mut self) {
        if !self.s.running || self.s.active < 0 {
            return;
        }
        let active = self.s.active;
        if let Some(q) = self.s.quests.iter_mut().find(|q| q.id == active && !q.done) {
            q.secs += 1;
            self.s.time += 1;
            // Like the original timer heartbeat: no per-second save; the
            // session's time is persisted on the next state change or on quit.
        } else {
            self.s.running = false;
            self.s.active = -1;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    struct DummyFx;
    impl crate::fx::FxSink for DummyFx {
        fn pop(&mut self, _: &str, _: f32, _: f32) {}
        fn toast(&mut self, _: &str) {}
        fn confetti(&mut self, _: usize) {}
        fn feed(&mut self) {}
        fn win(&mut self) {}
        fn sfx_hit(&mut self) {}
        fn sfx_win(&mut self) {}
        fn sfx_add(&mut self) {}
    }

    #[test]
    fn formulas() {
        assert_eq!(lvl(0), 1);
        assert_eq!(lvl(50), 2);
        assert_eq!(lvl_start(1), 0);
        assert_eq!(lvl_start(2), 50);
        assert_eq!(fmt_mmss(75), "01:15");
        assert_eq!(fmt_time_h(1800), "30m");
        assert_eq!(fmt_time_h(7200), "2.0h");
        assert_eq!(title_of(1), "Procrastinator");
        assert_eq!(title_of(9), "Mythic"); // capped at the last title
    }

    /// The save-file round-trip suite (runs under a throwaway HOME so the
    /// real save is never touched).
    #[test]
    fn save_roundtrip() {
        const STORE: &str = "{\"v\":1,\"xp\":400,\"total\":8,\"time\":40,\"streak\":1,\
\"last\":\"2026-10-11\",\"bosses\":2,\"badges\":[\"first\"],\
\"quests\":[],\"foe\":0,\"nextId\":9}\n";

        let tmp = std::env::temp_dir().join(format!("clawdquest-test-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&tmp);
        std::fs::create_dir_all(&tmp).unwrap();
        let store = tmp.join("store.json");
        std::fs::write(&store, STORE).unwrap();

        let mut fx = DummyFx;
        let mut g = Game::new();
        g.set_store(Some(store.clone()));

        // First init loads the pinned store.
        g.init();
        assert!(
            g.s.xp == 400 && g.s.total == 8 && g.s.bosses == 2 && g.s.next_id == 9,
            "store loaded"
        );

        g.add_quest("first quest", &mut fx);
        g.add_quest("second quest", &mut fx);
        assert!(
            g.s.quests.len() == 2 && g.s.next_id == 11,
            "two quests added"
        );
        g.toggle_timer(g.s.quests[0].id);
        assert!(
            g.s.running && g.s.active == g.s.quests[0].id,
            "timer running"
        );
        g.tick();
        g.tick();
        g.tick();
        assert!(
            g.s.quests[0].secs == 3 && g.s.time == 43,
            "chronometer ticks"
        );
        g.complete(0, 100.0, 100.0, &mut fx);
        assert!(g.s.xp == 400 + BASE_XP && g.s.total == 9, "xp awarded");
        g.save();

        // Reload from disk into a fresh state.
        g.init();
        assert!(
            g.s.quests.len() == 2 && g.s.total == 9 && g.s.xp == 400 + BASE_XP,
            "quests reloaded"
        );
        assert!(g.s.quests[1].text == "second quest", "text roundtrip");
        assert!(g.has_badge("first"), "badge kept");
        g.undo(0);
        g.save();
        g.init();
        assert!(
            !g.s.quests[0].done && g.s.quests[0].id == 9,
            "undo reloaded"
        );

        // Unpinned store defaults to <home>/.clawdquest. Clear it before
        // checking that a fresh store starts at defaults (the test must be
        // idempotent across runs).
        g.store = None;
        g.home = Some(tmp.clone());
        let def = tmp.join(".clawdquest/store.json");
        let _ = std::fs::remove_file(&def);
        g.init();
        assert!(
            g.s.xp == 0 && g.s.quests.is_empty(),
            "fresh store starts at defaults"
        );
        g.add_quest("hello", &mut fx);
        g.save();
        assert!(def.exists(), "~/.clawdquest/store.json written");

        let _ = std::fs::remove_dir_all(&tmp);
    }

    /// Older saves emitted "done" as a 0/1 number; both that and a real
    /// bool must load.
    #[test]
    fn done_number_or_bool() {
        let tmp = std::env::temp_dir().join(format!("clawdquest-test-done-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&tmp);
        std::fs::create_dir_all(&tmp).unwrap();
        let store = tmp.join("store.json");
        std::fs::write(
            &store,
            "{\"quests\":[{\"id\":1,\"text\":\"a\",\"done\":1,\"secs\":5},\
            {\"id\":2,\"text\":\"b\",\"done\":true,\"secs\":0},\
            {\"id\":3,\"text\":\"c\",\"secs\":0}]}",
        )
        .unwrap();
        let mut g = Game::new();
        g.set_store(Some(store.clone()));
        g.init();
        assert_eq!(g.s.quests.len(), 3);
        assert!(g.s.quests[0].done); // 1
        assert!(g.s.quests[1].done); // true
        assert!(!g.s.quests[2].done); // missing
        let _ = std::fs::remove_dir_all(&tmp);
    }
}
