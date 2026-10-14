//! The quest card. A rounded glass card that opens above
//! Clawd when he is clicked: level badge, XP bar, streak stats, the quest
//! list (scrollable; click a quest to run its chronometer, check to complete,
//! x to delete), an add bar, and a close button. Popups, toasts and confetti
//! are anchored to the pet rather than a window edge.

use crate::audio::Audio;
use crate::font::Font;
use crate::fx::FxSink;
use crate::game::{Game, fmt_mmss, fmt_time_h, lvl, lvl_start, title_of};
use crate::gfx::Gfx;
use crate::math::{Rect, Vec4, hsv, m4_identity};
use crate::pet::Pet;

const MAXPOP: usize = 16;
const MAXCONF: usize = 400;

/// Pack 0xRRGGBB plus an alpha into a color.
const fn rgba(hex: u32, a: f32) -> Vec4 {
    Vec4 {
        r: ((hex >> 16) & 255) as f32 / 255.0,
        g: ((hex >> 8) & 255) as f32 / 255.0,
        b: (hex & 255) as f32 / 255.0,
        a,
    }
}

// Palette.
const C_TXT: Vec4 = rgba(0xf4f0ff, 1.0);
const C_DIM: Vec4 = rgba(0x9a90c8, 1.0);
const C_GLASS: Vec4 = rgba(0x18122e, 0.82);
const C_LINE: Vec4 = rgba(0xa082ff, 0.22);
const C_ACC: Vec4 = rgba(0x8b5cf6, 1.0);
const C_ACC2: Vec4 = rgba(0xc084fc, 1.0);
const C_PINK: Vec4 = rgba(0xec4899, 1.0);
const C_XP: Vec4 = rgba(0xffd23f, 1.0);
const C_XP2: Vec4 = rgba(0xff9f1c, 1.0);
const C_OK: Vec4 = rgba(0x3ddc97, 1.0);
const C_WHITE: Vec4 = rgba(0xffffff, 1.0);
const C_NONE: Vec4 = rgba(0x000000, 0.0);

fn mul(c: Vec4, k: f32) -> Vec4 {
    Vec4 {
        r: c.r * k,
        g: c.g * k,
        b: c.b * k,
        a: c.a,
    }
}

struct Pop {
    text: String,
    x: f32,
    y: f32,
    t0: f32,
}

struct Conf {
    x: f32,
    y: f32,
    vx: f32,
    vy: f32,
    t: f32,
    c: Vec4,
}

struct RowHit {
    qi: usize,  // index into game.s.quests
    cb: Rect,   // checkbox rect
    txt: Rect,  // text rect
    xbtn: Rect, // delete rect
}

/// Wires game.rs's effects to this card + the pet + the audio sink (in C,
/// game.c called ui_pop/ui_toast/ui_confetti/pet_feed/pet_win/sfx_* global
/// functions directly).
struct Sink<'a> {
    ui: &'a mut Ui,
    pet: &'a mut Pet,
    audio: &'a Audio,
    t: f32,
}

impl FxSink for Sink<'_> {
    fn pop(&mut self, text: &str, x: f32, y: f32) {
        self.ui.pop(text, x, y, self.t);
    }
    fn toast(&mut self, text: &str) {
        self.ui.toast(text, self.t);
    }
    fn confetti(&mut self, n: usize) {
        self.ui.confetti(n);
    }
    fn feed(&mut self) {
        self.pet.feed();
    }
    fn win(&mut self) {
        self.pet.win(self.t);
    }
    fn sfx_hit(&mut self) {
        self.audio.sfx_hit();
    }
    fn sfx_win(&mut self) {
        self.audio.sfx_win();
    }
    fn sfx_add(&mut self) {
        self.audio.sfx_add();
    }
}

/// Keys the add bar cares about.
pub enum Key {
    Backspace,
    Enter,
}

pub struct Ui {
    // text input buffer (add bar)
    input_buf: String,

    pops: Vec<Pop>,
    toast: String,
    toast_t0: f32,
    toast_on: bool,
    confetti: Vec<Conf>,
    conf_scratch: Vec<crate::gfx::FlatVert>,

    panel_open: bool,
    quit_req: bool,
    mouse: (f32, f32),

