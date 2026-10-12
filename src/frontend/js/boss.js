// ---------------------------------------------------------------------------
// The stage: the three.js arena (stars, rings, hit sparks) around Clawd —
// Claude Code — rendered as a big 3D pixel-art figure: pixel maps matching
// the clawd-on-desk pet, extruded into chunky voxels, standing on the disc.
//
// Pixel-art versions, one per game state:
//   doze  — the board is empty: shut-lid eyes, he sleeps until a quest wakes him
//   wake  — short cameo when the first quest appears: sparks pop around him
//   idle  — quests on the board, nothing running: tall black eyes
//   work  — a chronometer is running: eyes down on the code, typing jitter
//   happy — the board was just cleared: grin with a tooth, jumping
//   dizzy — defeated: sparks orbit his head while he spins, until the next
//           quest arrives
//
// Public API:
//   BOSS.set(foe, hpFraction, dead, dormant) — sync with game state
//   BOSS.hit(big) — big = heavy hit (long-worked quest)
//   BOSS.win()    — board cleared celebration
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
  // Clawd stands in the middle, feet on the disc.
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

  // Wake up to n particles from the pool, launching them outward from the middle.
  function burst(n = 60, col = 0xffd23f) {
    pm.color.setHex(col);
    for (let i = 0, c = 0; i < N && c < n; i++) {
      if (pLife[i] <= 0) {
        c++;
        pLife[i] = 1;
        pPos.set([0, 0, 1], i * 3); // just in front of Clawd's face
        const a = Math.random() * 6.28;        // horizontal angle
        const b = Math.random() * 3.14 - 1.2;  // vertical angle
        const s = 2 + Math.random() * 5;        // speed
        pVel.set(
          [Math.cos(a) * s * Math.cos(b), Math.sin(b) * s + 2, Math.sin(a) * s * Math.cos(b)],
          i * 3
        );
      }
    }
  }

  // --- Clawd: 3D pixel-art voxels ---------------------------------------------

  // Clawd is the boxy coral pet from clawd-on-desk, redrawn as pixel maps:
  // a wide 11x7 torso, stubby 2x2 side arms, four 1px legs, and two tall
  // 1x2 black eyes. One character per pixel; rows are bottom-aligned when
  // built, so his feet always land on the arena disc.
  const PALETTE = {
    C: 0xde886d, // coral body (the pet's own body color)
    E: 0x1a0e08, // eyes / shut lids
    M: 0x2b120a, // mouth
    W: 0xfff3e8, // the one tooth in the happy grin
    S: 0xffbd2e, // floating sparks (wake / dizzy)
  };
  // Extrusion depth per pixel type, in voxels: the body is chunky, the face
  // sticks out in front, sparks float as thin chips.
  const DEPTH = { C: 4, E: 6, M: 6, W: 6, S: 1 };

  // Body building blocks (15 columns wide):
  //   torso occupies cols 2-12, arms cols 0-1 / 13-14, legs cols 3/5/9/11,
  //   eyes sit at cols 4 and 10 (rows 2-3 of the body).
  const T = '..CCCCCCCCCCC..';           // plain torso row
  const EYES = '..CCECCCCCECC..';         // torso + eye tops
  const ARMS_EYES = 'CCCCECCCCCECCCC';   // arms + eye bottoms
  const ARMS = 'CCCCCCCCCCCCCCC';         // arms + plain torso
  const GRIN = 'CCCCCCMWMCCCCCC';          // arms + grin (mouth-tooth-mouth)
  const MOUTH = 'CCCCCCCMCCCCCCC';        // arms + tiny mouth (doze)
  const LEGS = '...C.C...C.C...';         // four legs
  const SPARKS_WIDE = '.S...........S.';  // wake: sparks either side of the head
  const SPARKS_AROUND = '...S.......S...'; // dizzy: sparks orbiting
  const BLANK = '...............';

  const MAPS = {
    idle: [T, T, EYES, ARMS_EYES, ARMS, T, T, LEGS, LEGS],
    work: [T, T, T, ARMS_EYES, ARMS, T, T, LEGS, LEGS], // eyes down: half-height
    doze: [T, T, T, ARMS_EYES, MOUTH, T, T, LEGS, LEGS], // lids shut, tiny mouth
    wake: [SPARKS_WIDE, BLANK, T, T, EYES, ARMS_EYES, ARMS, T, T, LEGS, LEGS],
    happy: [T, T, EYES, ARMS_EYES, GRIN, T, T, LEGS, LEGS],
    dizzy: [SPARKS_AROUND, BLANK, T, T, EYES, ARMS_EYES, ARMS, T, T, LEGS, LEGS],
  };

  const V = 0.46;  // voxel edge; body is ~6.9 wide and ~4.1 tall — big
  const BASE_Y = -2.05; // feet height: right on the arena disc

  // Build one version as an InstancedMesh: one box per non-empty pixel,
  // extruded by its pixel type's depth. Maps are bottom-aligned.
  const voxMat = new THREE.MeshStandardMaterial({ flatShading: true, roughness: 0.55, metalness: 0.1 });
  function buildVariant(map) {
    const vox = [];
    for (let r = 0; r < map.length; r++) {
      for (let c = 0; c < map[r].length; c++) {
        const ch = map[r][c];
        if (ch !== '.') vox.push({ r, c, ch });
      }
    }
    const mesh = new THREE.InstancedMesh(new THREE.BoxGeometry(V, V, V), voxMat, vox.length);
    const rows = map.length;
    const m = new THREE.Matrix4();
    const q = new THREE.Quaternion();
    const col = new THREE.Color();
    const pos = new THREE.Vector3();
    const scl = new THREE.Vector3();
    vox.forEach((v, i) => {
      pos.set((v.c - 7) * V, (rows - 1 - v.r) * V + BASE_Y, 0);
      scl.set(1, 1, DEPTH[v.ch]);
      m.compose(pos, q, scl);
      mesh.setMatrixAt(i, m);
      mesh.setColorAt(i, col.setHex(PALETTE[v.ch]));
    });
    mesh.instanceColor.needsUpdate = true;
    return mesh;
  }

  const root = new THREE.Group(); // animated; the variant meshes swap inside it
  scene.add(root);
  const variants = {};
  for (const [name, map] of Object.entries(MAPS)) {
    variants[name] = buildVariant(map);
    variants[name].visible = false;
    root.add(variants[name]);
  }

  // --- Variant state machine --------------------------------------------------

  let shown = 'doze';
  variants.doze.visible = true;
  let revert = null; // pending timer that ends a cameo variant
  let popT = 1;      // 0..1 pop-in progress on the current variant
  let phase = 0;     // seconds since the current variant was shown
  let punch = 0;     // 0..1 hit squash, decays over time
  let spinAcc = 0;  // accumulated spin (dizzy)
  let hpScale = 1;  // shrinks slightly as HP drops
  let deadFlag = false;

  // Per-variant motion: bob amplitude/frequency, tilt, jump, spin speed.
  const MOTION = {
    idle:  { bobA: 0.06, bobF: 1.6, tilt: 0.02, jumpF: 0, spin: 0 },
    work:  { bobA: 0.04, bobF: 6.0, tilt: 0.04, jumpF: 0, spin: 0 }, // typing jitter
    doze:  { bobA: 0.05, bobF: 0.8, tilt: 0.06, jumpF: 0, spin: 0 }, // sleepy sway
    wake:  { bobA: 0.03, bobF: 2.0, tilt: 0.04, jumpF: 3, spin: 0 },
    happy: { bobA: 0.02, bobF: 1.0, tilt: 0.03, jumpF: 5, spin: 0 }, // jumping
    dizzy: { bobA: 0.04, bobF: 2.0, tilt: 0.08, jumpF: 0, spin: 2.6 }, // KO spin
  };

  // Swap the displayed variant, restarting its pop-in.
  function show(name) {
    if (shown === name) return;
    variants[shown].visible = false;
    shown = name;
    variants[name].visible = true;
    popT = 0;
    phase = 0;
  }

  // Show a cameo variant (happy, wake) for a moment, then fall back to the
  // steady variant given in `then`.
  function cameo(name, then, ms) {
    if (revert) clearTimeout(revert);
    show(name);
    revert = setTimeout(() => {
      revert = null;
      show(then);
    }, ms);
  }

  // --- Resize ----------------------------------------------------------------

  let shake = 0; // camera shake strength, decays over time
  let last = performance.now();

  // Keep the renderer size in sync with the stage element.
  // On narrow windows, pull the camera back so the arena still fits.
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

    // Clawd: pop-in scale on variant swap, hit squash, per-variant motion.
    // Voxel positions are baked in world space (feet on the disc), so the
    // root only carries the motion offsets.
    phase += dt;
    const M = MOTION[shown];
    popT = Math.min(1, popT + dt * 4);
    const pop = 1 - Math.pow(1 - popT, 3); // ease-out cubic
    punch = Math.max(0, punch - dt * 3);
    const sy = pop * (1 - punch * 0.15) * hpScale;
    const sxz = pop * (1 + punch * 0.12) * hpScale;
    root.scale.set(sxz, sy, sxz);

    let y = Math.sin(phase * M.bobF) * M.bobA;
    if (M.jumpF) y += Math.abs(Math.sin(phase * M.jumpF)) * 0.35;
    if (deadFlag) y -= 0.3; // slumps down while KO'd
    root.position.y += (y - root.position.y) * Math.min(1, dt * 6);
    root.rotation.z = Math.sin(phase * 1.1) * M.tilt + punch * 0.08 * Math.sin(phase * 40);
    spinAcc += M.spin * dt;
    root.rotation.y = spinAcc + Math.sin(phase * 0.5) * 0.1; // sway shows the 3D sides

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
    // Sync Clawd with the game state. He shrinks slightly as his HP drops;
    // the version follows the board and the chronometer.
    set(foe, hp, dead, dormant) {
      hpScale = 0.8 + 0.2 * hp;
      deadFlag = dead;
      if (revert) return; // a cameo (wake/happy) is playing out

      const steady = dead ? 'dizzy'
        : dormant ? 'doze'
        : running && active ? 'work'
        : 'idle';
      if (shown === 'doze' && steady !== 'doze') cameo('wake', steady, 1800); // waking up
      else show(steady);
    },
    // Heavy hits (long-worked quests) shake harder and burst red.
    hit(big) {
      punch = 1;
      shake = big ? 1.6 : 0.9;
      burst(big ? 100 : 55, big ? 0xff4d6d : 0xffd23f);
    },
    // Defeat celebration: happy for a moment, then he stays dizzy.
    win() {
      cameo('happy', 'dizzy', 2200);
      burst(150, 0x3ddc97);
      shake = 1.8;
    },
  };
})();
