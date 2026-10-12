// Rules: XP, levels, streaks, trophies, completing quests.
function award(amount, ev){
  const before = lvl(S.xp); S.xp += amount;
  pop('+'+amount+' XP', ev?.clientX ?? innerWidth*.4, ev?.clientY ?? 260);
  if (lvl(S.xp) > before) { toast(`⬆ LEVEL ${lvl(S.xp)} — ${TITLES[Math.min(lvl(S.xp)-1,7)]}!`); sfx.win(); confetti(); }
}
function touchStreak(){
  const t = today(); if (S.last === t) return;
  const y = new Date(Date.now()-864e5).toLocaleDateString('en-CA');
  S.streak = S.last === y ? S.streak+1 : 1; S.last = t;
}
function checkBadges(){
  for (const [id,label,ok] of BADGES) if (!S.badges.includes(id) && ok(S)) { S.badges.push(id); toast('🏆 Trophy: '+label); sfx.win(); }
}
function complete(q, ev){
  q.done = true; S.total++; touchStreak();
  award(XP[q.size] * (1 + Math.min(S.streak,7)*.1) | 0, ev);
  const bossDown = S.quests.every(x=>x.done);
  BOSS.hit(q.size===4); sfx.hit();
  if (active === q.id) active = null;
  if (bossDown) { S.bosses++; award(100); confetti(300); BOSS.win(); sfx.win(); toast('💀 BOSS DEFEATED! Everything is done.'); }
  checkBadges(); save(); render();
}
function undo(q){ q.done = false; save(); render(); }
