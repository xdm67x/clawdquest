// Sound effects (WebAudio beeps), floating +XP, toasts, confetti.
let ac; const beep = (f,d=.12,t=0) => { try{ ac ||= new AudioContext(); const o=ac.createOscillator(),g=ac.createGain();
  o.type='square';o.frequency.value=f;g.gain.setValueAtTime(.07,ac.currentTime+t);g.gain.exponentialRampToValueAtTime(.001,ac.currentTime+t+d);
  o.connect(g);g.connect(ac.destination);o.start(ac.currentTime+t);o.stop(ac.currentTime+t+d);}catch{} };
const sfx = {hit:()=>{beep(180,.1);beep(320,.12,.06)}, win:()=>[523,659,784,1046].forEach((f,i)=>beep(f,.15,i*.09)), add:()=>beep(440,.06)};
function pop(text,x,y){const e=document.createElement('div');e.className='pop';e.textContent=text;e.style.left=x+'px';e.style.top=y+'px';document.body.append(e);setTimeout(()=>e.remove(),1100)}
function toast(t){const e=document.createElement('div');e.className='toast';e.textContent=t;document.body.append(e);setTimeout(()=>e.remove(),2800)}
const cv=$('fx'),cx=cv.getContext('2d');let parts=[];
function confetti(n=140){cv.width=innerWidth;cv.height=innerHeight;const wasEmpty=!parts.length;for(let i=0;i<n;i++)parts.push({x:innerWidth*.4,y:innerHeight/3,vx:(Math.random()-.5)*16,vy:-Math.random()*14-2,c:`hsl(${Math.random()*360},90%,60%)`,l:90});if(wasEmpty)ctick()}
function ctick(){cx.clearRect(0,0,cv.width,cv.height);parts=parts.filter(p=>p.l-->0);for(const p of parts){p.x+=p.vx;p.y+=p.vy;p.vy+=.45;cx.fillStyle=p.c;cx.fillRect(p.x,p.y,7,7)}if(parts.length)requestAnimationFrame(ctick)}
