// Atlas text: coverage in the texture's alpha channel, per-vertex color.
struct Globals {
    ortho: mat4x4<f32>,
};
@group(0) @binding(0) var<uniform> globals: Globals;
@group(0) @binding(1) var tex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;

struct VOut {
    @location(0) v_uv: vec2<f32>,
    @location(1) v_col: vec4<f32>,
    @builtin(position) pos: vec4<f32>,
};

@vertex
fn vs(@location(0) a_pos: vec2<f32>, @location(1) a_uv: vec2<f32>,
      @location(2) a_col: vec4<f32>) -> VOut {
    var o: VOut;
    o.v_uv = a_uv;
    o.v_col = a_col;
    o.pos = globals.ortho * vec4(a_pos, 0.0, 1.0);
    return o;
}

@fragment
fn fs(v: VOut) -> @location(0) vec4<f32> {
    let a = textureSample(tex, samp, v.v_uv).a;
    return vec4(v.v_col.rgb, v.v_col.a * a);
}
