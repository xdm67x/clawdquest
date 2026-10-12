// ---------------------------------------------------------------------------
// Persistence.
//
// Inside the tinyjs app this uses tiny.store, which writes
// ~/Library/Application Support/<app id>/store.json.
// In a plain browser it falls back to localStorage, so the page still works
// when opened outside the app.
// ---------------------------------------------------------------------------

// The single key the whole state blob is stored under.
const KEY = 'state';

// A fresh state object. Declared as a function so every call returns a new
// copy with no shared arrays/objects between saves.
const DEFAULTS = () => ({
  v: 1,        // save format version, for future migrations
  xp: 0,       // total experience points
  total: 0,    // quests completed, all time
  time: 0,     // total seconds focused (the chronometer)
  streak: 0,   // consecutive active days
  last: null,  // last active day, as YYYY-MM-DD
  bosses: 0,   // bosses defeated
  badges: [],  // trophy ids unlocked
  quests: [],  // the current quest board (each quest carries its own secs)
  foe: 0,     // boards cleared so far; Clawd wakes again each time
  nextId: 1,   // id for the next quest added
});

// Storage backend: tiny.store when available, localStorage otherwise.
// Checked on every call because the tiny bridge may not exist yet at load time.
const backend = {
  get: async k =>
    typeof tiny !== 'undefined' && tiny.store
      ? tiny.store.get(k)
      : JSON.parse(localStorage.getItem(k) || 'null'),
  set: async (k, v) =>
    typeof tiny !== 'undefined' && tiny.store
      ? tiny.store.set(k, v)
      : localStorage.setItem(k, JSON.stringify(v)),
};

// The one game state object. Mutate it anywhere, then call save().
let S = DEFAULTS();

let pending = Promise.resolve();

// Persist the current state.
// Writes are queued on a promise chain so they land in order, and a failed
// write is logged without breaking later writes or the game.
const save = () => {
  // Deep copy: later mutations of S must not leak into this pending write.
  const snapshot = JSON.parse(JSON.stringify(S));
  pending = pending
    .then(() => backend.set(KEY, snapshot))
    .catch(e => console.error('save failed', e));
  return pending;
};

// Load the saved state. Must finish before the first render, otherwise an
// early click could write the defaults over real progress.
async function load() {
  try {
    const saved = await backend.get(KEY);
    if (saved) {
      // Merge over defaults: new fields get their default value,
      // and saves from older versions keep working.
      S = { ...DEFAULTS(), ...saved };
    }
  } catch (e) {
    console.error('load failed', e);
  }
}
