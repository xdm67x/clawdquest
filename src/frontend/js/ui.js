// ---------------------------------------------------------------------------
// Draws state into the DOM and wires up the controls.
// ---------------------------------------------------------------------------

let active = null; // id of the quest currently targeted for a round
const seen = new Set(); // quest ids already rendered once; only new ones animate in

// Repaint everything from S. Called after every state change.
function render() {
  // --- Hero panel: level, title, XP bar, stats --------------------------------

  const l = lvl(S.xp);
  const a = lvlStart(l);      // XP at the start of the current level
  const b = lvlStart(l + 1);  // XP at the start of the next level

  $('lvl').textContent = l;
  $('title').textContent = TITLES[Math.min(l - 1, 7)];
  $('xpbar').style.width = (S.xp - a) / (b - a) * 100 + '%';
  $('xptxt').textContent = `${S.xp - a} / ${b - a} XP to level ${l + 1}`;
  $('streak').textContent = S.streak;
  $('total').textContent = S.total;
  $('time').textContent = fmtTime(S.time || 0);

  // --- Boss bar: HP = open quests over total -----------------------------------

  const tot = S.quests.length;
  const rem = S.quests.filter(x => !x.done).length;
  const dead = S.quests.length > 0 && !rem; // board full and all done
  const empty = !S.quests.length;           // no quests at all
  const name = NAMES[S.foe % NAMES.length];

  BOSS.set(S.foe, tot ? rem / tot : 1, dead, empty);
  $('bname').textContent = empty
    ? 'A boss sleeps… add a quest to wake it'
    : dead
      ? '💀 ' + name + ' — DEFEATED'
      : `${name} · ${rem} quest${rem > 1 ? 's' : ''} left`;
  $('hpbar').style.width = (tot ? rem / tot * 100 : 100) + '%';
  $('hptxt').textContent = tot ? `HP ${rem} / ${tot}` : '';

  // --- Quest list ------------------------------------------------------------------

  // Open quests stack directly above the capture bar (align-content:end
  // keeps them glued to it when the list is shorter than its max height).
  $('list').innerHTML = '';
  // Done quests sink to the bottom; order is otherwise preserved.
  for (const q of S.quests.slice().sort((x, y) => x.done - y.done)) {
    const isRun = q.id === active && running; // this quest's clock is ticking
    const secs = q.secs || 0;
    const d = document.createElement('div');
    d.title = q.text + (q.done ? '' : ` — click to ${isRun ? 'pause' : 'start'}`);
    d.className = 'q'
      + (q.done ? ' done' : '')
      + (q.id === active ? ' act' : '')
      + (seen.has(q.id) ? '' : ' new'); // "new" class triggers the pop-in animation once
    seen.add(q.id);
    // The chip shows the quest's chronometer: highlighted while running,
    // dim once paused or accumulated from an earlier session.
    d.innerHTML = `
      <input type="checkbox" ${q.done ? 'checked' : ''}>
      <span class="t"></span>
      <span class="meta">
        ${(isRun || secs) ? `<span class="chip ${isRun ? 'run' : 'mult'}">${fmt(secs)}</span>` : ''}
      </span>
      <button class="x">✕</button>`;
    d.querySelector('.t').textContent = q.text; // via textContent: user text can never inject HTML

    d.querySelector('input').onclick = e => (q.done ? undo(q) : complete(q, e));
    // Click a quest to start its chronometer; click the running one to pause;
    // click another to switch the clock over to it.
    d.querySelector('.t').onclick = () => {
      if (q.done) return;
      if (isRun) running = false;
      else { active = q.id; running = true; }
      render();
    };
    d.querySelector('.x').onclick = () => {
      S.quests = S.quests.filter(x => x !== q);
      save();
      render();
    };
    $('list').append(d);
  }
  if (!S.quests.length) {
    $('list').innerHTML =
      '<div class="empty">Empty board.<br>Capture your first quest below to wake the boss.</div>';
  }
}

// --- Controls ------------------------------------------------------------------------

// Add a quest. If the previous board was fully cleared, start a fresh board
// and wake the next boss.
$('form').onsubmit = e => {
  e.preventDefault();
  const t = $('newq').value.trim();
  if (!t) return;
  if (S.quests.length && S.quests.every(x => x.done)) {
    S.quests = [];
    S.foe++;
  }
  S.quests.push({ id: S.nextId++, text: t, done: false, secs: 0 });
  $('newq').value = '';
  sfx.add();
  save();
  render();
  $('newq').focus(); // ready for the next capture
};
