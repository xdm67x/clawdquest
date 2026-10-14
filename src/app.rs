//! Shared window/panel geometry.
//!
//! Logical window size in points (the card and the pet are laid out in these)
//! and the framebuffer size in pixels (for scissor clipping). The pet window
//! has two fixed sizes (points): PET_* is Clawd alone; PANEL_* adds the quest
//! card above him.

pub const PET_SPR: f32 = 64.0; // drawn sprite size in points
pub const PET_W: f32 = 88.0;
pub const PET_H: f32 = 92.0;
pub const PANEL_W: f32 = 340.0;
// The pet is bottom-anchored, so this also sets the card-to-pet gap: the card
// ends at y=284, Clawd's cell top lands at PANEL_H-76 and his body just below
// it. Keep it small so the two read as one unit.
pub const PANEL_H: f32 = 364.0;

// Clawd's framing inside the window: feet this far above the bottom edge.
pub const PET_FEET_MARGIN: f32 = 12.0;

/// Screen-space y of the top of Clawd's head (points, y down from 0).
pub fn pet_head_top(h: f32) -> f32 {
    h - PET_FEET_MARGIN - PET_SPR
}
