//! The effects game rules trigger on the UI/pet/audio side. The trait keeps
//! game.rs decoupled and testable while the app wires it up.

pub trait FxSink {
    fn pop(&mut self, text: &str, x: f32, y: f32);
    fn toast(&mut self, text: &str);
    fn confetti(&mut self, n: usize);
    fn feed(&mut self);
    fn win(&mut self);
    fn sfx_hit(&mut self);
    fn sfx_win(&mut self);
    fn sfx_add(&mut self);
}
