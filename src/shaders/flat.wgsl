// Flat colored 2D triangles: the pet's shadow and apple, sparks, confetti.
// Positions arrive already in pixel space (CPU-transformed).
struct Globals {
    ortho: mat4x4<f32>,
};
@group(0) @binding(0) var<uniform> globals: Globals;

struct VOut {
    @location(0) v_col: vec4<f32>,
    @builtin(position) pos: vec4<f32>,
};

@vertex
fn vs(@location(0) a_pos: vec2<f32>, @location(1) a_col: vec4<f32>) -> VOut {
    var o: VOut;
    o.v_col = a_col;
    o.pos = globals.ortho * vec4(a_pos, 0.0, 1.0);
    return o;
}

@fragment
fn fs(v: VOut) -> @location(0) vec4<f32> {
    return v.v_col;
}
