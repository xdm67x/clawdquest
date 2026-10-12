// The three.js scene: arena, procedural monsters, hit/death reactions. Exposes BOSS.{set,hit,win}.
const BOSS = (() => {
  const stage = $('stage'), canvas = $('gl');
  const renderer = new THREE.WebGLRenderer({canvas, antialias:true, alpha:true});
  renderer.setPixelRatio(Math.min(devicePixelRatio,2));
  const scene = new THREE.Scene(); scene.fog = new THREE.FogExp2(0x120c26, .045);
  const cam = new THREE.PerspectiveCamera(42,1,.1,100); cam.position.set(0,.4,9);
  scene.add(new THREE.HemisphereLight(0xb9a4ff,0x1a0f33,.9));
  const key = new THREE.DirectionalLight(0xffffff,1.3); key.position.set(4,6,6); scene.add(key);
  const rim = new THREE.PointLight(0xff4d9d,2.2,30); rim.position.set(-6,2,-3); scene.add(rim);
  const rim2 = new THREE.PointLight(0x4da6ff,1.8,30); rim2.position.set(6,-1,-2); scene.add(rim2);

  // stars
  const sg = new THREE.BufferGeometry(), sp = new Float32Array(900);
  for (let i=0;i<900;i+=3){ sp[i]=(Math.random()-.5)*40; sp[i+1]=(Math.random()-.5)*24; sp[i+2]=-6-Math.random()*14; }
  sg.setAttribute('position',new THREE.BufferAttribute(sp,3));
  const stars = new THREE.Points(sg,new THREE.PointsMaterial({color:0xcbb8ff,size:.07,transparent:true,opacity:.8}));
  scene.add(stars);

  // arena
  const ring = new THREE.Mesh(new THREE.TorusGeometry(2.6,.04,12,80),new THREE.MeshBasicMaterial({color:0x8b5cf6}));
  ring.rotation.x = Math.PI/2; ring.position.y = -2.1; scene.add(ring);
  const ring2 = ring.clone(); ring2.scale.setScalar(1.35); ring2.material = new THREE.MeshBasicMaterial({color:0xec4899,transparent:true,opacity:.5}); scene.add(ring2);
  const disc = new THREE.Mesh(new THREE.CircleGeometry(2.6,64),new THREE.MeshBasicMaterial({color:0x8b5cf6,transparent:true,opacity:.12}));
  disc.rotation.x = -Math.PI/2; disc.position.y = -2.1; scene.add(disc);

  // hit sparks
  const N = 160, pg = new THREE.BufferGeometry(), pPos = new Float32Array(N*3), pVel = new Float32Array(N*3), pLife = new Float32Array(N);
  for (let i=0;i<N;i++) pPos[i*3+1] = -99;
  pg.setAttribute('position',new THREE.BufferAttribute(pPos,3));
  const pm = new THREE.PointsMaterial({color:0xffd23f,size:.16,transparent:true,opacity:1,depthWrite:false,blending:THREE.AdditiveBlending});
  const sparks = new THREE.Points(pg,pm); sparks.frustumCulled = false; scene.add(sparks);
  function burst(n=60, col=0xffd23f){
    pm.color.setHex(col);
    for (let i=0,c=0;i<N&&c<n;i++) if (pLife[i]<=0){ c++; pLife[i]=1;
      pPos.set([0,.3,.5],i*3); const a=Math.random()*6.28,b=Math.random()*3.14-1.2,s=2+Math.random()*5;
      pVel.set([Math.cos(a)*s*Math.cos(b),Math.sin(b)*s+2,Math.sin(a)*s*Math.cos(b)],i*3); }
  }

  // procedural monster
  const PAL = [[0x7c3aed,0xffd23f],[0xdc2626,0x7cff6b],[0x0891b2,0xff4d9d],[0x16a34a,0xffb703],[0xdb2777,0x6be3ff],[0xea580c,0xc084fc]];
  let boss = null, built = -1;
  function build(i){
    if (boss) scene.remove(boss.g);
    const [bc,ec] = PAL[i%PAL.length], g = new THREE.Group(), flat = {flatShading:true,roughness:.55,metalness:.15};
    const bodyG = new THREE.IcosahedronGeometry(1.5,2), p = bodyG.attributes.position, v = new THREE.Vector3();
    for (let k=0;k<p.count;k++){ v.fromBufferAttribute(p,k); const n=1+.12*Math.sin(v.x*3+i)*Math.cos(v.y*3+v.z*2); v.multiplyScalar(n); v.y*=.95+((i%3)*.06); p.setXYZ(k,v.x,v.y,v.z); }
    bodyG.computeVertexNormals();
    const bodyM = new THREE.MeshStandardMaterial({color:bc,emissive:0x000000,...flat});
    const body = new THREE.Mesh(bodyG,bodyM); g.add(body);
    const hornM = new THREE.MeshStandardMaterial({color:0xf5e9d0,...flat});
    const horns = 2 + (i%3)*2;
    for (let h=0;h<horns;h++){ const t=horns===2?(h?1:-1):(h/(horns-1))*2-1, c=new THREE.Mesh(new THREE.ConeGeometry(.2,1.1+(1-Math.abs(t))*.6,7),hornM);
      c.position.set(t*1.0,1.25-Math.abs(t)*.35,-.1); c.rotation.z=-t*.55; g.add(c); }
    for (let s=0;s<7;s++){ const c=new THREE.Mesh(new THREE.ConeGeometry(.16,.5,5),hornM); const a=s/6*Math.PI-Math.PI/2;
      c.position.set(Math.sin(a)*1.4,.2+Math.cos(a)*.5-.5,-1.1); c.rotation.set(-1.2,0,-a*.6); g.add(c); }
    const eyeM = new THREE.MeshStandardMaterial({color:ec,emissive:ec,emissiveIntensity:1.6});
    const eyes=[]; const eyeN = i%3===1 ? 3 : 2;
    for (let e=0;e<eyeN;e++){ const x = eyeN===2?(e?.55:-.55):(e-1)*.55, y=eyeN===3&&e===1?.78:.45;
      const eg=new THREE.Group(); eg.position.set(x,y,1.28);
      const ball=new THREE.Mesh(new THREE.SphereGeometry(.3,20,16),eyeM); const pu=new THREE.Mesh(new THREE.SphereGeometry(.12,12,10),new THREE.MeshBasicMaterial({color:0x100018})); pu.position.z=.24;
      const brow=new THREE.Mesh(new THREE.BoxGeometry(.6,.1,.12),new THREE.MeshStandardMaterial({color:0x14081f})); brow.position.set(0,.34,.15); brow.rotation.z=(x<0?-1:1)*-.4; if(eyeN===3&&e===1)brow.rotation.z=0;
      eg.add(ball,pu,brow); g.add(eg); eyes.push({eg,pu}); }
    const mouth = new THREE.Mesh(new THREE.BoxGeometry(1.4,.38,.3),new THREE.MeshStandardMaterial({color:0x10000a,emissive:0x330010})); mouth.position.set(0,-.55,1.35); g.add(mouth);
    for (let t=0;t<6;t++){ const tooth=new THREE.Mesh(new THREE.ConeGeometry(.08,.22,4),hornM); tooth.position.set(-.55+t*.22,-.42,1.5); tooth.rotation.x=Math.PI; g.add(tooth); }
    for (const sd of [-1,1]){ const arm=new THREE.Mesh(new THREE.CapsuleGeometry(.28,.9,6,10),bodyM); arm.position.set(sd*1.65,-.5,.3); arm.rotation.z=sd*.9; g.add(arm);
      const fist=new THREE.Mesh(new THREE.IcosahedronGeometry(.38,1),bodyM); fist.position.set(sd*2.15,-1.1,.5); g.add(fist); }
    g.scale.setScalar(.001); scene.add(g);
    boss = {g,body,bodyM,eyeM,eyes,mouth,hp:1,dead:false,dormant:false,punch:0,flash:0,spin:0,t:0};
    built = i;
  }

  const look = {x:0,y:0};
  addEventListener('pointermove',e=>{ const r=stage.getBoundingClientRect(); look.x=Math.max(-1,Math.min(1,((e.clientX-r.left)/r.width-.5)*2)); look.y=Math.max(-1,Math.min(1,((e.clientY-r.top)/r.height-.5)*2)); });
  let shake = 0, last = performance.now();
  function resize(){ const w=stage.clientWidth,h=stage.clientHeight; if(!w||!h) return; renderer.setSize(w,h,false); cam.aspect=w/h; cam.position.z = w/h<1.1 ? 11 : 9; cam.updateProjectionMatrix(); }
  new ResizeObserver(resize).observe(stage); resize();

  function frame(now){
    const dt=Math.min((now-last)/1000,.05); last=now; const t=now/1000;
    stars.rotation.y=t*.01; ring.rotation.z=t*.4; ring2.rotation.z=-t*.25; ring2.material.opacity=.35+.15*Math.sin(t*2);
    if (boss){ const b=boss; b.t+=dt;
      const tgt = b.dead ? .001 : (b.dormant ? .7 : .85+.15*b.hp);
      const cur = b.g.scale.x, s = cur+(tgt-cur)*Math.min(1,dt*(b.dead?3:5)) + b.punch*.06;
      b.g.scale.setScalar(Math.max(s,.001));
      const bob = b.dead ? -3 : Math.sin(t*1.6)*.18 - .1;
      b.g.position.y += (bob - b.g.position.y)*Math.min(1,dt*(b.dead?2:6));
      b.spin += ((b.dead?9:0)-b.spin)*dt*2;
      b.g.rotation.y += ((look.x*.5)-b.g.rotation.y)*dt*4 + b.spin*dt;
      b.g.rotation.x += ((look.y*.25)-b.g.rotation.x)*dt*4;
      b.g.rotation.z = Math.sin(t*1.1)*.03 + b.punch*.12*Math.sin(t*60);
      const rage = 1-b.hp; b.punch=Math.max(0,b.punch-dt*3); b.flash=Math.max(0,b.flash-dt*3.5);
      b.eyeM.emissiveIntensity = b.dormant||b.dead ? .1 : 1.2+rage*1.8+Math.sin(t*5)*.3*rage;
      b.bodyM.emissive.setRGB(b.flash,b.flash*.05+rage*.05,b.flash*.05);
      b.mouth.scale.y = 1+ (b.dead?0:(Math.sin(t*3)*.25+rage*.8)) + b.punch*1.2;
      for (const e of b.eyes){ e.pu.position.x += (look.x*.1 - e.pu.position.x)*dt*6; e.pu.position.y += (-look.y*.08 - e.pu.position.y)*dt*6; }
    }
    const pa=pg.attributes.position; let alive=false;
    for (let i=0;i<N;i++) if (pLife[i]>0){ alive=true; pLife[i]-=dt*.9; pVel[i*3+1]-=9*dt;
      for(let k=0;k<3;k++) pPos[i*3+k]+=pVel[i*3+k]*dt; if(pLife[i]<=0) pPos.set([0,-99,0],i*3); }
    pa.needsUpdate=true; pm.opacity=alive?1:0;
    shake=Math.max(0,shake-dt*3);
    cam.position.x=(Math.random()-.5)*shake*.4; cam.position.y=.4+(Math.random()-.5)*shake*.4; cam.lookAt(0,-.1,0);
    renderer.render(scene,cam); requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);

  return {
    set(foe,hp,dead,dormant){ if (built!==foe) build(foe); Object.assign(boss,{hp,dead,dormant}); },
    hit(big){ if(!boss) return; boss.punch=1; boss.flash=1; shake=big?1.6:.9; burst(big?100:55,big?0xff4d6d:0xffd23f); },
    win(){ burst(150,0x3ddc97); shake=1.8; }
  };
})();
