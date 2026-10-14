//! Text rendering from the embedded atlas. Coverage is packed at 4bpp in
//! src/assets/font.b64; glyph metrics live in src/assets/font.meta. Quads are
//! streamed through the renderer each frame.

use crate::gfx::{FontVert, Gfx};
use crate::math::Vec4;

pub const FONT_B64: &str = include_str!("assets/font.b64");
pub const FONT_META: &str = include_str!("assets/font.meta");

pub struct Font {
    count: usize,
    cols: usize,
    rows: usize,
    cw: f32,
    ch: f32,
    base: f32,
    w: f32,
    h: f32,
    chars: Vec<u32>,
    adv: Vec<f32>,
    /// ASCII fast path: codepoint < 128 -> atlas index.
    idx_of: [i32; 128],
    scratch: Vec<FontVert>,
}

/// Base64 (standard alphabet, '=' padding) -> bytes. Both embedded atlases
/// use it; a small decoder keeps the binary dependency-free.
pub fn b64_decode(s: &str) -> Vec<u8> {
    fn val(c: u8) -> u32 {
        match c {
            b'A'..=b'Z' => (c - b'A') as u32,
            b'a'..=b'z' => (c - b'a') as u32 + 26,
            b'0'..=b'9' => (c - b'0') as u32 + 52,
            b'+' => 62,
            b'/' => 63,
            _ => 0,
        }
    }
    let b = s.as_bytes();
    let mut out = Vec::with_capacity(b.len() / 4 * 3);
    let mut i = 0;
    while i + 4 <= b.len() {
        let v = (val(b[i]) << 18) | (val(b[i + 1]) << 12) | (val(b[i + 2]) << 6) | val(b[i + 3]);
        out.push((v >> 16) as u8);
        if b[i + 2] != b'=' {
            out.push((v >> 8) as u8);
        }
        if b[i + 3] != b'=' {
            out.push(v as u8);
        }
        i += 4;
    }
    out
}

/// Decode the font atlas: 4bpp coverage -> RGBA (rgb = white, a = coverage).
pub fn font_rgba() -> (Vec<u8>, u32, u32) {
    let raw = b64_decode(FONT_B64);
    let (w, h) = {
        let m = Meta::parse();
        (m.w, m.h)
    };
    let mut rgba = Vec::with_capacity(w * h * 4);
    for i in 0..w * h {
        let byte = raw[i / 2];
        let nib = if i & 1 == 1 { byte >> 4 } else { byte & 15 };
        rgba.extend_from_slice(&[255, 255, 255, nib * 17]);
    }
    (rgba, w as u32, h as u32)
}

/// Atlas metrics, parsed from src/assets/font.meta.
struct Meta {
    count: usize,
    cols: usize,
    rows: usize,
    cw: usize,
    ch: usize,
    base: usize,
    w: usize,
    h: usize,
    chars: Vec<u32>,
    adv: Vec<f32>,
}

impl Meta {
    fn parse() -> Self {
        let mut m = Meta {
            count: 0,
            cols: 0,
            rows: 0,
            cw: 0,
            ch: 0,
            base: 0,
            w: 0,
            h: 0,
            chars: Vec::new(),
            adv: Vec::new(),
        };
        for line in FONT_META.lines() {
            let (k, v) = line.split_once(' ').unwrap_or((line, ""));
            match k {
                "count" => m.count = v.parse().unwrap(),
                "cols" => m.cols = v.parse().unwrap(),
                "rows" => m.rows = v.parse().unwrap(),
                "cw" => m.cw = v.parse().unwrap(),
                "ch" => m.ch = v.parse().unwrap(),
                "base" => m.base = v.parse().unwrap(),
                "w" => m.w = v.parse().unwrap(),
                "h" => m.h = v.parse().unwrap(),
                "chars" => m.chars = v.split(',').map(|s| s.parse().unwrap()).collect(),
                "adv" => {
                    m.adv = v
                        .split(',')
                        .map(|s| s.parse::<u8>().unwrap() as f32)
                        .collect()
                }
                _ => {}
            }
        }
        m
    }
}

impl Font {
    pub fn new() -> Self {
        let m = Meta::parse();
        assert_eq!(m.chars.len(), m.count, "font.meta: chars mismatch");
        assert_eq!(m.adv.len(), m.count, "font.meta: adv mismatch");
        let mut idx_of = [-1i32; 128];
        for (g, &cp) in m.chars.iter().enumerate() {
            if (cp as usize) < 128 {
                idx_of[cp as usize] = g as i32;
            }
        }
        Self {
            count: m.count,
            cols: m.cols,
            rows: m.rows,
            cw: m.cw as f32,
            ch: m.ch as f32,
            base: m.base as f32,
            w: m.w as f32,
            h: m.h as f32,
            chars: m.chars,
            adv: m.adv,
            idx_of,
            scratch: Vec::with_capacity(512 * 6),
        }
    }

    fn glyph_of(&self, cp: char) -> i32 {
        let cp = cp as u32;
        if cp < 128 {
            return self.idx_of[cp as usize];
        }
        // the handful of extra glyphs after 0x7E: a small linear scan
        for g in 95..self.count {
            if self.chars[g] == cp {
                return g as i32;
            }
        }
        -1
    }

    pub fn measure(&self, text: &str, size: f32) -> f32 {
        let s = size / 28.0;
        let mut w = 0.0;
        for c in text.chars() {
            let g = self.glyph_of(c);
            if g >= 0 {
                w += self.adv[g as usize] * s;
            } else {
                w += self.adv[self.idx_of['?' as usize] as usize] * s;
            }
        }
        w
    }

    /// Draw at baseline y; size is nominal text height in pixels (CSS-like).
    /// Returns the measured width.
    #[allow(clippy::too_many_arguments)]
    pub fn draw(
        &mut self,
        gfx: &mut Gfx,
        text: &str,
        x: f32,
        y: f32,
        size: f32,
        color: Vec4,
        bold: bool,
    ) -> f32 {
        let s = size / 28.0;
        const MAX_GLYPHS: usize = 512;
        self.scratch.clear();
        let col = [color.r, color.g, color.b, color.a];
        // fake-bold: a second pass, slightly offset
        for pass in 0..if bold { 2 } else { 1 } {
            let mut pen = x + pass as f32 * s * 0.7;
            let mut n = 0;
            for c in text.chars() {
                if n >= MAX_GLYPHS {
                    break;
                }
                let mut g = self.glyph_of(c);
                if g < 0 {
                    g = self.idx_of['?' as usize];
                }
                if g < 0 {
                    continue;
                }
                let g = g as usize;
                let x0 = pen;
                let y0 = y - self.base * s;
                let x1 = x0 + self.cw * s;
                let y1 = y0 + self.ch * s;
                let u0 = (g % self.cols) as f32 / self.cols as f32;
                let v0 = (g / self.cols) as f32 / self.rows as f32;
                let u1 = u0 + self.cw / self.w;
                let v1 = v0 + self.ch / self.h;
                // Two triangles per glyph: x y u v r g b a
                let quad = [
                    [x0, y0, u0, v0],
                    [x1, y0, u1, v0],
                    [x1, y1, u1, v1],
                    [x0, y0, u0, v0],
                    [x1, y1, u1, v1],
                    [x0, y1, u0, v1],
                ];
                for [px, py, pu, pv] in quad {
                    self.scratch.push(FontVert {
                        pos: [px, py],
                        uv: [pu, pv],
                        col,
                    });
                }
                n += 1;
                pen += self.adv[g] * s;
            }
        }
        gfx.draw_font(&self.scratch);
        self.measure(text, size)
    }
}
