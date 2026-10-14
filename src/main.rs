//! Clawd Pet — entry point.
//!
//! A transparent, borderless, always-on-top window: Clawd lives on the
//! desktop. Drag him anywhere (the window follows the cursor); click him to
//! open the quest card above him (stats, quest list, add bar, close). While a
//! chronometer runs he works; left alone he dozes off after a while.
//!
//! winit owns windowing and events, wgpu (Metal on macOS) renders, cpal plays
//! audio.

mod app;
mod audio;
mod font;
mod fx;
mod game;
mod gfx;
mod math;
mod pet;
mod ui;

use std::sync::Arc;
use std::time::Instant;

use winit::dpi::{LogicalPosition, LogicalSize, PhysicalPosition};
use winit::event::{ElementState, Ime, MouseButton, MouseScrollDelta, WindowEvent};
use winit::event_loop::{ActiveEventLoop, ControlFlow, EventLoop};
use winit::keyboard::{KeyCode, ModifiersState, PhysicalKey};
use winit::window::{Window, WindowId};

use app::{PANEL_H, PANEL_W, PET_H, PET_W};
use audio::Audio;
use font::Font;
use game::Game;
use gfx::Gfx;
use pet::Pet;
use ui::{Key, Ui};

/// One monitor, physical pixels (x, y, w, h).
struct Monitor {
    x: i32,
    y: i32,
    w: u32,
    h: u32,
    primary: bool,
}

struct App {
    window: Arc<Window>,
    gfx: Gfx,
    font: Font,
    game: Game,
    pet: Pet,
    ui: Ui,
    audio: Audio,
    monitors: Vec<Monitor>,
    start: Instant,

    // frame pacing
    last_frame: Option<Instant>,
    tick_acc: f32,

    // interaction state (mouse down on Clawd, outside the card)
    panel: bool,
    pressing: bool,
    dragging: bool,
    drag_moved: bool,
    grab: (f32, f32), // cursor - window position, at grab (window points)
    press_pt: (f32, f32),
    press_t: Instant,
    cursor: (f32, f32),   // last cursor position (window points)
    mods: ModifiersState, // live modifier state, to reject shortcut keys as text

    save_pending: u32, // frames until the placement is persisted
    placed: bool,      // first resumed(): restore the pet's placement
}

impl App {
    fn new(event_loop: &EventLoop<()>, window: Arc<Window>) -> Self {
        let (spr, sw, sh) = pet::sprite_rgba();
        let (fnt, fw, fh) = font::font_rgba();
        // wgpu 29: InstanceDescriptor no longer implements Default — build it
        // from the event loop's display handle explicitly.
        let instance = wgpu::Instance::new(wgpu::InstanceDescriptor::new_with_display_handle(
            Box::new(event_loop.owned_display_handle()),
        ));
        let gfx = Gfx::new(instance, window.clone(), (&spr, sw, sh), (&fnt, fw, fh));

        let mut game = Game::new();
        game.init(); // must finish before the first frame (loads the save)
        let font = Font::new();
        let pet = Pet::new();
        let ui = Ui::new();
        let audio = Audio::new();

        Self {
            window,
            gfx,
            font,
            game,
            pet,
            ui,
            audio,
            monitors: Vec::new(),
            start: Instant::now(),
            last_frame: None,
            tick_acc: 0.0,
            panel: false,
            pressing: false,
            dragging: false,
            drag_moved: false,
            grab: (0.0, 0.0),
            press_pt: (0.0, 0.0),
            press_t: Instant::now(),
            cursor: (-1.0, -1.0),
            mods: ModifiersState::empty(),
            save_pending: 0,
            placed: false,
        }
    }

    fn t(&self) -> f32 {
        self.start.elapsed().as_secs_f64() as f32
    }

    // --- window placement -------------------------------------------------------

    fn pos_save(&self) {
        let Some(dir) = state_dir() else {
            return;
        };
        let _ = std::fs::create_dir_all(&dir);
        // Stored in points.
        let scale = self.window.scale_factor() as f32;
        if let Ok(pos) = self.window.outer_position() {
            let out = format!(
                "{} {}\n",
                (pos.x as f32 / scale) as i32,
                (pos.y as f32 / scale) as i32
            );
            let _ = std::fs::write(dir.join("pet.pos"), out);
        }
    }