    // quest list scrolling: the target is authoritative
    target_scroll: f32,
    card_pressed: bool, // a press started on the card (released = click)
    dragging: bool,     // ...and on the scroll band, so rows follow the finger
    drag_scroll0: f32,
    press_x0: f32,
    press_y0: f32,
    drag_moved: bool,

    rows: Vec<RowHit>,
    card_rect: Rect,
    band_rect: Rect,
    add_btn: Rect,
    close_btn: Rect,

    rng: crate::math::Rng,
    w: f32,
    h: f32,
}

impl Ui {
    pub fn new() -> Self {
        Self {
            input_buf: String::new(),
            pops: Vec::with_capacity(MAXPOP),
            toast: String::new(),
            toast_t0: 0.0,
            toast_on: false,
            confetti: Vec::with_capacity(MAXCONF),
            conf_scratch: Vec::with_capacity(MAXCONF * 6),
            panel_open: false,
            quit_req: false,
            mouse: (-1.0, -1.0),
            target_scroll: 0.0,
            card_pressed: false,
            dragging: false,
            drag_scroll0: 0.0,
            press_x0: 0.0,
            press_y0: 0.0,
            drag_moved: false,
            rows: Vec::new(),
            card_rect: Rect::default(),
            band_rect: Rect::default(),
            add_btn: Rect::default(),
            close_btn: Rect::default(),
            rng: crate::math::Rng::new(),
            w: 88.0,
            h: 92.0,
        }
    }

    pub fn panel_open(&self) -> bool {
        self.panel_open
    }

    pub fn should_quit(&self) -> bool {
        self.quit_req
    }

    pub fn set_panel(&mut self, open: bool) {
        self.panel_open = open;
        if !open {
            self.input_buf.clear();
            self.target_scroll = 0.0;
        }
    }

    pub fn on_card(&self, x: f32, y: f32) -> bool {
        self.panel_open
            && x >= self.card_rect.x
            && x <= self.card_rect.x + self.card_rect.w
            && y >= self.card_rect.y
            && y <= self.card_rect.y + self.card_rect.h
    }

    /// Floating +XP popup, anchored over the pet (x/y < 0 = defaults).
    pub fn pop(&mut self, text: &str, x: f32, y: f32, t: f32) {
        let text = text.to_string();
        // The x<0/y<0 fallbacks center over the pet (callers always pass real
        // coordinates).
        let x = if x < 0.0 { (self.w - 120.0) * 0.5 } else { x };
        let y = if y < 0.0 {
            crate::app::pet_head_top(self.h) - 8.0
        } else {
            y
        };
        if self.pops.len() < MAXPOP {
            self.pops.push(Pop { text, x, y, t0: t });
        } else {
            let i = (self.rng.next_f32() * MAXPOP as f32) as usize % MAXPOP;
            self.pops[i] = Pop { text, x, y, t0: t };
        }
    }

    pub fn toast(&mut self, text: &str, t: f32) {
        self.toast = text.to_string();
        self.toast_t0 = t;
        self.toast_on = true;
    }

    pub fn confetti(&mut self, n: usize) {
        let ox = self.w * 0.5;
        let oy = crate::app::pet_head_top(self.h) - 30.0;
        for _ in 0..n {
            if self.confetti.len() >= MAXCONF {
                break;
            }
            let r = self.rng.next_f32();
            let c = hsv(r, 0.9, 0.6);
            self.confetti.push(Conf {
                x: ox,
                y: oy,
                vx: (self.rng.next_f32() - 0.5) * 16.0 * 60.0,
                vy: -(self.rng.next_f32() * 14.0 + 2.0) * 60.0,
                t: 1.5, // 90 frames at 60fps
                c: Vec4 {
                    r: c.x,
                    g: c.y,
                    b: c.z,
                    a: 1.0,
                },
            });
        }
    }

    // --- input ----------------------------------------------------------------

    fn in_rect(r: &Rect, x: f32, y: f32) -> bool {
        x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h
    }

