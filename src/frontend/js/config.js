// Static game data and small helpers. No state lives here.
const XP = {1:10, 2:25, 4:60};
const TITLES = ['Procrastinator','Apprentice','Doer','Grinder','Slayer','Champion','Legend','Mythic'];
const NAMES = ['Backlog Beast','Deadline Drake','Inbox Imp','Meeting Maw','Scope Creeper','Bug Basilisk'];
const BADGES = [
  ['first','🗡 First blood',s=>s.total>=1],['ten','🏅 10 quests',s=>s.total>=10],['fifty','👑 50 quests',s=>s.total>=50],
  ['focus','🧘 First round',s=>s.sessions>=1],['five','⏳ 5 rounds',s=>s.sessions>=5],['streak3','🔥 3-day streak',s=>s.streak>=3],
  ['streak7','🌋 7-day streak',s=>s.streak>=7],['boss','💀 Boss slain',s=>s.bosses>=1],['lvl5','⭐ Level 5',s=>lvl(s.xp)>=5]];
const lvl = xp => Math.floor(Math.sqrt(xp/50))+1;
const lvlStart = n => 50*(n-1)*(n-1);
const today = () => new Date().toLocaleDateString('en-CA');
const $ = id => document.getElementById(id);
