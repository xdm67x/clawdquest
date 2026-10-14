//! Clawd the desktop pet — 2D sprite renderer.
//!
//! Clawd is drawn from the Icons8 mood icons packed into one premultiplied
//! RGBA atlas (src/assets/sprites.b64). The window is transparent, so there
//! is no backdrop: just the sprite, a soft shadow, sparks, floating zZz while
//! dozing, and the apple he eats when a quest completes. Animations (bob,
//! tilt, hop, spin, pop-in) transform the flat sprite. Sparks are CPU-expanded
//! quads (wgpu has no per-point size).

use crate::app::{PET_FEET_MARGIN, PET_SPR, pet_head_top};
use crate::font::{Font, b64_decode};
use crate::gfx::{FlatVert, Gfx};
use crate::math::{Rng, Vec3, Vec4, hex3, m4_mul, m4_rot_z, m4_scale, m4_translate};

pub const SPRITES_B64: &str = include_str!("assets/sprites.b64");

// Atlas layout (3x3 cells of 128px; see icons/).
pub const SPR_COLS: u32 = 3;
pub const SPR_ROWS: u32 = 3;
pub const SPR_W: u32 = 384;
pub const SPR_H: u32 = 384;
pub const SPR_CELL: u32 = 128;

/// Decode the base64 atlas into raw premultiplied RGBA bytes.
pub fn sprite_rgba() -> (Vec<u8>, u32, u32) {
    let px = b64_decode(SPRITES_B64);
    assert_eq!(
        px.len(),
        (SPR_W * SPR_H * 4) as usize,
        "sprite atlas size mismatch"
    );
    (px, SPR_W, SPR_H)
}

// --- Pet states and their motion + atlas sprite ------------------------------

#[derive(Clone, Copy, PartialEq)]
enum V {
    Idle,
    Work,
    Doze,
    Wake,
    Happy,
    Fed,
    Eat,
    Dizzy,
    Party,
}

impl V {
    fn spr(self) -> u32 {
        self as u32 // atlas slot order matches the enum (SPR_* were 0..8)
    }
}

/// bob: fraction of sprite height, jump: hop fraction, spin: rad/s.
#[derive(Clone, Copy)]
struct Motion {
    bob_a: f32,
    bob_f: f32,
    tilt: f32,
    jump_f: f32,
    spin: f32,
}

const M_IDLE: Motion = Motion {
    bob_a: 0.020,
    bob_f: 1.6,
    tilt: 0.030,
    jump_f: 0.0,
    spin: 0.0,
};
const M_WORK: Motion = Motion {
    bob_a: 0.012,
    bob_f: 6.0,
    tilt: 0.015,
    jump_f: 0.0,
    spin: 0.0,
};
const M_DOZE: Motion = Motion {
    bob_a: 0.025,
    bob_f: 0.9,
    tilt: 0.045,
    jump_f: 0.0,
    spin: 0.0,
};
const M_WAKE: Motion = Motion {
    bob_a: 0.015,
    bob_f: 2.0,
    tilt: 0.025,
    jump_f: 0.12,
    spin: 0.0,
};
const M_HAPPY: Motion = Motion {
    bob_a: 0.010,
    bob_f: 1.0,
    tilt: 0.020,
    jump_f: 0.12,
    spin: 0.0,
};
const M_FED: Motion = Motion {
    bob_a: 0.018,
    bob_f: 1.2,
    tilt: 0.030,
    jump_f: 0.0,
    spin: 0.0,
};
const M_EAT: Motion = Motion {
    bob_a: 0.012,
    bob_f: 8.0,
    tilt: 0.020,
    jump_f: 0.0,
    spin: 0.0,
};
const M_DIZZY: Motion = Motion {
    bob_a: 0.015,
    bob_f: 2.0,
    tilt: 0.050,
    jump_f: 0.0,
    spin: 1.2,
};
const M_PARTY: Motion = Motion {
    bob_a: 0.010,
    bob_f: 1.5,
    tilt: 0.025,
    jump_f: 0.10,
    spin: 0.0,
};

fn motion(v: V) -> Motion {
    match v {
        V::Idle => M_IDLE,
        V::Work => M_WORK,
        V::Doze => M_DOZE,
        V::Wake => M_WAKE,
        V::Happy => M_HAPPY,
        V::Fed => M_FED,
        V::Eat => M_EAT,
        V::Dizzy => M_DIZZY,
        V::Party => M_PARTY,
    }
}