    /// Persist the session and leave (save + placement, then quit).
    fn quit(&mut self, event_loop: &ActiveEventLoop) {
        self.game.save();
        self.pos_save();
        event_loop.exit();
    }

    fn set_panel(&mut self, open: bool) {
        if self.panel == open {
            return;
        }
        self.panel = open;
        self.ui.set_panel(open);
        self.pet.interact();

        let scale = self.window.scale_factor();
        let (w, h) = if open {
            (PANEL_W, PANEL_H)
        } else {
            (PET_W, PET_H)
        };
        let pos = self.window.outer_position().unwrap_or_default();
        let cur = self.window.inner_size();
        // Grow upward around Clawd: he stays bottom-center of the window, so
        // the feet stay fixed on screen. (winit 0.30 renamed set_inner_size to
        // request_inner_size.)
        let _ = self.window.request_inner_size(LogicalSize::new(w, h));
        let nw = (w * scale as f32) as i32;
        let nh = (h * scale as f32) as i32;
        self.window.set_outer_position(PhysicalPosition::new(
            pos.x + cur.width as i32 / 2 - nw / 2,
            pos.y + cur.height as i32 - nh,
        ));
        self.save_pending = 15; // window ops are async: persist once they settle
    }

    // --- one frame ----------------------------------------------------------------

    fn frame(&mut self) {
        let scale = self.window.scale_factor();
        let inner = self.window.inner_size(); // physical
        let lw = inner.to_logical::<f32>(scale).width;
        let lh = inner.to_logical::<f32>(scale).height;

        if self.save_pending > 0 {
            self.save_pending -= 1;
            if self.save_pending == 0 {
                self.pos_save();
            }
        }
        if self.ui.take_close_request() {
            self.set_panel(false);
        }

        let now = Instant::now();
        let mut dt = self
            .last_frame
            .map(|l| now.duration_since(l).as_secs_f32())
            .unwrap_or(1.0 / 60.0);
        self.last_frame = Some(now);
        if dt > 0.05 {
            dt = 0.05; // clamp: no jumps after a stall
        }
        let t = self.t();

        // Chronometer heartbeat, one second at a time while running.
        if self.game.s.running {
            self.tick_acc += dt;
            while self.tick_acc >= 1.0 {
                self.tick_acc -= 1.0;
                self.game.tick();
            }
        } else {
            self.tick_acc = 0.0;
        }

        self.gfx.begin(lw, lh, inner.width, inner.height);

        self.pet.frame(&mut self.gfx, &mut self.font, t, dt);
        self.ui.frame(
            &mut self.gfx,
            &mut self.font,
            &self.game,
            &mut self.pet,
            t,
            dt,
        );

        self.gfx.present();
    }
}

