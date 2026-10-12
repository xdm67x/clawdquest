// ---------------------------------------------------------------------------
// Game rules: XP, levels, streaks, trophies, completing quests.
// ---------------------------------------------------------------------------

// Add XP. Shows a floating "+n XP" at the click position (or a default spot
// for programmatic awards), and celebrates level-ups.
function award(amount, ev) {
  const before = lvl(S.xp);
  S.xp += amount;
  pop('+' + amount + ' XP', ev?.clientX ?? innerWidth * 0.4, ev?.clientY ?? 260);
  if (lvl(S.xp) > before) {
    toast(`⬆ LEVEL ${lvl(S.xp)} — ${TITLES[Math.min(lvl(S.xp) - 1, 7)]}!`);
    sfx.win();
    confetti();
  }
}

// Update the daily streak. Only the first activity of a day counts:
//   - same day  -> already counted, nothing to do
//   - yesterday -> streak continues
//   - older gap -> streak restarts at 1
function touchStreak() {
  const t = today();
  if (S.last === t) return;
  const y = new Date(Date.now() - 864e5).toLocaleDateString('en-CA'); // yesterday
  S.streak = S.last === y ? S.streak + 1 : 1;
  S.last = t;
}

// Record every trophy whose condition is met. Trophies no longer have a shelf
// in the UI, so unlocks are silent — they are kept in the save file.
function checkBadges() {
  for (const [id, , ok] of BADGES) {
    if (!S.badges.includes(id) && ok(S)) S.badges.push(id);
  }
}

// Complete a quest. The click event ev is used to place the +XP popup.
// The chronometer time spent on the quest pays out twice: as XP for level
// progress and as the punch dealt to the boss. Quests worked 10+ minutes
// land a heavy hit.
function complete(q, ev) {
  q.done = true;
  S.total++;
  touchStreak();
  const mins = Math.round((q.secs || 0) / 60);
  award(BASE_XP + mins * XP_PER_MIN, ev);

  const bossDown = S.quests.every(x => x.done);

  BOSS.hit((q.secs || 0) >= HEAVY_HIT_SECS);
  sfx.hit();
  if (active === q.id) { active = null; running = false; }

  // Whole board cleared: bonus XP and the boss goes down.
  if (bossDown) {
    S.bosses++;
    award(100);
    confetti(300);
    BOSS.win();
    sfx.win();
    toast('💀 BOSS DEFEATED! Everything is done.');
  }

  checkBadges();
  save();
  render();
}

// Un-check a quest (no XP is taken back, the round count stays too).
function undo(q) {
  q.done = false;
  save();
  render();
}
