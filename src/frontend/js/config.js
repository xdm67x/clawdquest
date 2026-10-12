// ---------------------------------------------------------------------------
// Static game data and small helpers. No game state lives here.
// ---------------------------------------------------------------------------

// Payout for completing a quest: a flat base plus XP for every minute worked
// on it. The chronometer time is the reward — level progress and boss damage
// both scale with it.
const BASE_XP = 10;
const XP_PER_MIN = 4;

// Quests worked at least this long land a heavy hit on the boss.
const HEAVY_HIT_SECS = 600;

// Human-readable focused time for the HUD stat: "42m" / "3.5h".
const fmtTime = s => (s < 3600 ? (s / 60 | 0) + 'm' : (s / 3600).toFixed(1) + 'h');

// Player rank names, one per level (index 0 = level 1).
// Levels above the list length fall back to the last title.
const TITLES = [
  'Procrastinator',
  'Apprentice',
  'Doer',
  'Grinder',
  'Slayer',
  'Champion',
  'Legend',
  'Mythic',
];

// The boss: Claude Code, "Clawd". He is the only boss — every cleared board
// just wakes him again.
const NAMES = ['Clawd'];

// Trophies: [id, label, unlock condition].
// Each condition receives the whole state object S and returns true when earned.
const BADGES = [
  ['first',   '🗡 First blood',  s => s.total >= 1],
  ['ten',     '🏅 10 quests',    s => s.total >= 10],
  ['fifty',   '👑 50 quests',    s => s.total >= 50],
  ['focus',   '🧘 25 min focused', s => s.time >= 1500],
  ['five',    '⏳ 5 h focused',    s => s.time >= 18000],
  ['streak3', '🔥 3-day streak', s => s.streak >= 3],
  ['streak7', '🌋 7-day streak', s => s.streak >= 7],
  ['boss',    '💀 Boss slain',   s => s.bosses >= 1],
  ['lvl5',    '⭐ Level 5',      s => lvl(s.xp) >= 5],
];

// Current level from total XP.
// Level 1 starts at 0 XP and each level costs a bit more than the last
// (sqrt curve: levels come fast at first, slower later).
const lvl = xp => Math.floor(Math.sqrt(xp / 50)) + 1;

// Total XP needed to *reach* level n (i.e. the start of level n).
const lvlStart = n => 50 * (n - 1) * (n - 1);

// Today's date as YYYY-MM-DD (the 'en-CA' locale formats it that way).
// Used to track daily streaks.
const today = () => new Date().toLocaleDateString('en-CA');

// Tiny DOM helper: $('#lvl') is document.getElementById('lvl').
const $ = id => document.getElementById(id);