const SPARK_N: usize = 160;

#[derive(Default)]
struct Spark {
    x: f32,
    y: f32,
    vx: f32,
    vy: f32,
    life: f32,
    col: Vec3,
}

pub struct Pet {
    // static meshes (the shadow fan and the apple, colored per vertex)
    shadow_verts: Vec<FlatVert>,
    apple_verts: Vec<FlatVert>,
    sparks: Vec<Spark>,
    spark_scratch: Vec<FlatVert>,

    // animation state
    shown: V,
    revert_at: f32, // -1 = none
    revert_to: V,
    pop_t: f32,
    phase: f32,
    spin_acc: f32,
    fed_flag: bool,
    dormant_flag: bool,
    working_flag: bool,
    drag_flag: bool,
    idle_secs: f32,
    blink_t: f32,
    blink_on: f32,
    feed_state: u8, // 0 none, 1 approach, 2 chomp
    feed_t: f32,
    rng: Rng,

    // last known window size (points), for spawning sparks anywhere
    w: f32,
    h: f32,
}

impl Pet {
    pub fn new() -> Self {
        debug_assert!(
            SPR_CELL as f32 >= PET_SPR && SPR_CELL.is_multiple_of(PET_SPR as u32),
            "atlas cell must be an integer multiple of PET_SPR"
        );
        // Soft shadow: a squashed fan, alpha fading from the center to the
        // rim (positions in apple-cell units; scaled by `cell` at draw time).
        let mut shadow_verts = Vec::with_capacity(32 * 3);
        let center = [0.0f32, 0.0, 0.0, 0.0, 0.0, 0.22];
        for i in 0..32 {
            let a0 = i as f32 / 32.0 * std::f32::consts::TAU;
            let a1 = (i + 1) as f32 / 32.0 * std::f32::consts::TAU;
            let (s0, c0) = a0.sin_cos();
            let (s1, c1) = a1.sin_cos();
            shadow_verts.push(FlatVert {
                pos: [0.0, 0.0],
                col: [center[2], center[3], center[4], center[5]],
            });
            shadow_verts.push(FlatVert {
                pos: [c0 * 3.6, s0 * 1.1],
                col: [0.0, 0.0, 0.0, 0.0],
            });
            shadow_verts.push(FlatVert {
                pos: [c1 * 3.6, s1 * 1.1],
                col: [0.0, 0.0, 0.0, 0.0],
            });
        }

        // The apple: body + highlight discs, stem and leaf quads, in
        // apple-local units (body radius 1). All one mesh, colored per vertex.
        let mut apple_verts: Vec<FlatVert> = Vec::with_capacity(256);
        const RED: Vec3 = Vec3 {
            x: 1.0,
            y: 0.302,
            z: 0.427,
        }; // 0xff4d6d
        const HI: Vec3 = Vec3 {
            x: 1.0,
            y: 0.502,
            z: 0.596,
        }; // 0xff8098
        const STEM: Vec3 = Vec3 {
            x: 0.42,
            y: 0.247,
            z: 0.114,
        }; // 0x6b3f1d
        const LEAF: Vec3 = Vec3 {
            x: 0.239,
            y: 0.863,
            z: 0.592,
        }; // 0x3ddc97
        let tri = |pts: [[f32; 2]; 3], c: Vec3, v: &mut Vec<FlatVert>| {
            for p in pts {
                v.push(FlatVert {
                    pos: [p[0], p[1]],
                    col: [c.x, c.y, c.z, 1.0],
                });
            }
        };
        // Body: a slightly egg-shaped fan (wider at the bottom).
        for i in 0..24 {
            let a0 = i as f32 / 24.0 * std::f32::consts::TAU;
            let a1 = (i + 1) as f32 / 24.0 * std::f32::consts::TAU;
            let (s0, c0) = a0.sin_cos();
            let (s1, c1) = a1.sin_cos();
            let r0 = 1.0 - 0.12 * s0;
            let r1 = 1.0 - 0.12 * s1;
            tri(
                [
                    [0.0, -0.15],
                    [c0 * r0, -0.15 + s0 * r0],
                    [c1 * r1, -0.15 + s1 * r1],
                ],
                RED,
                &mut apple_verts,
            );
        }
        // Highlight spot, upper left.
        for i in 0..12 {
            let a0 = i as f32 / 12.0 * std::f32::consts::TAU;
            let a1 = (i + 1) as f32 / 12.0 * std::f32::consts::TAU;
            let (s0, c0) = a0.sin_cos();
            let (s1, c1) = a1.sin_cos();
            tri(
                [
                    [-0.42, 0.35],
                    [-0.42 + c0 * 0.24, 0.35 + s0 * 0.24],
                    [-0.42 + c1 * 0.24, 0.35 + s1 * 0.24],
                ],
                HI,
                &mut apple_verts,
            );
        }
        // Stem.
        tri(
            [[0.0, 0.78], [0.09, 1.05], [-0.09, 1.05]],
            STEM,
            &mut apple_verts,
        );
        tri(
            [[0.0, 0.78], [0.14, 0.82], [0.09, 1.05]],
            STEM,
            &mut apple_verts,
        );
        // Leaf, tilted beside the stem.
        tri(
            [[0.16, 1.02], [0.62, 1.06], [0.30, 1.22]],
            LEAF,
            &mut apple_verts,
        );
        tri(
            [[0.16, 1.02], [0.44, 0.92], [0.62, 1.06]],
            LEAF,
            &mut apple_verts,
        );

        Self {
            shadow_verts,
            apple_verts,
            sparks: (0..SPARK_N).map(|_| Spark::default()).collect(),
            spark_scratch: Vec::with_capacity(SPARK_N * 6),
            shown: V::Idle,
            revert_at: -1.0,
            revert_to: V::Idle,
            pop_t: 1.0,
            phase: 0.0,
            spin_acc: 0.0,
            fed_flag: false,
            dormant_flag: false,
            working_flag: false,
            drag_flag: false,
            idle_secs: 0.0,
            blink_t: 3.0,
            blink_on: 0.0,
            feed_state: 0,
            feed_t: 0.0,
            rng: Rng::new(),
            w: 88.0,
            h: 92.0,
        }
    }

