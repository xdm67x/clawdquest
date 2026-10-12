// ---------------------------------------------------------------------------
// Feedback: sound effects, floating +XP, toasts, confetti.
// ---------------------------------------------------------------------------

// --- Sound (WebAudio beeps) ---------------------------------------------------

// AudioContext is created lazily on the first beep (browsers require a user
// gesture first anyway), and any failure is silently ignored: sound is a
// nice-to-have and must never break the game.
let ac;

// Play a short square-wave beep at frequency f, lasting d seconds,
// starting t seconds from now.
const beep = (f, d = 0.12, t = 0) => {
  try {
    ac ||= new AudioContext();
    const o = ac.createOscillator();
    const g = ac.createGain();
    o.type = 'square';
    o.frequency.value = f;
    g.gain.setValueAtTime(0.07, ac.currentTime + t);
    g.gain.exponentialRampToValueAtTime(0.001, ac.currentTime + t + d); // fade out
    o.connect(g);
    g.connect(ac.destination);
    o.start(ac.currentTime + t);
    o.stop(ac.currentTime + t + d);
  } catch {}
};

const sfx = {
  hit: () => { beep(180, 0.1); beep(320, 0.12, 0.06); },       // low thump
  win: () => [523, 659, 784, 1046].forEach((f, i) => beep(f, 0.15, i * 0.09)), // rising arpeggio
  add: () => beep(440, 0.06),                                  // quest added
};

// --- Floating "+XP" text -------------------------------------------------------

// Spawn a small label at the click position that fades via CSS animation
// and removes itself after 1.1s.
function pop(text, x, y) {
  const e = document.createElement('div');
  e.className = 'pop';
  e.textContent = text;
  e.style.left = x + 'px';
  e.style.top = y + 'px';
  document.body.append(e);
  setTimeout(() => e.remove(), 1100);
}

// Same idea for one-line notifications at the top of the screen.
function toast(t) {
  const e = document.createElement('div');
  e.className = 'toast';
  e.textContent = t;
  document.body.append(e);
  setTimeout(() => e.remove(), 2800);
}

// --- Confetti ------------------------------------------------------------------

// Full-screen 2D canvas. Particles live in a plain array; each tick moves
// them with gravity and drops dead ones from the array.
const cv = $('fx');
const cx = cv.getContext('2d');
let parts = [];

// Add n confetti squares launched from the upper middle of the screen.
// The animation loop only starts when the array was empty.
function confetti(n = 140) {
  cv.width = innerWidth;
  cv.height = innerHeight;
  const wasEmpty = !parts.length;
  for (let i = 0; i < n; i++) {
    parts.push({
      x: innerWidth * 0.4,
      y: innerHeight / 3,
      vx: (Math.random() - 0.5) * 16,
      vy: -Math.random() * 14 - 2,
      c: `hsl(${Math.random() * 360},90%,60%)`,
      l: 90, // lifetime in frames
    });
  }
  if (wasEmpty) ctick();
}

function ctick() {
  cx.clearRect(0, 0, cv.width, cv.height);
  parts = parts.filter(p => p.l-- > 0);
  for (const p of parts) {
    p.x += p.vx;
    p.y += p.vy;
    p.vy += 0.45; // gravity
    cx.fillStyle = p.c;
    cx.fillRect(p.x, p.y, 7, 7);
  }
  if (parts.length) requestAnimationFrame(ctick);
}
