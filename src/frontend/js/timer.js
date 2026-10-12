// Focus rounds.
let len = 25, timer = null, left = 0, combo = 0;
function fmt(s){return String(s/60|0).padStart(2,'0')+':'+String(s%60).padStart(2,'0')}
function startRound(){
  if (timer) { clearInterval(timer); timer = null; left = 0; combo = 0; return render(); }
  left = len*60; sfx.add();
  timer = setInterval(()=>{ if(--left<=0) finishRound(); render(); },1000); render();
}
function finishRound(){
  clearInterval(timer); timer=null; combo++; S.sessions++; touchStreak();
  const bonus = Math.round(len*1.2) + (combo-1)*10;
  award(bonus); sfx.win(); confetti(80); BOSS.hit(true);
  const q = S.quests.find(x=>x.id===active && !x.done);
  if (q) q.rounds=(q.rounds||0)+1;
  const open = S.quests.filter(x=>!x.done).length;
  const msg = open ? `Round done! +${bonus} XP (combo ×${combo}). ${open} quest${open>1?'s':''} left.` : 'Round done! Add the next quest.';
  toast(msg); try{tiny.notify('Round complete', msg)}catch{}
  checkBadges(); save(); render();
}