    fn show(&mut self, name: V) {
        if self.shown == name {
            return;
        }
        self.shown = name;
        self.pop_t = 0.0;
        self.phase = 0.0;
    }

    fn cameo(&mut self, name: V, then: V, ms: f32, t: f32) {
        self.show(name);
        self.revert_to = then;
        self.revert_at = t + ms / 1000.0;
    }

    fn burst(&mut self, n: usize, hex: u32) {
        let c = hex3(hex);
        let mut cnt = 0;
        for s in self.sparks.iter_mut() {
            if cnt >= n {
                break;
            }
            if s.life <= 0.0 {
                cnt += 1;
                let rnd = self.rng.next_f32();
                s.life = 0.6 + rnd * 0.3;
                s.x = self.w * 0.5 + (self.rng.next_f32() - 0.5) * 40.0;
                s.y = pet_head_top(self.h) + 30.0 + (self.rng.next_f32() - 0.5) * 40.0;
                let a = self.rng.next_f32() * std::f32::consts::TAU;
                let sp = 60.0 + self.rng.next_f32() * 160.0;
                s.vx = a.cos() * sp;
                s.vy = a.sin() * sp - 80.0;
                s.col = c;
            }
        }
    }

    /// Steady state, synced each frame. fed = all quests done; dormant = none.
    pub fn set(&mut self, fed: bool, dormant: bool, working: bool) {
        self.fed_flag = fed;
        self.dormant_flag = dormant;
        self.working_flag = working;
    }

    /// An apple flies in and he eats it, then a happy hop.
    pub fn feed(&mut self) {
        if self.feed_state == 0 {
            self.feed_state = 1;
            self.feed_t = 0.0;
        }
    }

    /// All-quests-done celebration.
    pub fn win(&mut self, t: f32) {
        self.cameo(V::Party, V::Fed, 2200.0, t);
        self.burst(20, 0xff4d6d);
        self.burst(20, 0x3ddc97);
        self.burst(20, 0xffbd2e);
    }

    /// A pat on the head (quick click, not a drag).
    pub fn click(&mut self, t: f32) {
        self.cameo(V::Happy, self.steady_variant(), 1500.0, t);
    }

