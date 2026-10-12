// ---------------------------------------------------------------------------
// The three.js scene: arena, procedural monsters, hit and death reactions.
//
// Everything lives inside one IIFE and is exposed as a tiny public API:
//   BOSS.set(foeIndex, hpFraction, dead, dormant) — sync with game state
//   BOSS.hit(big)  — big = heavy hit (round finish / big quest)
//   BOSS.win()     — board cleared celebration
// ---------------------------------------------------------------------------

const BOSS = (() => {
  const stage = $('stage');
  const canvas = $('gl');

  // --- Renderer, camera, scene ---------------------------------------------

  const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true });
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2));

  const scene = new THREE.Scene();
  scene.fog = new THREE.FogExp2(0x120c26, 0.045);

  const cam = new THREE.PerspectiveCamera(42, 1, 0.1, 100);
  cam.position.set(0, 0.4, 9);

  // --- Lights ---------------------------------------------------------------

  scene.add(new THREE.HemisphereLight(0xb9a4ff, 0x1a0f33, 0.9));

  const key = new THREE.DirectionalLight(0xffffff, 1.3);
  key.position.set(4, 6, 6);
  scene.add(key);

  const rim = new THREE.PointLight(0xff4d9d, 2.2, 30);
  rim.position.set(-6, 2, -3);
  scene.add(rim);

  const rim2 = new THREE.PointLight(0x4da6ff, 1.8, 30);
  rim2.position.set(6, -1, -2);
  scene.add(rim2);

  // --- Starfield ------------------------------------------------------------

  // 300 random points (900 floats) far behind the arena.
  const sg = new THREE.BufferGeometry();
  const sp = new Float32Array(900);
  for (let i = 0; i < 900; i += 3) {
    sp[i] = (Math.random() - 0.5) * 40;
    sp[i + 1] = (Math.random() - 0.5) * 24;
    sp[i + 2] = -6 - Math.random() * 14;
  }
  sg.setAttribute('position', new THREE.BufferAttribute(sp, 3));
  const stars = new THREE.Points(
    sg,
    new THREE.PointsMaterial({ color: 0xcbb8ff, size: 0.07, transparent: true, opacity: 0.8 })
  );
  scene.add(stars);

  // --- Arena ----------------------------------------------------------------

  // Purple floor ring, a faint larger pink ring around it, and a soft disc inside.
  const ring = new THREE.Mesh(
    new THREE.TorusGeometry(2.6, 0.04, 12, 80),
    new THREE.MeshBasicMaterial({ color: 0x8b5cf6 })
  );
  ring.rotation.x = Math.PI / 2;
  ring.position.y = -2.1;
  scene.add(ring);

  const ring2 = ring.clone();
  ring2.scale.setScalar(1.35);
  ring2.material = new THREE.MeshBasicMaterial({ color: 0xec4899, transparent: true, opacity: 0.5 });
  scene.add(ring2);

  const disc = new THREE.Mesh(
    new THREE.CircleGeometry(2.6, 64),
    new THREE.MeshBasicMaterial({ color: 0x8b5cf6, transparent: true, opacity: 0.12 })
  );
  disc.rotation.x = -Math.PI / 2;
  disc.position.y = -2.1;
  scene.add(disc);

  // --- Hit sparks -----------------------------------------------------------

  // A fixed pool of N particles. Dead particles are parked at y = -99
  // and reused on each burst instead of allocating new geometry.
  const N = 160;
  const pg = new THREE.BufferGeometry();
  const pPos = new Float32Array(N * 3);
  const pVel = new Float32Array(N * 3);
  const pLife = new Float32Array(N);
  for (let i = 0; i < N; i++) pPos[i * 3 + 1] = -99;
  pg.setAttribute('position', new THREE.BufferAttribute(pPos, 3));

  const pm = new THREE.PointsMaterial({
    color: 0xffd23f,
    size: 0.16,
    transparent: true,
    depthWrite: false,
    blending: THREE.AdditiveBlending,
  });
  const sparks = new THREE.Points(pg, pm);
  sparks.frustumCulled = false; // particles move every frame; never cull
  scene.add(sparks);

  // Wake up to n particles from the pool, launching them outward from the boss.
  function burst(n = 60, col = 0xffd23f) {
    pm.color.setHex(col);
    for (let i = 0, c = 0; i < N && c < n; i++) {
      if (pLife[i] <= 0) {
        c++;
        pLife[i] = 1;
        pPos.set([0, 0.3, 0.5], i * 3); // boss chest position
        const a = Math.random() * 6.28;            // horizontal angle
        const b = Math.random() * 3.14 - 1.2;      // vertical angle
        const s = 2 + Math.random() * 5;           // speed
        pVel.set(
          [Math.cos(a) * s * Math.cos(b), Math.sin(b) * s + 2, Math.sin(a) * s * Math.cos(b)],
          i * 3
        );
      }
    }
  }

  // --- Procedural monster ---------------------------------------------------

  // [body color, eye color] pairs, one per boss. Each boss also gets
  // slightly different geometry via its index i (horn count, eye count...).
  const PAL = [
    [0x7c3aed, 0xffd23f],
    [0xdc2626, 0x7cff6b],
    [0x0891b2, 0xff4d9d],
    [0x16a34a, 0xffb703],
    [0xdb2777, 0x6be3ff],
    [0xea580c, 0xc084fc],
  ];

  let boss = null; // the live boss record, see shape below
  let built = -1;  // index of the boss currently on stage

  // Build boss number i and put it on stage. Called only when the index changes.
  function build(i) {
    if (boss) scene.remove(boss.g);

    const [bc, ec] = PAL[i % PAL.length];
    const g = new THREE.Group();
    const flat = { flatShading: true, roughness: 0.55, metalness: 0.15 };

    // Body: a sphere warped by a sine field, so each boss has a unique lumpiness.
    const bodyG = new THREE.IcosahedronGeometry(1.5, 2);
    const p = bodyG.attributes.position;
    const v = new THREE.Vector3();
    for (let k = 0; k < p.count; k++) {
      v.fromBufferAttribute(p, k);
      const n = 1 + 0.12 * Math.sin(v.x * 3 + i) * Math.cos(v.y * 3 + v.z * 2);
      v.multiplyScalar(n);
      v.y *= 0.95 + (i % 3) * 0.06; // slight per-boss squash
      p.setXYZ(k, v.x, v.y, v.z);
    }
    bodyG.computeVertexNormals();

    const bodyM = new THREE.MeshStandardMaterial({ color: bc, emissive: 0x000000, ...flat });
    const body = new THREE.Mesh(bodyG, bodyM);
    g.add(body);

    // Horns on the head: 2, 4 or 6 depending on the boss index,
    // spread evenly from left (-1) to right (+1).
    const hornM = new THREE.MeshStandardMaterial({ color: 0xf5e9d0, ...flat });
    const horns = 2 + (i % 3) * 2;
    for (let h = 0; h < horns; h++) {
      const t = horns === 2 ? (h ? 1 : -1) : (h / (horns - 1)) * 2 - 1;
      const c = new THREE.Mesh(
        new THREE.ConeGeometry(0.2, 1.1 + (1 - Math.abs(t)) * 0.6, 7),
        hornM
      );
      c.position.set(t * 1.0, 1.25 - Math.abs(t) * 0.35, -0.1);
      c.rotation.z = -t * 0.55; // tilt outward
      g.add(c);
    }

    // Spikes along the chin.
    for (let s = 0; s < 7; s++) {
      const c = new THREE.Mesh(new THREE.ConeGeometry(0.16, 0.5, 5), hornM);
      const a = (s / 6) * Math.PI - Math.PI / 2;
      c.position.set(Math.sin(a) * 1.4, 0.2 + Math.cos(a) * 0.5 - 0.5, -1.1);
      c.rotation.set(-1.2, 0, -a * 0.6);
      g.add(c);
    }

    // Eyes: glowing, with a pupil that tracks the pointer, and an angry brow.
    // Bosses alternate between two and three eyes.
    const eyeM = new THREE.MeshStandardMaterial({ color: ec, emissive: ec, emissiveIntensity: 1.6 });
    const eyes = [];
    const eyeN = i % 3 === 1 ? 3 : 2;
    for (let e = 0; e < eyeN; e++) {
      const x = eyeN === 2 ? (e ? 0.55 : -0.55) : (e - 1) * 0.55;
      const y = eyeN === 3 && e === 1 ? 0.78 : 0.45;
      const eg = new THREE.Group();
      eg.position.set(x, y, 1.28);
      const ball = new THREE.Mesh(new THREE.SphereGeometry(0.3, 20, 16), eyeM);
      const pu = new THREE.Mesh(new THREE.SphereGeometry(0.12, 12, 10), new THREE.MeshBasicMaterial({ color: 0x100018 }));
      pu.position.z = 0.24;
      const brow = new THREE.Mesh(new THREE.BoxGeometry(0.6, 0.1, 0.12), new THREE.MeshStandardMaterial({ color: 0x14081f }));
      brow.position.set(0, 0.34, 0.15);
      brow.rotation.z = (x < 0 ? -1 : 1) * -0.4; // slanted, angrier
      if (eyeN === 3 && e === 1) brow.rotation.z = 0;
      eg.add(ball, pu, brow);
      g.add(eg);
      eyes.push({ eg, pu });
    }

    // Mouth with teeth.
    const mouth = new THREE.Mesh(
      new THREE.BoxGeometry(1.4, 0.38, 0.3),
      new THREE.MeshStandardMaterial({ color: 0x10000a, emissive: 0x330010 })
    );
    mouth.position.set(0, -0.55, 1.35);
    g.add(mouth);
    for (let t = 0; t < 6; t++) {
      const tooth = new THREE.Mesh(new THREE.ConeGeometry(0.08, 0.22, 4), hornM);
      tooth.position.set(-0.55 + t * 0.22, -0.42, 1.5);
      tooth.rotation.x = Math.PI; // point downward
      g.add(tooth);
    }

    // Arms and fists, one per side.
    for (const sd of [-1, 1]) {
      const arm = new THREE.Mesh(new THREE.CapsuleGeometry(0.28, 0.9, 6, 10), bodyM);
      arm.position.set(sd * 1.65, -0.5, 0.3);
      arm.rotation.z = sd * 0.9;
      g.add(arm);
      const fist = new THREE.Mesh(new THREE.IcosahedronGeometry(0.38, 1), bodyM);
      fist.position.set(sd * 2.15, -1.1, 0.5);
      g.add(fist);
    }

    // Start tiny; the animation loop scales it up to full size.
    g.scale.setScalar(0.001);
    scene.add(g);

    boss = {
      g,       // the three.js group
      body,    // body mesh (unused directly, kept for clarity)
      bodyM,   // body material — emissive used for hit flash
      eyeM,    // eye material — emissive used for rage glow
      eyes,    // eye groups; each pupil tracks the pointer
      mouth,   // mouth mesh — scales up when raging or hit
      hp: 1,   // 0..1 fraction, synced by BOSS.set()
      dead: false,
      dormant: false, // true when no quest is on the board yet
      punch: 0,       // 0..1 hit recoil, decays over time
      flash: 0,       // 0..1 white flash on the body, decays over time
      spin: 0,        // extra spin speed while dying
      t: 0,           // seconds since this boss was built
    };
    built = i;
  }

  // --- Pointer tracking -----------------------------------------------------

  // The boss turns its head and eyes toward the cursor.
  // look.x / look.y are normalized to -1..1 within the stage bounds.
  const look = { x: 0, y: 0 };
  addEventListener('pointermove', e => {
    const r = stage.getBoundingClientRect();
    look.x = Math.max(-1, Math.min(1, ((e.clientX - r.left) / r.width - 0.5) * 2));
    look.y = Math.max(-1, Math.min(1, ((e.clientY - r.top) / r.height - 0.5) * 2));
  });

  // --- Resize ----------------------------------------------------------------

  let shake = 0; // camera shake strength, decays over time
  let last = performance.now();

  // Keep the renderer size in sync with the stage element.
  // On narrow windows, pull the camera back so the boss still fits.
  function resize() {
    const w = stage.clientWidth;
    const h = stage.clientHeight;
    if (!w || !h) return;
    renderer.setSize(w, h, false);
    cam.aspect = w / h;
    cam.position.z = w / h < 1.1 ? 11 : 9;
    cam.updateProjectionMatrix();
  }
  new ResizeObserver(resize).observe(stage);
  resize();

  // --- Animation loop --------------------------------------------------------

  function frame(now) {
    const dt = Math.min((now - last) / 1000, 0.05); // clamp: no jumps after a stall
    last = now;
    const t = now / 1000;

    // Ambient motion: slow star drift, spinning rings, pulsing outer ring.
    stars.rotation.y = t * 0.01;
    ring.rotation.z = t * 0.4;
    ring2.rotation.z = -t * 0.25;
    ring2.material.opacity = 0.35 + 0.15 * Math.sin(t * 2);

    if (boss) {
      const b = boss;
      b.t += dt;

      // Target size: shrink to nothing when dead, shrink a little when
      // dormant, otherwise stay full size and puff up as HP drops.
      const tgt = b.dead ? 0.001 : (b.dormant ? 0.7 : 0.85 + 0.15 * b.hp);
      const cur = b.g.scale.x;
      const s = cur + (tgt - cur) * Math.min(1, dt * (b.dead ? 3 : 5)) + b.punch * 0.06;
      b.g.scale.setScalar(Math.max(s, 0.001));

      // Gentle bobbing; sink below the floor when dead.
      const bob = b.dead ? -3 : Math.sin(t * 1.6) * 0.18 - 0.1;
      b.g.position.y += (bob - b.g.position.y) * Math.min(1, dt * (b.dead ? 2 : 6));

      // Turn toward the cursor, spin fast while dying.
      b.spin += ((b.dead ? 9 : 0) - b.spin) * dt * 2;
      b.g.rotation.y += (look.x * 0.5 - b.g.rotation.y) * dt * 4 + b.spin * dt;
      b.g.rotation.x += (look.y * 0.25 - b.g.rotation.x) * dt * 4;
      b.g.rotation.z = Math.sin(t * 1.1) * 0.03 + b.punch * 0.12 * Math.sin(t * 60);

      // Rage grows as HP drops; punch/flash decay after each hit.
      const rage = 1 - b.hp;
      b.punch = Math.max(0, b.punch - dt * 3);
      b.flash = Math.max(0, b.flash - dt * 3.5);
      b.eyeM.emissiveIntensity =
        b.dormant || b.dead ? 0.1 : 1.2 + rage * 1.8 + Math.sin(t * 5) * 0.3 * rage;
      b.bodyM.emissive.setRGB(b.flash, b.flash * 0.05 + rage * 0.05, b.flash * 0.05);
      b.mouth.scale.y = 1 + (b.dead ? 0 : Math.sin(t * 3) * 0.25 + rage * 0.8) + b.punch * 1.2;

      // Pupils follow the cursor.
      for (const e of b.eyes) {
        e.pu.position.x += (look.x * 0.1 - e.pu.position.x) * dt * 6;
        e.pu.position.y += (-look.y * 0.08 - e.pu.position.y) * dt * 6;
      }
    }

    // Sparks: simple gravity sim over the fixed pool.
    const pa = pg.attributes.position;
    let alive = false;
    for (let i = 0; i < N; i++) {
      if (pLife[i] > 0) {
        alive = true;
        pLife[i] -= dt * 0.9;
        pVel[i * 3 + 1] -= 9 * dt; // gravity
        for (let k = 0; k < 3; k++) pPos[i * 3 + k] += pVel[i * 3 + k] * dt;
        if (pLife[i] <= 0) pPos.set([0, -99, 0], i * 3); // park the dead particle
      }
    }
    pa.needsUpdate = true;
    pm.opacity = alive ? 1 : 0;

    // Camera shake on every hit, decaying back to center.
    shake = Math.max(0, shake - dt * 3);
    cam.position.x = (Math.random() - 0.5) * shake * 0.4;
    cam.position.y = 0.4 + (Math.random() - 0.5) * shake * 0.4;
    cam.lookAt(0, -0.1, 0);

    renderer.render(scene, cam);
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);

  // --- Public API -------------------------------------------------------------

  return {
    // Sync the scene with game state. Rebuilds the monster only when the
    // boss index changes; otherwise just updates hp/dead/dormant in place.
    set(foe, hp, dead, dormant) {
      if (built !== foe) build(foe);
      Object.assign(boss, { hp, dead, dormant });
    },
    // Heavy hits (long-worked quests, chronometer finishes) shake harder and burst red.
    hit(big) {
      if (!boss) return;
      boss.punch = 1;
      boss.flash = 1;
      shake = big ? 1.6 : 0.9;
      burst(big ? 100 : 55, big ? 0xff4d6d : 0xffd23f);
    },
    // Defeat celebration: green burst and the strongest shake.
    win() {
      burst(150, 0x3ddc97);
      shake = 1.8;
    },
  };
})();
