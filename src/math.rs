//! Minimal column-major 4x4 matrix and small vector helpers: enough for this
//! renderer (ortho projection, 2D transforms) plus hex/HSV color packing.

/// Column-major 4x4 matrix (same layout the shaders expect).
pub type Mat4 = [[f32; 4]; 4];

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Vec3 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Vec4 {
    pub r: f32,
    pub g: f32,
    pub b: f32,
    pub a: f32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Rect {
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
}

pub fn m4_identity() -> Mat4 {
    let mut m = [[0.0; 4]; 4];
    m[0][0] = 1.0;
    m[1][1] = 1.0;
    m[2][2] = 1.0;
    m[3][3] = 1.0;
    m
}

/// Pixel-space orthographic projection: y down, (0,0) top-left.
pub fn m4_ortho(w: f32, h: f32) -> Mat4 {
    let mut m = [[0.0; 4]; 4];
    m[0][0] = 2.0 / w;
    m[1][1] = -2.0 / h;
    m[2][2] = -1.0;
    m[3][0] = -1.0;
    m[3][1] = 1.0;
    m[3][3] = 1.0;
    m
}

/// out = a * b (transform by b first, then a).
/// Both matrices are column-major: m[col][row].
pub fn m4_mul(a: &Mat4, b: &Mat4) -> Mat4 {
    let mut o = [[0.0f32; 4]; 4];
    for c in 0..4 {
        for i in 0..4 {
            o[c][i] = a[0][i] * b[c][0] + a[1][i] * b[c][1] + a[2][i] * b[c][2] + a[3][i] * b[c][3];
        }
    }
    o
}

pub fn m4_translate(x: f32, y: f32, z: f32) -> Mat4 {
    let mut m = m4_identity();
    m[3][0] = x;
    m[3][1] = y;
    m[3][2] = z;
    m
}

pub fn m4_rot_z(a: f32) -> Mat4 {
    let mut m = m4_identity();
    let (s, c) = (a.sin(), a.cos());
    m[0][0] = c;
    m[0][1] = s;
    m[1][0] = -s;
    m[1][1] = c;
    m
}

pub fn m4_scale(x: f32, y: f32, z: f32) -> Mat4 {
    let mut m = m4_identity();
    m[0][0] = x;
    m[1][1] = y;
    m[2][2] = z;
    m
}

/// Transform a 2D point (z=0, w=1) by a column-major matrix.
pub fn transform2(m: &Mat4, x: f32, y: f32) -> [f32; 2] {
    [
        m[0][0] * x + m[1][0] * y + m[3][0],
        m[0][1] * x + m[1][1] * y + m[3][1],
    ]
}

/// Pack 0xRRGGBB into a Vec3.
pub fn hex3(c: u32) -> Vec3 {
    Vec3 {
        x: ((c >> 16) & 255) as f32 / 255.0,
        y: ((c >> 8) & 255) as f32 / 255.0,
        z: (c & 255) as f32 / 255.0,
    }
}

/// HSV for confetti hues (h, s, v in 0..1).
pub fn hsv(h: f32, s: f32, v: f32) -> Vec3 {
    let i = (h * 6.0).floor();
    let f = h * 6.0 - i;
    let p = v * (1.0 - s);
    let q = v * (1.0 - f * s);
    let t = v * (1.0 - (1.0 - f) * s);
    match (i as i32).rem_euclid(6) {
        0 => Vec3 { x: v, y: t, z: p },
        1 => Vec3 { x: q, y: v, z: p },
        2 => Vec3 { x: p, y: v, z: t },
        3 => Vec3 { x: p, y: q, z: v },
        4 => Vec3 { x: t, y: p, z: v },
        _ => Vec3 { x: v, y: p, z: q },
    }
}

/// A tiny deterministic PRNG.
pub struct Rng(u64);

impl Rng {
    pub fn new() -> Self {
        Rng(0x2545F4914F6CDD1D)
    }

    pub fn next_f32(&mut self) -> f32 {
        // xorshift64*
        let mut x = self.0;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        self.0 = x;
        ((x.wrapping_mul(0x2545F4914F6CDD1D) >> 40) as f32) / (1u64 << 24) as f32
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mul_applies_translation() {
        // (T * S) must keep T's translation in column 3.
        let m = m4_mul(&m4_translate(170.0, 352.0, 0.0), &m4_scale(64.0, 64.0, 1.0));
        assert!((m[3][0] - 170.0).abs() < 1e-4);
        assert!((m[3][1] - 352.0).abs() < 1e-4);
        assert!((m[0][0] - 64.0).abs() < 1e-4);
        assert!((m[1][1] - 64.0).abs() < 1e-4);
    }

    #[test]
    fn mul_composes_transforms() {
        // T * S * pivot: the sprite's bottom-center pivot lands at (cx-32, cy-64).
        let m = m4_mul(
            &m4_translate(170.0, 352.0, 0.0),
            &m4_mul(&m4_scale(64.0, 64.0, 1.0), &m4_translate(-0.5, -1.0, 0.0)),
        );
        assert!((m[3][0] - 138.0).abs() < 1e-4);
        assert!((m[3][1] - 288.0).abs() < 1e-4);
        // The unit square's top-right corner maps to (138+64, 288+64).
        let mut v = [1.0f32, 1.0, 1.0]; // x y w
        for i in 0..2 {
            v[i] = m[0][i] * 1.0 + m[1][i] * 1.0 + m[3][i];
        }
        assert!((v[0] - 202.0).abs() < 1e-3);
        assert!((v[1] - 352.0).abs() < 1e-3);
    }
}
