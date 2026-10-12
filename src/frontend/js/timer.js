// ---------------------------------------------------------------------------
// Chronometer: a single stopwatch shared by the quests.
//
// Clicking a quest makes it the active one and starts its clock; clicking the
// same quest again pauses it; clicking another quest switches the clock.
// Time accumulates per quest (q.secs) and in the session total (S.time),
// one second at a time, only while running.
// ---------------------------------------------------------------------------

let running = false; // the heartbeat only advances while this is true

// Format seconds as mm:ss (minutes may exceed 59).
function fmt(s) {
  return String(s / 60 | 0).padStart(2, '0') + ':' + String(s % 60).padStart(2, '0');
}

// One-second heartbeat. The active quest is resolved on every tick, so a
// quest that was completed, deleted or undone in between simply stops the
// clock instead of leaking time onto a ghost task.
setInterval(() => {
  if (!running || !active) return;
  const q = S.quests.find(x => x.id === active && !x.done);
  if (!q) {
    running = false;
    active = null;
    return render();
  }
  q.secs = (q.secs || 0) + 1;
  S.time = (S.time || 0) + 1;
  render();
}, 1000);
