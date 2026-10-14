// Rounded-rect shader: SDF corners, optional gradient fill, 1px-ish border.
// A port of the original RR_VS/RR_FS (GLSL 150): the shader outputs
// premultiplied alpha, so the pipeline blends premultiplied source-over.
// Positions arrive in pixel space; the rect parameters come from a small
// dedicated uniform (dynamic-offset bound).
struct Globals {
    ortho: mat4x4<f32>,
};
@group(0) @binding(0) var<uniform> globals: Globals;

struct RR {
    rect: vec4<f32>,   // x y w h
    params: vec4<f32>, // x: corner radius, y: border width, z: gaxis, w: _
    fill: vec4<f32>,
    fill2: vec4<f32>,   // gradient end
    border: vec4<f32>,
};
@group(1) @binding(0) var<uniform> u: RR;

struct VOut {
    @location(0) v_p: vec2<f32>,
    @builtin(position) pos: vec4<f32>,
};

@vertex
fn vs(@location(0) a_pos: vec2<f32>) -> VOut {
    var o: VOut;
    o.v_p = a_pos;
    o.pos = globals.ortho * vec4(a_pos, 0.0, 1.0);
    return o;
}

fn sd(p: vec2<f32>) -> f32 {
    let h = u.rect.zw * 0.5 - vec2(u.params.x);
    let q = abs(p) - h;
    return length(max(q, vec2<f32>(0.0))) + min(max(q.x, q.y), 0.0) - u.params.x;
}

@fragment
fn fs(v: VOut) -> @location(0) vec4<f32> {
    let c = v.v_p - (u.rect.xy + u.rect.zw * 0.5);
    let d = sd(c);
    let inside = 1.0 - smoothstep(-0.5, 0.5, d);
    let band = 1.0 - smoothstep(u.params.y - 0.5, u.params.y + 0.5, abs(d));
    // gaxis: 0 none, 1 horizontal, 2 vertical
    var g = 0.0;
    if (u.params.z > 1.5) {
        g = clamp((v.v_p.y - u.rect.y) / max(u.rect.w, 1.0), 0.0, 1.0);
    } else if (u.params.z > 0.5) {
        g = clamp((v.v_p.x - u.rect.x) / max(u.rect.z, 1.0), 0.0, 1.0);
    }
    var fill = mix(u.fill, u.fill2, g);
    fill.a = fill.a * inside;
    let b = vec4(u.border.rgb, u.border.a * band);
    return vec4(
        b.rgb * b.a + fill.rgb * fill.a * (1.0 - b.a),
        b.a + fill.a * (1.0 - b.a),
    );
}