    /// The window is being dragged around the desktop.
    pub fn drag(&mut self, on: bool) {
        self.drag_flag = on;
    }

    /// Any user interaction (wakes him, resets doze).
    pub fn interact(&mut self) {
        self.idle_secs = 0.0;
    }

    fn steady_variant(&self) -> V {
        if self.working_flag {
            V::Work
        } else if self.fed_flag {
            V::Fed
        } else if self.dormant_flag || self.idle_secs > 300.0 {
            V::Doze
        } else {
            V::Idle
        }
    }

    pub fn frame(&mut self, gfx: &mut Gfx, font: &mut Font, t: f32, dt: f32) {
        self.w = gfx.logical_size().0;
        self.h = gfx.logical_size().1;
        let cx = self.w * 0.5;
        let feet_y = self.h - PET_FEET_MARGIN;
        self.idle_secs += dt;

        // Variant state machine: a cameo plays out, dragging wins, else steady.
        if self.revert_at >= 0.0 && t >= self.revert_at {
            self.revert_at = -1.0;
            let to = self.revert_to;
            self.show(to);
        }
        if self.drag_flag {
            self.revert_at = -1.0;
            self.show(V::Dizzy);
        } else if self.revert_at < 0.0 {
            let steady = self.steady_variant();
            if self.shown == V::Doze && steady != V::Doze {
                self.cameo(V::Wake, steady, 1800.0, t);
            } else {
                self.show(steady);
            }
        }

        // The apple: approach (0.6s), then three chomps (0.75s), then a hop.
        if self.feed_state != 0 {
            self.feed_t += dt;
            if self.feed_state == 1 && self.feed_t >= 0.6 {
                self.feed_state = 2;
                self.feed_t = 0.0;
            } else if self.feed_state == 2 && self.feed_t >= 0.75 {
                self.feed_state = 0;
                self.cameo(V::Happy, self.steady_variant(), 1300.0, t);
                self.burst(12, 0xff4d6d);
                self.burst(12, 0x3ddc97);
            }
        }

        // A quick squash "blink" now and then while awake.
        if self.shown == V::Idle || self.shown == V::Work {
            if self.blink_on > 0.0 {
                self.blink_on -= dt;
            } else {
                self.blink_t -= dt;
                if self.blink_t <= 0.0 {
                    self.blink_on = 0.14;
                    self.blink_t = 2.5 + self.rng.next_f32() * 4.0;
                }
            }
        }
        let mut disp = self.shown;
        if self.feed_state == 2 {
            disp = V::Eat;
        }

        let m = motion(disp);
        self.phase += dt;
        self.pop_t += dt * 4.0;
        if self.pop_t > 1.0 {
            self.pop_t = 1.0;
        }
        let pop = 1.0 - (1.0 - self.pop_t).powi(3);

        let mut bob = (self.phase * m.bob_f).sin() * m.bob_a;
        if m.jump_f != 0.0 {
            bob += (self.phase * m.jump_f).sin().abs() * m.jump_f;
        }
        if self.feed_state == 2 {
            bob += 0.012 * (self.feed_t * 22.0).sin().abs(); // chomp wobble
        }
        self.spin_acc += m.spin * dt;
        let rotz = (self.phase * 1.1).sin() * m.tilt;

        let mut sx = pop;
        let mut sy = pop;
        if self.blink_on > 0.0 && (self.shown == V::Idle || self.shown == V::Work) {
            sx *= 1.04;
            sy *= 0.90;
        }

        // --- Shadow (drawn under the sprite, in PET_SPR cell units) ----------
        let cell = PET_SPR / 9.0;
        let shadow_model = m4_mul(
            &m4_translate(cx, feet_y + 0.3, 0.0),
            &m4_scale(cell, cell, 1.0),
        );
        gfx.draw_flat(&self.shadow_verts, shadow_model);

        // --- Clawd --------------------------------------------------------------
        let model = m4_mul(
            &m4_translate(cx, feet_y - bob * PET_SPR, 0.0),
            &m4_mul(
                &m4_scale(PET_SPR * sx, PET_SPR * sy, 1.0),
                &m4_mul(&m4_rot_z(rotz), &m4_translate(-0.5, -1.0, 0.0)),
            ),
        );
        // Sample half a texel inside the cell: at the exact uv boundary the
        // linear filter would pull in the neighboring icon's edge pixels
        // (their orange arms reach the cell border) as an orange fringe.
        let eu = 0.5 / SPR_W as f32;
        let ev = 0.5 / SPR_H as f32;
        let spr = disp.spr();
        let u0 = (spr % SPR_COLS) as f32 / SPR_COLS as f32;
        let v0 = (spr / SPR_COLS) as f32 / SPR_ROWS as f32;
        let uv = [
            u0 + eu,
            v0 + ev,
            u0 + 1.0 / SPR_COLS as f32 - eu,
            v0 + 1.0 / SPR_ROWS as f32 - ev,
        ];
        gfx.draw_sprite(
            model,
            uv,
            Vec4 {
                r: 1.0,
                g: 1.0,
                b: 1.0,
                a: 1.0,
            },
        );

        // --- The apple ----------------------------------------------------------
        if self.feed_state != 0 {
            // The mouth is around 44% up the sprite.
            let mouth_x = cx + PET_SPR * 0.06;
            let mouth_y = feet_y - PET_SPR * 0.44;
            let (ax, ay, ascale, arot);
            if self.feed_state == 1 {
                let p = (self.feed_t / 0.6).min(1.0);
                let ease = 1.0 - (1.0 - p).powi(3);
                let sx0 = cx + 40.0;
                let sy0 = feet_y + 24.0;
                ax = sx0 + (mouth_x - sx0) * ease + (p * std::f32::consts::PI).sin() * 14.0;
                ay = sy0 + (mouth_y - sy0) * ease;
                arot = (p * std::f32::consts::TAU).sin() * 0.4;
                ascale = 1.0;
            } else {
                ax = mouth_x;
                ay = mouth_y;
                ascale = 1.0 - self.feed_t / 0.75; // each chomp takes a bite
                arot = (self.feed_t * 22.0).sin() * 0.15;
            }
            let s = cell * 0.78 * ascale;
            let model = m4_mul(
                &m4_translate(ax, ay, 0.0),
                &m4_mul(&m4_scale(s, s, 1.0), &m4_rot_z(arot)),
            );
            gfx.draw_flat(&self.apple_verts, model);
        }

        // --- Sparks (CPU-expanded quads) ------------------------------------------
        self.spark_scratch.clear();
        let half = 2.0 * gfx.pts_per_px(); // 4 device pixels
        for s in self.sparks.iter_mut() {
            if s.life > 0.0 {
                s.life -= dt;
                s.vy += 700.0 * dt; // gravity
                s.x += s.vx * dt;
                s.y += s.vy * dt;
                if s.life <= 0.0 {
                    continue;
                }
                let a = if s.life < 0.2 { s.life * 5.0 } else { 1.0 };
                let col = [s.col.x, s.col.y, s.col.z, a];
                let x0 = s.x - half;
                let x1 = s.x + half;
                let y0 = s.y - half;
                let y1 = s.y + half;
                self.spark_scratch.push(FlatVert { pos: [x0, y0], col });
                self.spark_scratch.push(FlatVert { pos: [x1, y0], col });
                self.spark_scratch.push(FlatVert { pos: [x1, y1], col });
                self.spark_scratch.push(FlatVert { pos: [x0, y0], col });
                self.spark_scratch.push(FlatVert { pos: [x1, y1], col });
                self.spark_scratch.push(FlatVert { pos: [x0, y1], col });
            }
        }
        gfx.draw_flat(&self.spark_scratch, crate::math::m4_identity());

        // --- zZz while dozing -----------------------------------------------------
        if self.shown == V::Doze {
            let head_top = pet_head_top(self.h);
            const ZC: [char; 3] = ['z', 'Z', 'Z'];
            for (i, &g) in ZC.iter().enumerate() {
                let p = (t * 0.22 + i as f32 * 0.33).fract();
                let a = (p * std::f32::consts::PI).sin() * 0.8;
                let gs = g.to_string();
                font.draw(
                    gfx,
                    &gs,
                    cx + 30.0 + i as f32 * 8.0 + p * 10.0,
                    head_top - 6.0 - p * 34.0,
                    10.0 + i as f32 * 4.0,
                    Vec4 {
                        r: 0.753,
                        g: 0.729,
                        b: 1.0,
                        a,
                    },
                    true,
                );
            }
        }
    }
}
