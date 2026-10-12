// Persistence. Inside the app this is tiny.store, which writes
// ~/Library/Application Support/<app id>/store.json; in a plain browser it falls back to localStorage.
const KEY = 'state';
const DEFAULTS = () => ({v:1,xp:0,total:0,sessions:0,streak:0,last:null,bosses:0,badges:[],quests:[],foe:0,nextId:1,len:25});
const backend = {
  get: async k => (typeof tiny!=='undefined' && tiny.store) ? tiny.store.get(k) : JSON.parse(localStorage.getItem(k)||'null'),
  set: async (k,v) => (typeof tiny!=='undefined' && tiny.store) ? tiny.store.set(k,v) : localStorage.setItem(k,JSON.stringify(v))
};

let S = DEFAULTS();   // the one game state object; mutate it, then call save()

let pending = Promise.resolve();
// Writes are queued so they land in order and a failed write never breaks the game.
const save = () => {
  const snapshot = JSON.parse(JSON.stringify(S));
  pending = pending.then(() => backend.set(KEY, snapshot)).catch(e => console.error('save failed', e));
  return pending;
};

// Must finish before the first render, otherwise early clicks would overwrite saved progress.
async function load() {
  try {
    const saved = await backend.get(KEY);
    if (saved) S = {...DEFAULTS(), ...saved};   // new fields get defaults, old saves keep working
  } catch (e) { console.error('load failed', e); }
}