    fn click(&mut self, x: f32, y: f32, game: &mut Game, pet: &mut Pet, audio: &Audio, t: f32) {
        enum Action {
            Complete(usize),
            Undo(usize),
            Delete(usize),
            Toggle(usize),
            Submit,
            Close,
        }
        // Find what was hit: checkbox, delete x, then text row.
        let mut action = None;
        for r in &self.rows {
            if Self::in_rect(&r.cb, x, y) {
                action = Some(if game.s.quests[r.qi].done {
                    Action::Undo(r.qi)
                } else {
                    Action::Complete(r.qi)
                });
                break;
            }
        }
        if action.is_none() {
            for r in &self.rows {
                if Self::in_rect(&r.xbtn, x, y) {
                    action = Some(Action::Delete(r.qi));
                    break;
                }
            }
        }
        if action.is_none() {
            for r in &self.rows {
                if Self::in_rect(&r.txt, x, y) {
                    action = Some(Action::Toggle(r.qi));
                    break;
                }
            }
        }
        if action.is_none() {
            if Self::in_rect(&self.add_btn, x, y) {
                action = Some(Action::Submit);
            } else if Self::in_rect(&self.close_btn, x, y) {
                action = Some(Action::Close);
            }
        }
        match action {
            Some(Action::Complete(qi)) => {
                let mut sink = Sink {
                    ui: self,
                    pet,
                    audio,
                    t,
                };
                game.complete(qi, x, y, &mut sink);
            }
            Some(Action::Undo(qi)) => game.undo(qi),
            Some(Action::Delete(qi)) => game.del_quest(qi),
            Some(Action::Toggle(qi)) => {
                let id = game.s.quests[qi].id;
                game.toggle_timer(id);
            }
            Some(Action::Submit) => self.submit_input(game, pet, audio, t),
            Some(Action::Close) => self.quit_req = true,
            None => {}
        }
    }

    /// The add bar's Return action (also the Add button): trim and submit.
    fn submit_input(&mut self, game: &mut Game, pet: &mut Pet, audio: &Audio, t: f32) {
        // Trim like the JS form did (leading/trailing spaces only).
        let s = self.input_buf.trim_matches(' ').to_string();
        if !s.is_empty() {
            let mut sink = Sink {
                ui: self,
                pet,
                audio,
                t,
            };
            game.add_quest(&s, &mut sink);
            self.input_buf.clear();
        }
    }

    pub fn mouse_down(&mut self, x: f32, y: f32) {
        if !self.panel_open {
            return;
        }
        self.card_pressed = true;
        self.drag_moved = false;
        self.press_x0 = x;
        self.press_y0 = y;
        self.drag_scroll0 = self.target_scroll;
        self.dragging = Self::in_rect(&self.band_rect, x, y);
    }

    pub fn mouse_up(
        &mut self,
        x: f32,
        y: f32,
        game: &mut Game,
        pet: &mut Pet,
        audio: &Audio,
        t: f32,
    ) {
        // A press that stayed put is a click; a drag suppresses the click.
        if self.card_pressed && !self.drag_moved && self.panel_open {
            self.click(x, y, game, pet, audio, t);
        }
        self.card_pressed = false;
        self.dragging = false;
    }

    pub fn wheel(&mut self, dy: f32) {
        if !self.panel_open {
            return;
        }
        let (mx, my) = self.mouse;
        if Self::in_rect(&self.band_rect, mx, my) {
            self.target_scroll -= dy * 36.0; // one row per notch
        }
    }

    pub fn motion(&mut self, x: f32, y: f32) {
        self.mouse = (x, y);
        if self.card_pressed {
            let (dx, dy) = (x - self.press_x0, y - self.press_y0);
            if dx > 6.0 || dx < -6.0 || dy > 6.0 || dy < -6.0 {
                self.drag_moved = true;
            }
            if self.dragging && self.drag_moved {
                self.target_scroll = self.drag_scroll0 - dy; // rows follow the finger
            }
        }
    }

    pub fn text(&mut self, s: &str) {
        if !self.panel_open {
            return;
        }
        if self.input_buf.chars().count() + s.chars().count() < 256 {
            self.input_buf.push_str(s);
        }
    }