impl winit::application::ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.placed {
            return;
        }
        self.placed = true;
        // Monitors are only enumerable from the ActiveEventLoop (winit 0.30).
        let primary = event_loop.primary_monitor();
        self.monitors = event_loop
            .available_monitors()
            .map(|m| Monitor {
                x: m.position().x,
                y: m.position().y,
                w: m.size().width,
                h: m.size().height,
                primary: primary.as_ref().is_some_and(|p| *p == m),
            })
            .collect();
        // Restore the last placement, or park him at the bottom-right corner.
        let scale = self.window.scale_factor();
        if let Some((x, y)) = pos_load(&self.monitors, &self.window, scale) {
            self.window.set_outer_position(PhysicalPosition::new(x, y));
        } else if let Some((x, y)) = default_park(&self.monitors, scale) {
            self.window.set_outer_position(PhysicalPosition::new(x, y));
        }
        self.window.set_visible(true);
        // Text input is always on: the add bar owns the keyboard while the
        // card is open (text arrives via IME events).
        self.window.set_ime_allowed(true);
    }

    fn window_event(&mut self, event_loop: &ActiveEventLoop, _: WindowId, event: WindowEvent) {
        let scale = self.window.scale_factor();
        match event {
            WindowEvent::CloseRequested => self.quit(event_loop),
            WindowEvent::CursorMoved { position, .. } => {
                let pos: LogicalPosition<f32> = position.to_logical(scale);
                let (x, y) = (pos.x, pos.y);
                self.cursor = (x, y);
                self.ui.motion(x, y);
                // While pressed on Clawd, follow the cursor with the window:
                // moving the window keeps the cursor motion events coming, and
                // macOS delivers the whole drag to the window that received
                // the press.
                if self.pressing {
                    let (dx, dy) = (x - self.press_pt.0, y - self.press_pt.1);
                    if !self.drag_moved && dx.abs() + dy.abs() > 6.0 {
                        self.drag_moved = true;
                    }
                    if self.drag_moved && !self.dragging {
                        self.dragging = true;
                        self.pet.drag(true);
                    }
                    if self.dragging {
                        // Keep the cursor at the grab offset: move the window
                        // by the same delta the cursor moved within it.
                        let ddx = ((x - self.grab.0) * scale as f32) as i32;
                        let ddy = ((y - self.grab.1) * scale as f32) as i32;
                        if let Ok(pos) = self.window.outer_position() {
                            self.window.set_outer_position(PhysicalPosition::new(
                                pos.x + ddx,
                                pos.y + ddy,
                            ));
                        }
                    }
                }
            }
            WindowEvent::MouseInput {
                state: ElementState::Pressed,
                button: MouseButton::Left,
                ..
            } => {
                let (x, y) = self.cursor;
                if self.ui.on_card(x, y) {
                    self.ui.mouse_down(x, y);
                } else if !self.pressing {
                    self.pressing = true;
                    self.drag_moved = false;
                    // grab offset: cursor position within the window (points)
                    self.grab = (x, y);
                    self.press_pt = (x, y);
                    self.press_t = Instant::now();
                    self.pet.interact();
                }
            }
            WindowEvent::MouseInput {
                state: ElementState::Released,
                button: MouseButton::Left,
                ..
            } => {
                let (x, y) = self.cursor;
                let t = self.t();
                self.ui
                    .mouse_up(x, y, &mut self.game, &mut self.pet, &self.audio, t);
                if self.pressing {
                    self.pressing = false;
                    let dur = self.press_t.elapsed().as_secs_f64();
                    if !self.drag_moved && dur < 0.45 {
                        // A pat on the head: toggle the quest card.
                        let open = !self.ui.panel_open();
                        self.set_panel(open);
                        let t = self.t();
                        self.pet.click(t);
                        self.audio.sfx_pet();
                        self.pet.interact();
                    }
                    if self.dragging {
                        self.dragging = false;
                        self.pet.drag(false);
                        self.pos_save();
                    }
                }
            }
            WindowEvent::MouseWheel { delta, .. } => {
                let dy = match delta {
                    MouseScrollDelta::LineDelta(_, y) => y,
                    MouseScrollDelta::PixelDelta(px) => px.y as f32 / 40.0,
                };
                self.ui.wheel(dy);
            }
            WindowEvent::KeyboardInput { event, .. } => {
                if event.state == ElementState::Pressed {
                    // Plain typing arrives as KeyboardInput text on macOS; only
                    // real IME composition (marked text) comes through Ime.
                    // Skip control/Cmd shortcuts so they don't type letters.
                    if !self.mods.control_key()
                        && !self.mods.super_key()
                        && let Some(txt) = event.text.as_deref()
                    {
                        self.ui.text(txt);
                    }
                    if let PhysicalKey::Code(code) = event.physical_key {
                        let t = self.t();
                        match code {
                            KeyCode::Escape if self.panel => self.set_panel(false),
                            KeyCode::Backspace if self.ui.panel_open() => self.ui.key(
                                Key::Backspace,
                                &mut self.game,
                                &mut self.pet,
                                &self.audio,
                                t,
                            ),
                            KeyCode::Enter | KeyCode::NumpadEnter if self.ui.panel_open() => self
                                .ui
                                .key(Key::Enter, &mut self.game, &mut self.pet, &self.audio, t),
                            _ => {}
                        }
                    }
                    self.pet.interact();
                }
            }
            WindowEvent::ModifiersChanged(m) => {
                self.mods = m.state();
            }
            WindowEvent::Ime(Ime::Commit(s)) => {
                self.ui.text(&s);
            }
            _ => {}
        }
    }

    fn about_to_wait(&mut self, _event_loop: &ActiveEventLoop) {
        self.frame();
    }
}

