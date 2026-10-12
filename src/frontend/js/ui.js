// Draws state into the DOM and wires up the controls.
let active = null;           // quest currently targeted for a round
const seen = new Set();     // quest ids already shown, so only new ones animate
function render(){
  const l = lvl(S.xp), a = lvlStart(l), b = lvlStart(l+1);
  $('lvl').textContent = l; $('title').textContent = TITLES[Math.min(l-1,7)];
  $('xpbar').style.width = (S.xp-a)/(b-a)*100+'%'; $('xptxt').textContent = `${S.xp-a} / ${b-a} XP to level ${l+1}`;
  $('streak').textContent = S.streak; $('total').textContent = S.total; $('sessions').textContent = S.sessions;
  const tot = S.quests.reduce((n,q)=>n+q.size,0), rem = S.quests.reduce((n,q)=>n+(q.done?0:q.size),0);
  const open = S.quests.filter(x=>!x.done).length, dead = S.quests.length>0 && !open, empty = !S.quests.length;
  const name = NAMES[S.foe % NAMES.length];
  BOSS.set(S.foe, tot?rem/tot:1, dead, empty);
  $('bname').textContent = empty ? 'A boss sleeps… add a quest to wake it' : dead ? '💀 '+name+' — DEFEATED' : `${name} · ${open} quest${open>1?'s':''} left`;
  $('hpbar').style.width = (tot? rem/tot*100 : 100)+'%'; $('hptxt').textContent = tot ? `HP ${rem} / ${tot}` : '';
  $('clock').textContent = fmt(timer ? left : len*60);
  $('start').textContent = timer ? '🏳 Give up' : '⚔ Start round'; $('start').classList.toggle('stop',!!timer);
  const aq = S.quests.find(x=>x.id===active);
  $('focusinfo').textContent = timer ? (aq ? '🎯 '+aq.text : 'Click a quest to target it') : combo ? `Combo ×${combo} — chain another round!` : 'Target a quest, then start a round.';
  $('qcount').textContent = open+' open';
  $('list').innerHTML = '';
  for (const q of S.quests.slice().sort((x,y)=>x.done-y.done)) {
    const d = document.createElement('div'); d.className = 'q s'+q.size+(q.done?' done':'')+(q.id===active?' act':'')+(seen.has(q.id)?'':' new');
    seen.add(q.id);
    d.innerHTML = `<input type="checkbox" ${q.done?'checked':''}><span class="t"></span><span class="meta"><span class="chip">${XP[q.size]} xp</span>${q.rounds?`<span class="chip" style="background:none;color:var(--dim)">⏱ ${q.rounds}</span>`:''}</span><button class="x">✕</button>`;
    d.querySelector('.t').textContent = q.text;
    d.querySelector('input').onclick = e => q.done ? undo(q) : complete(q,e);
    d.querySelector('.t').onclick = () => { if(!q.done){active=q.id;render()} };
    d.querySelector('.x').onclick = () => { S.quests = S.quests.filter(x=>x!==q); save(); render(); };
    $('list').append(d);
  }
  if (!S.quests.length) $('list').innerHTML = '<div class="empty">Empty board.<br>Write down everything you need to finish, then watch the boss shrink.</div>';
  $('badges').innerHTML = BADGES.map(([id,l])=>`<span class="${S.badges.includes(id)?'got':''}">${l}</span>`).join('');
}
$('form').onsubmit = e => { e.preventDefault(); const t=$('newq').value.trim(); if(!t) return;
  if (S.quests.length && S.quests.every(x=>x.done)) { S.quests = []; S.foe++; }
  S.quests.push({id:S.nextId++,text:t,size:+$('size').value,done:false}); $('newq').value=''; sfx.add(); save(); render(); };
$('start').onclick = startRound;
document.querySelectorAll('[data-len]').forEach(b=>b.onclick=()=>{ if(timer) return; len=+b.dataset.len; S.len=len; save();
  document.querySelectorAll('[data-len]').forEach(x=>x.classList.toggle('on',x===b)); render(); });