    pub fn key(&mut self, key: Key, game: &mut Game, pet: &mut Pet, audio: &Audio, t: f32) {
        if !self.panel_open {
            return;
        }
        match key {
            Key::Backspace => {
                self.input_buf.pop(); // removes the last char
            }
            Key::Enter => self.submit_input(game, pet, audio, t),
        }
    }

    // --- layout ------------------------------------------------------------------

    /// Truncate text to maxw pixels, appending "..." when it does not fit.
    fn fit_text(&self, font: &Font, s: &str, size: f32, maxw: f32) -> String {
        if font.measure(s, size) <= maxw {
            return s.to_string();
        }
        let n = s.chars().count();
        for k in (1..n).rev() {
            let mut cand: String = s.chars().take(k).collect();
            cand.push_str("...");
            if font.measure(&cand, size) <= maxw {
                return cand;
            }
        }
        "...".to_string()
    }

    fn panel_frame(&mut self, gfx: &mut Gfx, font: &mut Font, game: &Game, t: f32) {
        let mut cw = 300.0;
        if cw > self.w - 20.0 {
            cw = self.w - 20.0;
        }
        let cx = (self.w - cw) * 0.5;
        let cy = 10.0;
        let ch = 274.0;
        self.card_rect = Rect {
            x: cx,
            y: cy,
            w: cw,
            h: ch,
        };
        gfx.draw_rrect(cx, cy, cw, ch, 18.0, C_GLASS, C_NONE, 0.0, C_LINE, 1.0);

        let ix = cx + 14.0;
        let iw = cw - 28.0; // inner box

        // Header: level badge, name, rank.
        gfx.draw_rrect(
            ix,
            cy + 12.0,
            30.0,
            30.0,
            9.0,
            C_ACC,
            C_PINK,
            2.0,
            C_NONE,
            0.0,
        );
        {
            let n = format!("{}", lvl(game.s.xp));
            let w = font.measure(&n, 13.0);
            font.draw(gfx, &n, ix + 15.0 - w / 2.0, cy + 32.0, 13.0, C_WHITE, true);
        }
        font.draw(gfx, "Clawd Quest", ix + 40.0, cy + 25.0, 14.0, C_TXT, true);
        {
            let s = format!(
                "Lv {} \u{2022} {}",
                lvl(game.s.xp),
                title_of(lvl(game.s.xp))
            );
            font.draw(gfx, &s, ix + 40.0, cy + 41.0, 10.0, C_DIM, false);
        }

        // Close button, top-right.
        let qb = 22.0;
        self.close_btn = Rect {
            x: cx + cw - 14.0 - qb,
            y: cy + 15.0,
            w: qb,
            h: qb,
        };
        let (mx, my) = self.mouse;
        let q_hover = Self::in_rect(&self.close_btn, mx, my);
        gfx.draw_rrect(
            self.close_btn.x,
            self.close_btn.y,
            self.close_btn.w,
            self.close_btn.h,
            8.0,
            rgba(0xffffff, if q_hover { 0.18 } else { 0.07 }),
            C_NONE,
            0.0,
            rgba(0xffffff, 0.16),
            1.0,
        );
        {
            let x = "\u{d7}"; // × (U+00D7)
            font.draw(
                gfx,
                x,
                self.close_btn.x + (qb - font.measure(x, 12.0)) * 0.5,
                self.close_btn.y + 15.0,
                12.0,
                if q_hover { C_WHITE } else { C_TXT },
                false,
            );
        }

        // XP bar.
        let l = lvl(game.s.xp);
        let a = lvl_start(l);
        let b = lvl_start(l + 1);
        let bx = ix;
        let bw = iw - 92.0;
        let by = cy + 52.0;
        gfx.draw_rrect(
            bx,
            by,
            bw,
            7.0,
            3.5,
            rgba(0x000000, 0.40),
            C_NONE,
            0.0,
            rgba(0xffffff, 0.06),
            1.0,
        );
        let frac = (game.s.xp - a) as f32 / (b - a) as f32;
        if frac > 0.0 {
            let mut fw = bw * frac;
            if fw < 7.0 {
                fw = 7.0;
            }
            if frac > 1.0 {
                fw = bw;
            }
            gfx.draw_rrect(bx, by, fw, 7.0, 3.5, C_XP, C_XP2, 1.0, C_NONE, 0.0);
        }
        {
            let s = format!("{} / {} XP", game.s.xp - a, b - a);
            font.draw(
                gfx,
                &s,
                cx + cw - 14.0 - font.measure(&s, 9.0),
                by + 7.5,
                9.0,
                C_DIM,
                false,
            );
        }

        // Stats line.
        {
            let s = format!(
                "{}-day streak \u{2022} {} done \u{2022} {} focused",
                game.s.streak,
                game.s.total,
                fmt_time_h(game.s.time)
            );
            font.draw(gfx, &s, ix, cy + 78.0, 10.0, C_DIM, false);
        }

        // Quest band (scrollable). The add bar is docked to the card bottom.
        let add_y = cy + ch - 14.0 - 32.0;
        let row_h = 30.0;
        let gap = 6.0;
        self.band_rect = Rect {
            x: ix,
            y: cy + 92.0,
            w: iw,
            h: add_y - 8.0 - (cy + 92.0),
        };

        // Sort done quests to the bottom (stable).
        let mut ord: Vec<usize> = Vec::with_capacity(game.s.quests.len());
        for (i, q) in game.s.quests.iter().enumerate() {
            if !q.done {
                ord.push(i);
            }
        }
        for (i, q) in game.s.quests.iter().enumerate() {
            if q.done {
                ord.push(i);
            }
        }

        let m = ord.len();
        let content_h = if m > 0 {
            m as f32 * (row_h + gap) - gap
        } else {
            0.0
        };
        let max_scroll = if content_h > self.band_rect.h {
            content_h - self.band_rect.h
        } else {
            0.0
        };
        self.target_scroll = self.target_scroll.clamp(0.0, max_scroll);

        self.rows.clear();
        if self.band_rect.h > 0.0 {
            gfx.set_scissor(Some(self.band_rect));
            for (vi, &qi) in ord.iter().enumerate() {
                let ry = self.band_rect.y + vi as f32 * (row_h + gap) - self.target_scroll;
                if ry + row_h < self.band_rect.y || ry > self.band_rect.y + self.band_rect.h {
                    continue;
                }
                let q = &game.s.quests[qi];
                let hovered = mx >= self.band_rect.x
                    && mx <= self.band_rect.x + self.band_rect.w
                    && my >= ry
                    && my <= ry + row_h;
                let is_act = q.id == game.s.active;
                let is_run = is_act && game.s.running;

                // Row background.
                let mut bg = rgba(0xffffff, 0.04);
                let mut border = C_NONE;
                if is_act {
                    bg = rgba(0x8b5cf6, 0.20);
                    border = C_ACC2;
                } else if hovered {
                    bg = rgba(0xffffff, 0.08);
                }
                gfx.draw_rrect(
                    self.band_rect.x,
                    ry,
                    self.band_rect.w,
                    row_h,
                    10.0,
                    bg,
                    C_NONE,
                    0.0,
                    border,
                    1.0,
                );

                // Checkbox.
                let ccx = self.band_rect.x + 5.0;
                let ccy = ry + (row_h - 18.0) / 2.0;
                gfx.draw_rrect(
                    ccx,
                    ccy,
                    18.0,
                    18.0,
                    6.0,
                    if q.done { C_OK } else { C_NONE },
                    C_NONE,
                    0.0,
                    if q.done { C_OK } else { rgba(0xffffff, 0.27) },
                    2.0,
                );
                if q.done {
                    let g = "\u{2713}"; // checkmark
                    let gw = font.measure(g, 11.0);
                    font.draw(
                        gfx,
                        g,
                        ccx + 9.0 - gw / 2.0,
                        ccy + 13.5,
                        11.0,
                        rgba(0x06281b, 1.0),
                        true,
                    );
                }

                // Quest text (with ellipsis; strikethrough when done).
                let tx = ccx + 26.0;
                let maxw = self.band_rect.w - 26.0 - 26.0 - 56.0;
                let shown = self.fit_text(font, &q.text, 12.0, maxw);
                let tcol = if q.done { mul(C_TXT, 0.55) } else { C_TXT };
                font.draw(gfx, &shown, tx, ry + row_h / 2.0 + 4.0, 12.0, tcol, false);
                if q.done {
                    let tw = font.measure(&shown, 12.0);
                    gfx.draw_rrect(
                        tx,
                        ry + row_h / 2.0 - 2.0,
                        tw,
                        1.5,
                        1.0,
                        tcol,
                        C_NONE,
                        0.0,
                        C_NONE,
                        0.0,
                    );
                }

                // Chronometer chip.
                if is_run || q.secs > 0 {
                    let s = fmt_mmss(q.secs);
                    let sw = font.measure(&s, 10.0) + 12.0;
                    let sx = self.band_rect.x + self.band_rect.w - 26.0 - sw;
                    if is_run {
                        gfx.draw_rrect(
                            sx,
                            ry + (row_h - 16.0) / 2.0,
                            sw,
                            16.0,
                            8.0,
                            rgba(0x8b5cf6, 0.13),
                            C_NONE,
                            0.0,
                            C_NONE,
                            0.0,
                        );
                        font.draw(
                            gfx,
                            &s,
                            sx + 6.0,
                            ry + row_h / 2.0 + 3.5,
                            10.0,
                            C_ACC2,
                            true,
                        );
                    } else {
                        font.draw(
                            gfx,
                            &s,
                            sx + 6.0,
                            ry + row_h / 2.0 + 3.5,
                            10.0,
                            C_DIM,
                            false,
                        );
                    }
                }

                // Delete button, visible on hover.
                let xx = self.band_rect.x + self.band_rect.w - 24.0;
                if hovered {
                    let dm = "\u{d7}";
                    font.draw(
                        gfx,
                        dm,
                        xx + (26.0 - font.measure(dm, 12.0)) * 0.5,
                        ry + row_h / 2.0 + 4.0,
                        12.0,
                        C_DIM,
                        false,
                    );
                }
                self.rows.push(RowHit {
                    qi,
                    cb: Rect {
                        x: ccx,
                        y: ccy,
                        w: 18.0,
                        h: 18.0,
                    },
                    txt: Rect {
                        x: tx,
                        y: ry,
                        w: maxw,
                        h: row_h,
                    },
                    xbtn: Rect {
                        x: xx,
                        y: ry,
                        w: 26.0,
                        h: row_h,
                    },
                });
            }
            gfx.set_scissor(None);
        }

        // Empty board notice.
        if game.s.quests.is_empty() {
            let l1 = "No quests \u{2022} add one below.";
            font.draw(
                gfx,
                l1,
                cx + (cw - font.measure(l1, 11.0)) * 0.5,
                self.band_rect.y + self.band_rect.h * 0.5 + 4.0,
                11.0,
                C_DIM,
                false,
            );
        }

        // --- Add bar -------------------------------------------------------------
        gfx.draw_rrect(ix, add_y, iw, 32.0, 16.0, C_GLASS, C_NONE, 0.0, C_LINE, 1.0);
        if self.input_buf.is_empty() {
            let ph = "Add a quest...";
            font.draw(gfx, ph, ix + 12.0, add_y + 21.0, 12.0, C_DIM, false);
        } else {
            font.draw(
                gfx,
                &self.input_buf,
                ix + 12.0,
                add_y + 21.0,
                12.0,
                C_TXT,
                false,
            );
        }
        if (t).fract() < 0.5 {
            let cx2 = ix + 12.0 + font.measure(&self.input_buf, 12.0);
            gfx.draw_rrect(
                cx2,
                add_y + 10.0,
                1.5,
                13.0,
                1.0,
                C_ACC2,
                C_NONE,
                0.0,
                C_NONE,
                0.0,
            );
        }
        let ab_w = 56.0;
        self.add_btn = Rect {
            x: ix + iw - ab_w - 4.0,
            y: add_y + 4.0,
            w: ab_w,
            h: 24.0,
        };
        let ab_hover = Self::in_rect(&self.add_btn, mx, my);
        gfx.draw_rrect(
            self.add_btn.x,
            self.add_btn.y,
            self.add_btn.w,
            self.add_btn.h,
            12.0,
            C_ACC,
            C_ACC2,
            1.0,
            C_NONE,
            0.0,
        );
        if ab_hover {
            gfx.draw_rrect(
                self.add_btn.x,
                self.add_btn.y,
                self.add_btn.w,
                self.add_btn.h,
                12.0,
                rgba(0xffffff, 0.12),
                C_NONE,
                0.0,
                C_NONE,
                0.0,
            );
        }
        let add = "Add";
        font.draw(
            gfx,
            add,
            self.add_btn.x + (self.add_btn.w - font.measure(add, 12.0)) * 0.5,
            self.add_btn.y + 15.5,
            12.0,
            C_WHITE,
            true,
        );
    }