// --- window placement helpers ---------------------------------------------------

/// Clawd's state directory (`~/.clawdquest`), if HOME is known.
fn state_dir() -> Option<std::path::PathBuf> {
    std::env::var_os("HOME").map(|h| std::path::Path::new(&h).join(".clawdquest"))
}

/// The monitor containing a point (physical px), or the primary monitor.
fn monitor_at(monitors: &[Monitor], x: i32, y: i32) -> Option<&Monitor> {
    monitors
        .iter()
        .find(|m| x >= m.x && x < m.x + m.w as i32 && y >= m.y && y < m.y + m.h as i32)
        .or_else(|| monitors.iter().find(|m| m.primary))
}

/// Approximate the usable desktop area: the monitor minus the menu bar (top)
/// and a typical dock (bottom). winit does not expose either.
fn usable_bounds(scale: f64, m: &Monitor) -> (i32, i32, i32, i32) {
    let top = (25.0 * scale as f32) as i32;
    let bottom = (90.0 * scale as f32) as i32;
    let w = m.w as i32;
    let h = (m.h as i32 - top - bottom).max(0);
    (m.x, m.y + top, w, h)
}

/// Keep the pet on a real display (positions are global desktop coordinates,
/// physical pixels).
fn pos_clamp(monitors: &[Monitor], window: &Window, x: &mut i32, y: &mut i32) {
    let scale = window.scale_factor();
    let pw = (PET_W * scale as f32) as i32;
    let ph = (PET_H * scale as f32) as i32;
    if let Some(m) = monitor_at(monitors, *x + pw / 2, *y + ph / 2) {
        let (ux, uy, uw, uh) = usable_bounds(scale, m);
        *x = (*x).clamp(ux, ux + uw - pw);
        *y = (*y).clamp(uy, uy + uh - ph);
    }
}

/// Read the saved placement (points) -> physical px.
fn pos_load(monitors: &[Monitor], window: &Window, scale: f64) -> Option<(i32, i32)> {
    let text = std::fs::read_to_string(state_dir()?.join("pet.pos")).ok()?;
    let mut it = text.split_whitespace();
    let x: i32 = it.next()?.parse().ok()?;
    let y: i32 = it.next()?.parse().ok()?;
    let mut px = (x as f64 * scale) as i32;
    let mut py = (y as f64 * scale) as i32;
    pos_clamp(monitors, window, &mut px, &mut py);
    Some((px, py))
}

/// Default parking spot: the bottom-right of the primary display's usable
/// area, 48pt in.
fn default_park(monitors: &[Monitor], scale: f64) -> Option<(i32, i32)> {
    let m = monitors.iter().find(|m| m.primary)?;
    let (ux, uy, uw, uh) = usable_bounds(scale, m);
    let pw = (PET_W * scale as f32) as i32;
    let ph = (PET_H * scale as f32) as i32;
    let margin = (48.0 * scale as f32) as i32;
    Some((ux + uw - pw - margin, uy + uh - ph - margin))
}

fn main() {
    let event_loop = EventLoop::new().unwrap();
    event_loop.set_control_flow(ControlFlow::Poll);

    let attrs = Window::default_attributes()
        .with_title("Clawd Pet")
        .with_inner_size(LogicalSize::new(PET_W, PET_H))
        .with_decorations(false) // borderless
        .with_transparent(true) // the desktop shows through
        .with_resizable(false)
        .with_visible(false) // position first, then show
        .with_window_level(winit::window::WindowLevel::AlwaysOnTop);
    // EventLoop::create_window is deprecated in winit 0.30 but is the only way
    // to create the window before run() — needed so the surface can be built
    // up front. The monitors are enumerated in resumed() instead (they moved
    // to ActiveEventLoop).
    #[allow(deprecated)]
    let window = Arc::new(event_loop.create_window(attrs).expect("create window"));

    let mut app = App::new(&event_loop, window);
    event_loop.run_app(&mut app).unwrap();
}