    // --- frame ------------------------------------------------------------------

    pub fn frame(
        &mut self,
        gfx: &mut Gfx,
        font: &mut Font,
        game: &Game,
        pet: &mut Pet,
        t: f32,
        dt: f32,
    ) {
        self.w = gfx.logical_size().0;
        self.h = gfx.logical_size().1;

        // Keep the pet in sync with the game state every frame: fed when every
        // quest is done, dormant when the board is empty, working while a
        // chronometer runs.
        let rem = game.s.quests.iter().filter(|q| !q.done).count();
        pet.set(
            !game.s.quests.is_empty() && rem == 0,
            game.s.quests.is_empty(),
            game.s.running,
        );

        if self.panel_open {
            self.panel_frame(gfx, font, game, t);
        }

        // --- Floating +XP pops ---------------------------------------------------
        self.pops.retain(|p| t - p.t0 <= 1.1);
        for p in &self.pops {
            let age = t - p.t0;
            let pr = age / 1.1;
            let ease = 1.0 - (1.0 - pr).powi(3);
            let alpha = 1.0 - pr * pr;
            font.draw(
                gfx,
                &p.text,
                p.x,
                p.y - 80.0 * ease,
                24.0,
                Vec4 {
                    r: C_XP.r,
                    g: C_XP.g,
                    b: C_XP.b,
                    a: alpha,
                },
                true,
            );
        }

        // --- Toast ---------------------------------------------------------------
        if self.toast_on {
            let age = t - self.toast_t0;
            if age > 2.8 {
                self.toast_on = false;
            } else {
                let tw = font.measure(&self.toast, 13.0) + 36.0;
                gfx.draw_rrect(
                    (self.w - tw) * 0.5,
                    12.0,
                    tw,
                    34.0,
                    14.0,
                    C_ACC,
                    C_PINK,
                    1.0,
                    C_NONE,
                    0.0,
                );
                font.draw(
                    gfx,
                    &self.toast,
                    (self.w - font.measure(&self.toast, 13.0)) * 0.5,
                    33.0,
                    13.0,
                    C_WHITE,
                    true,
                );
            }
        }

        // --- Confetti ------------------------------------------------------------
        if !self.confetti.is_empty() {
            for c in self.confetti.iter_mut() {
                c.x += c.vx * dt;
                c.y += c.vy * dt;
                c.vy += 0.45 * 60.0 * 60.0 * dt; // gravity (0.45 px/frame^2 in the JS)
                c.t -= dt;
            }
            self.confetti.retain(|c| c.t > 0.0);
            self.conf_scratch.clear();
            let half = 3.5 * gfx.pts_per_px(); // 7 device pixels
            for c in &self.confetti {
                let col = [c.c.r, c.c.g, c.c.b, c.c.a];
                let (x0, x1) = (c.x - half, c.x + half);
                let (y0, y1) = (c.y - half, c.y + half);
                self.conf_scratch
                    .push(crate::gfx::FlatVert { pos: [x0, y0], col });
                self.conf_scratch
                    .push(crate::gfx::FlatVert { pos: [x1, y0], col });
                self.conf_scratch
                    .push(crate::gfx::FlatVert { pos: [x1, y1], col });
                self.conf_scratch
                    .push(crate::gfx::FlatVert { pos: [x0, y0], col });
                self.conf_scratch
                    .push(crate::gfx::FlatVert { pos: [x1, y1], col });
                self.conf_scratch
                    .push(crate::gfx::FlatVert { pos: [x0, y1], col });
            }
            gfx.draw_flat(&self.conf_scratch, m4_identity());
        }
    }
}
