// ---------------------------------------------------------------------------
// Clawd the desktop pet — 2D sprite renderer.
//
// Clawd is drawn from the Icons8 mood icons packed into one RGBA atlas
// (src/sprites.h, built by tools/mksprites.swift). The window is
// transparent, so there is no arena: just the sprite, a soft shadow, sparks,
// floating zZz while dozing, and the apple he eats when a quest completes.
// Animations (bob, tilt, hop, spin, pop-in) transform the flat sprite.
// ---------------------------------------------------------------------------

#include "pet.h"

#include <SDL3/SDL.h>
#include "glutil.h"
#include <math.h>
#include <stdlib.h>

#include "app.h"
#include "font.h"
#include "math.h"
#include "sprites.h"

// ---------------------------------------------------------------------------
// Shaders: textured 2D quads (sprites), flat colored 2D triangles (shadow,
// apple), and 2D points (sparks).
// ---------------------------------------------------------------------------

static const char *SPR_VS =
    "#version 150\n"
    "in vec2 a_pos;\n"
    "in vec2 a_uv;\n"
    "uniform mat4 u_mvp;\n"
    "uniform mat4 u_model;\n"
    "uniform vec4 u_uv;   // x0 y0 x1 y1\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = u_uv.xy + a_uv * (u_uv.zw - u_uv.xy);\n"
    "    gl_Position = u_mvp * u_model * vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char *SPR_FS =
    "#version 150\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec4 u_tint;\n"
    "out vec4 frag;\n"
    "void main() {\n"
    // The atlas is premultiplied (so GL_LINEAR filtering has no dark
    // fringes); the tint must premultiply the color the same way.
    "    vec4 c = texture(u_tex, v_uv);\n"
    "    frag = vec4(u_tint.rgb * c.rgb, u_tint.a * c.a);\n"
    "}\n";

static const char *FLAT_VS =
    "#version 150\n"
    "in vec2 a_pos;\n"
    "in vec4 a_col;\n"
    "uniform mat4 u_mvp;\n"
    "uniform mat4 u_model;\n"
    "out vec4 v_col;\n"
    "void main() {\n"
    "    v_col = a_col;\n"
    "    gl_Position = u_mvp * u_model * vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char *FLAT_FS =
    "#version 150\n"
    "in vec4 v_col;\n"
    "out vec4 frag;\n"
    "void main() { frag = v_col; }\n";

static const char *PT_VS =
    "#version 150\n"
    "in vec2 a_pos;\n"
    "in vec4 a_col;\n"
    "in float a_size;\n"
    "uniform mat4 u_mvp;\n"
    "out vec4 v_col;\n"
    "void main() {\n"
    "    v_col = a_col;\n"
    "    gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);\n"
    "    gl_PointSize = a_size;\n"
    "}\n";

#define PT_FS FLAT_FS

// ---------------------------------------------------------------------------
// Pet states and their motion + atlas sprite.
// ---------------------------------------------------------------------------

enum { V_IDLE, V_WORK, V_DOZE, V_WAKE, V_HAPPY, V_FED, V_EAT, V_DIZZY, V_PARTY };

static const int SPR_OF[] = {
    SPR_IDLE, SPR_WORK, SPR_DOZE, SPR_WAKE, SPR_HAPPY,
    SPR_FED, SPR_EAT, SPR_DIZZY, SPR_PARTY,
};

// bob: fraction of sprite height, jump: hop fraction, spin: rad/s.
typedef struct { float bobA, bobF, tilt, jumpF, spin; } Motion;
static const Motion MOTION[9] = {
    [V_IDLE] = { 0.020f, 1.6f, 0.030f, 0, 0 },
    [V_WORK] = { 0.012f, 6.0f, 0.015f, 0, 0 },
    [V_DOZE] = { 0.025f, 0.9f, 0.045f, 0, 0 },
    [V_WAKE] = { 0.015f, 2.0f, 0.025f, 0.12f, 0 },
    [V_HAPPY] = { 0.010f, 1.0f, 0.020f, 0.12f, 0 },
    [V_FED] = { 0.018f, 1.2f, 0.030f, 0, 0 },
    [V_EAT] = { 0.012f, 8.0f, 0.020f, 0, 0 },
    [V_DIZZY] = { 0.015f, 2.0f, 0.050f, 0, 1.2f },
    [V_PARTY] = { 0.010f, 1.5f, 0.025f, 0.10f, 0 },
};

// How big the (square) sprite is drawn, in points. The atlas cell is an
// integer multiple of this (2x, so it maps 1:1 to device pixels on Retina);
// the typedef below fails to compile if the two ever drift apart.
#define SPR_PX PET_SPR
typedef char pet_atlas_cell_multiple[SPR_CELL % PET_SPR == 0 ? 1 : -1];

// ---------------------------------------------------------------------------
// GL state
// ---------------------------------------------------------------------------

static GLuint spr_prog, flat_prog, pt_prog;
static GLint spr_u_mvp, spr_u_model, spr_u_uv, spr_u_tint, spr_u_tex;
static GLint flat_u_mvp, flat_u_model, pt_u_mvp;
static GLuint spr_tex, spr_vbo;
static GLuint shadow_vbo;
static GLuint apple_vbo;
static int apple_nverts;
static GLuint sparks_vbo;

#define SPARK_N 160
static float sp_x[SPARK_N], sp_y[SPARK_N], sp_vx[SPARK_N], sp_vy[SPARK_N], sp_life[SPARK_N];
static Vec4 sp_col[SPARK_N];

// Pet animation state.
static int shown = V_IDLE;
static float revert_at = -1;
static int revert_to = V_IDLE;
static float pop_t = 1, phase = 0, spin_acc = 0;
static bool fed_flag, dormant_flag, working_flag, drag_flag;
static float idle_secs;            // seconds since the last interaction
static float blink_t = 3, blink_on = 0;

// The apple animation: 0 none, 1 approach, 2 chomp.
static int feed_state;
static float feed_t;

static float rnd(void) { return (float)rand() / (float)RAND_MAX; }

static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// Push one flat triangle (x, y, r, g, b, a per vertex) into a growing buffer.
static int push_tri(float *v, int n, const float t[3][6]) {
    for (int k = 0; k < 3; k++)
        for (int f = 0; f < 6; f++)
            v[(n + k) * 6 + f] = t[k][f];
    return n + 3;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void pet_init(void) {
    static const char *spr_attribs[] = { "a_pos", "a_uv", NULL };
    static const char *flat_attribs[] = { "a_pos", "a_col", NULL };
    static const char *pt_attribs[] = { "a_pos", "a_col", "a_size", NULL };
    spr_prog = gl_program_at(SPR_VS, SPR_FS, spr_attribs);
    spr_u_mvp = glGetUniformLocation(spr_prog, "u_mvp");
    spr_u_model = glGetUniformLocation(spr_prog, "u_model");
    spr_u_uv = glGetUniformLocation(spr_prog, "u_uv");
    spr_u_tint = glGetUniformLocation(spr_prog, "u_tint");
    spr_u_tex = glGetUniformLocation(spr_prog, "u_tex");
    flat_prog = gl_program_at(FLAT_VS, FLAT_FS, flat_attribs);
    flat_u_mvp = glGetUniformLocation(flat_prog, "u_mvp");
    flat_u_model = glGetUniformLocation(flat_prog, "u_model");
    pt_prog = gl_program_at(PT_VS, PT_FS, pt_attribs);
    pt_u_mvp = glGetUniformLocation(pt_prog, "u_mvp");

    // Decode the base64 atlas into an RGBA texture.
    {
        int nb = SPR_B64_LEN / 4 * 3;
        unsigned char *px = SDL_malloc((size_t)nb);
        for (int i = 0, o = 0; i < SPR_B64_LEN; i += 4) {
            int v0 = b64_val(SPR_B64[i]), v1 = b64_val(SPR_B64[i + 1]);
            int v2 = b64_val(SPR_B64[i + 2]), v3 = b64_val(SPR_B64[i + 3]);
            unsigned v = (unsigned)v0 << 18 | (unsigned)v1 << 12 | (unsigned)v2 << 6 | (unsigned)v3;
            px[o++] = (unsigned char)(v >> 16);
            if (SPR_B64[i + 2] != '=') px[o++] = (unsigned char)(v >> 8);
            if (SPR_B64[i + 3] != '=') px[o++] = (unsigned char)v;
        }
        glGenTextures(1, &spr_tex);
        glBindTexture(GL_TEXTURE_2D, spr_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SPR_W, SPR_H, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, px);
        SDL_free(px);
    }

    // Sprite quad: 2 tris, unit square, uv 0..1.
    {
        const float q[6][4] = {
            { 0, 0, 0, 0 }, { 1, 0, 1, 0 }, { 1, 1, 1, 1 },
            { 0, 0, 0, 0 }, { 1, 1, 1, 1 }, { 0, 1, 0, 1 },
        };
        glGenBuffers(1, &spr_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, spr_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
    }

    // Soft shadow: a squashed fan, alpha fading from the center to the rim
    // (positions in apple-cell units; scaled by `cell` at draw time).
    {
        float v[34 * 3 * 6];
        int n = 0;
        float tri[3][6] = {
            { 0, 0, 0, 0, 0, 0.22f },
            { 0, 0, 0, 0, 0, 0 },
            { 0, 0, 0, 0, 0, 0 },
        };
        for (int i = 0; i < 32; i++) {
            float a0 = (float)i / 32 * 2.0f * (float)M_PI;
            float a1 = (float)(i + 1) / 32 * 2.0f * (float)M_PI;
            tri[1][0] = cosf(a0) * 3.6f; tri[1][1] = sinf(a0) * 1.1f;
            tri[2][0] = cosf(a1) * 3.6f; tri[2][1] = sinf(a1) * 1.1f;
            n = push_tri(v, n, tri);
        }
        glGenBuffers(1, &shadow_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, shadow_vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * 6 * sizeof(float)), v, GL_STATIC_DRAW);
    }

    // The apple: body + highlight discs, stem and leaf quads, in apple-local
    // units (body radius 1). All one mesh, colored per vertex.
    {
        float v[256 * 6];
        int n = 0;
        static const Vec3 RED = { 1.0f, 0.302f, 0.427f };    // 0xff4d6d
        static const Vec3 HI = { 1.0f, 0.502f, 0.596f };     // 0xff8098
        static const Vec3 STEM = { 0.42f, 0.247f, 0.114f }; // 0x6b3f1d
        static const Vec3 LEAF = { 0.239f, 0.863f, 0.592f };  // 0x3ddc97
        float tri[3][6];
        // Body: a slightly egg-shaped fan (wider at the bottom).
        for (int i = 0; i < 24; i++) {
            float a0 = (float)i / 24 * 2.0f * (float)M_PI;
            float a1 = (float)(i + 1) / 24 * 2.0f * (float)M_PI;
            float r0 = 1.0f - 0.12f * sinf(a0);
            float r1 = 1.0f - 0.12f * sinf(a1);
            const float pts[3][3] = {
                { 0, -0.15f, r0 }, { cosf(a0) * r0, -0.15f + sinf(a0) * r0, 0 }, { cosf(a1) * r1, -0.15f + sinf(a1) * r1, 0 },
            };
            for (int k = 0; k < 3; k++) {
                tri[k][0] = pts[k][0];
                tri[k][1] = pts[k][1];
                tri[k][2] = RED.x; tri[k][3] = RED.y; tri[k][4] = RED.z; tri[k][5] = 1;
            }
            n = push_tri(v, n, tri);
        }
        // Highlight spot, upper left.
        for (int i = 0; i < 12; i++) {
            float a0 = (float)i / 12 * 2.0f * (float)M_PI;
            float a1 = (float)(i + 1) / 12 * 2.0f * (float)M_PI;
            const float pts[3][3] = {
                { -0.42f, 0.35f, 0.24f },
                { -0.42f + cosf(a0) * 0.24f, 0.35f + sinf(a0) * 0.24f, 0 },
                { -0.42f + cosf(a1) * 0.24f, 0.35f + sinf(a1) * 0.24f, 0 },
            };
            for (int k = 0; k < 3; k++) {
                tri[k][0] = pts[k][0];
                tri[k][1] = pts[k][1];
                tri[k][2] = HI.x; tri[k][3] = HI.y; tri[k][4] = HI.z; tri[k][5] = 1;
            }
            n = push_tri(v, n, tri);
        }
        // Stem.
        {
            const float pts[3][3] = { { 0, 0.78f, 0 }, { 0.09f, 1.05f, 0 }, { -0.09f, 1.05f, 0 } };
            for (int k = 0; k < 3; k++) {
                tri[k][0] = pts[k][0]; tri[k][1] = pts[k][1];
                tri[k][2] = STEM.x; tri[k][3] = STEM.y; tri[k][4] = STEM.z; tri[k][5] = 1;
            }
            n = push_tri(v, n, tri);
            const float pts2[3][3] = { { 0, 0.78f, 0 }, { 0.14f, 0.82f, 0 }, { 0.09f, 1.05f, 0 } };
            for (int k = 0; k < 3; k++) {
                tri[k][0] = pts2[k][0]; tri[k][1] = pts2[k][1];
                tri[k][2] = STEM.x; tri[k][3] = STEM.y; tri[k][4] = STEM.z; tri[k][5] = 1;
            }
            n = push_tri(v, n, tri);
        }
        // Leaf, tilted beside the stem.
        {
            const float pts[3][3] = { { 0.16f, 1.02f, 0 }, { 0.62f, 1.06f, 0 }, { 0.30f, 1.22f, 0 } };
            for (int k = 0; k < 3; k++) {
                tri[k][0] = pts[k][0]; tri[k][1] = pts[k][1];
                tri[k][2] = LEAF.x; tri[k][3] = LEAF.y; tri[k][4] = LEAF.z; tri[k][5] = 1;
            }
            n = push_tri(v, n, tri);
            const float pts2[3][3] = { { 0.16f, 1.02f, 0 }, { 0.44f, 0.92f, 0 }, { 0.62f, 1.06f, 0 } };
            for (int k = 0; k < 3; k++) {
                tri[k][0] = pts2[k][0]; tri[k][1] = pts2[k][1];
                tri[k][2] = LEAF.x; tri[k][3] = LEAF.y; tri[k][4] = LEAF.z; tri[k][5] = 1;
            }
            n = push_tri(v, n, tri);
        }
        apple_nverts = n;
        glGenBuffers(1, &apple_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, apple_vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * 6 * sizeof(float)), v, GL_STATIC_DRAW);
    }

    glGenBuffers(1, &sparks_vbo);
}

static void show(int name) {
    if (shown == name) return;
    shown = name;
    pop_t = 0;
    phase = 0;
}

static void cameo(int name, int then, float ms, float t) {
    show(name);
    revert_to = then;
    revert_at = t + ms / 1000.0f;
}

static void burst(int n, int hex) {
    for (int i = 0, cnt = 0; i < SPARK_N && cnt < n; i++) {
        if (sp_life[i] <= 0) {
            cnt++;
            sp_life[i] = 0.6f + rnd() * 0.3f;
            sp_x[i] = app_w * 0.5f + (rnd() - 0.5f) * 40.0f;
            sp_y[i] = (float)PET_HEAD_TOP(app_h) + 30.0f + (rnd() - 0.5f) * 40.0f;
            float a = rnd() * 6.28f;
            float s = 60 + rnd() * 160;
            sp_vx[i] = cosf(a) * s;
            sp_vy[i] = sinf(a) * s - 80;
            Vec3 c = hex3(hex);
            sp_col[i] = (Vec4){ c.x, c.y, c.z, 1 };
        }
    }
}

void pet_feed(void) {
    if (!feed_state) {
        feed_state = 1;
        feed_t = 0;
    }
}

void pet_win(void) {
    cameo(V_PARTY, V_FED, 2200, (float)SDL_GetTicks() / 1000.0f);
    burst(20, 0xff4d6d);
    burst(20, 0x3ddc97);
    burst(20, 0xffbd2e);
}

static int steady_variant(void) {
    return working_flag ? V_WORK : fed_flag ? V_FED
         : (dormant_flag || idle_secs > 300.0f) ? V_DOZE : V_IDLE;
}

void pet_set(bool fed, bool dormant, bool working) {
    fed_flag = fed;
    dormant_flag = dormant;
    working_flag = working;
}

void pet_click(void) {
    cameo(V_HAPPY, steady_variant(), 1500, (float)SDL_GetTicks() / 1000.0f);
}

void pet_drag(bool on) {
    drag_flag = on;
}

void pet_interact(void) {
    idle_secs = 0;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

static void bind_flat_attribs(void) {
    GLsizei stride = 6 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void *)(2 * sizeof(float)));
}

void pet_frame(float t, float dt) {
    Mat4 ortho;
    m4_ortho(ortho, (float)app_w, (float)app_h);

    idle_secs += dt;

    // Variant state machine: a cameo plays out, dragging wins, else steady.
    if (revert_at >= 0 && t >= revert_at) {
        revert_at = -1;
        show(revert_to);
    }
    if (drag_flag) {
        if (revert_at >= 0) revert_at = -1;
        show(V_DIZZY);
    } else if (revert_at < 0) {
        int steady = steady_variant();
        if (shown == V_DOZE && steady != V_DOZE)
            cameo(V_WAKE, steady, 1800, t);
        else
            show(steady);
    }

    // The apple: approach (0.6s), then three chomps (0.75s), then a hop.
    if (feed_state) {
        feed_t += dt;
        if (feed_state == 1 && feed_t >= 0.6f) {
            feed_state = 2;
            feed_t = 0;
        } else if (feed_state == 2 && feed_t >= 0.75f) {
            feed_state = 0;
            cameo(V_HAPPY, steady_variant(), 1300, t);
            burst(12, 0xff4d6d);
            burst(12, 0x3ddc97);
        }
    }

    // A quick squash "blink" now and then while awake.
    if (shown == V_IDLE || shown == V_WORK) {
        if (blink_on > 0) {
            blink_on -= dt;
        } else {
            blink_t -= dt;
            if (blink_t <= 0) {
                blink_on = 0.14f;
                blink_t = 2.5f + rnd() * 4.0f;
            }
        }
    }
    int disp = shown;
    if (feed_state == 2) disp = V_EAT;

    // Layout: sprite feet on the bottom margin.
    float cx = app_w * 0.5f;
    float feet_y = (float)app_h - (float)PET_FEET_MARGIN;

    const Motion M = MOTION[disp];
    phase += dt;
    pop_t += dt * 4;
    if (pop_t > 1) pop_t = 1;
    float pop = 1 - powf(1 - pop_t, 3);

    float bob = sinf(phase * M.bobF) * M.bobA;
    if (M.jumpF) bob += fabsf(sinf(phase * M.jumpF)) * M.jumpF;
    if (feed_state == 2) bob += 0.012f * fabsf(sinf(feed_t * 22.0f)); // chomp wobble
    spin_acc += M.spin * dt;
    float rotz = sinf(phase * 1.1f) * M.tilt;

    float sx = pop, sy = pop;
    if (blink_on > 0 && (shown == V_IDLE || shown == V_WORK)) {
        sx *= 1.04f;
        sy *= 0.90f;
    }

    glEnable(GL_BLEND);
    // Source-over: alpha must accumulate as coverage (a out = a + dst.a*(1-a)),
    // or the shadow would end up nearly invisible on the desktop.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    // --- Shadow (drawn under the sprite, in PET_SPR cell units) --------------
    float cell = (float)PET_SPR / 9.0f;
    {
        Mat4 m, s_, t_;
        m4_scale(s_, cell, cell, 1);
        m4_translate(t_, cx, feet_y + 0.3f, 0);
        m4_mul(m, t_, s_);
        glUseProgram(flat_prog);
        glUniformMatrix4fv(flat_u_mvp, 1, GL_FALSE, ortho);
        glUniformMatrix4fv(flat_u_model, 1, GL_FALSE, m);
        glBindBuffer(GL_ARRAY_BUFFER, shadow_vbo);
        bind_flat_attribs();
        glDrawArrays(GL_TRIANGLES, 0, 32 * 3);
    }

    // --- Clawd -----------------------------------------------------------------
    {
        Mat4 m, t_, rz, s_;
        m4_translate(t_, -0.5f, -1.0f, 0); // bottom-center pivot
        m4_rotZ(rz, rotz);
        m4_mul(m, rz, t_);
        m4_scale(s_, (float)SPR_PX * sx, (float)SPR_PX * sy, 1);
        m4_mul(m, s_, m);
        m4_translate(t_, cx, feet_y - bob * (float)SPR_PX, 0);
        m4_mul(m, t_, m);

        int spr = SPR_OF[disp];
        float u0 = (float)(spr % SPR_COLS) / SPR_COLS;
        float v0 = (float)(spr / SPR_COLS) / SPR_ROWS;
        glUseProgram(spr_prog);
        // The atlas is premultiplied: blend premultiplied source-over.
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
                            GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        // Sample half a texel inside the cell: at the exact uv boundary the
        // LINEAR filter would pull in the neighboring icon's edge pixels
        // (their orange arms reach the cell border) as an orange fringe.
        float eu = 0.5f / (float)SPR_W, ev = 0.5f / (float)SPR_H;
        glUniform4f(spr_u_uv, u0 + eu, v0 + ev,
                    u0 + 1.0f / SPR_COLS - eu, v0 + 1.0f / SPR_ROWS - ev);
        glUniformMatrix4fv(spr_u_mvp, 1, GL_FALSE, ortho);
        glUniformMatrix4fv(spr_u_model, 1, GL_FALSE, m);
        glUniform4f(spr_u_tint, 1, 1, 1, 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, spr_tex);
        glUniform1i(spr_u_tex, 0);
        glBindBuffer(GL_ARRAY_BUFFER, spr_vbo);
        GLsizei stride = 4 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void *)(2 * sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        // Back to straight alpha for the flat meshes below.
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                            GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }

    // --- The apple -------------------------------------------------------------
    if (feed_state) {
        float ax, ay, ascale = 1.0f, arot = 0;
        // The mouth is around 44% up the sprite.
        float mouth_x = cx + (float)SPR_PX * 0.06f;
        float mouth_y = feet_y - (float)SPR_PX * 0.44f;
        if (feed_state == 1) {
            float p = feed_t / 0.6f;
            if (p > 1) p = 1;
            float ease = 1 - powf(1 - p, 3);
            float sx0 = cx + 40.0f, sy0 = feet_y + 24.0f;
            ax = sx0 + (mouth_x - sx0) * ease + sinf(p * (float)M_PI) * 14.0f;
            ay = sy0 + (mouth_y - sy0) * ease;
            arot = sinf(p * (float)M_PI * 2.0f) * 0.4f;
        } else {
            ax = mouth_x;
            ay = mouth_y;
            ascale = 1.0f - feed_t / 0.75f; // each chomp takes a bite
            arot = sinf(feed_t * 22.0f) * 0.15f;
        }
        float s = cell * 0.78f * ascale;
        Mat4 m, t_, rz, s_;
        m4_rotZ(rz, arot);
        m4_scale(s_, s, s, 1);
        m4_mul(m, s_, rz);
        m4_translate(t_, ax, ay, 0);
        m4_mul(m, t_, m);
        glUseProgram(flat_prog);
        glUniformMatrix4fv(flat_u_mvp, 1, GL_FALSE, ortho);
        glUniformMatrix4fv(flat_u_model, 1, GL_FALSE, m);
        glBindBuffer(GL_ARRAY_BUFFER, apple_vbo);
        bind_flat_attribs();
        glDrawArrays(GL_TRIANGLES, 0, apple_nverts);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
    }

    // --- Sparks ----------------------------------------------------------------
    float buf[SPARK_N * 7];
    int nsp = 0;
    for (int i = 0; i < SPARK_N; i++) {
        if (sp_life[i] > 0) {
            sp_life[i] -= dt;
            sp_vy[i] += 700 * dt; // gravity
            sp_x[i] += sp_vx[i] * dt;
            sp_y[i] += sp_vy[i] * dt;
            if (sp_life[i] <= 0) continue;
            buf[nsp * 7 + 0] = sp_x[i];
            buf[nsp * 7 + 1] = sp_y[i];
            buf[nsp * 7 + 2] = sp_col[i].r;
            buf[nsp * 7 + 3] = sp_col[i].g;
            buf[nsp * 7 + 4] = sp_col[i].b;
            buf[nsp * 7 + 5] = sp_life[i] < 0.2f ? sp_life[i] * 5.0f : 1.0f;
            buf[nsp * 7 + 6] = 4.0f;
            nsp++;
        }
    }
    if (nsp) {
        glUseProgram(pt_prog);
        glUniformMatrix4fv(pt_u_mvp, 1, GL_FALSE, ortho);
        glBindBuffer(GL_ARRAY_BUFFER, sparks_vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nsp * 7 * sizeof(float)), buf, GL_STREAM_DRAW);
        GLsizei stride = 7 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void *)(2 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void *)(6 * sizeof(float)));
        glDrawArrays(GL_POINTS, 0, nsp);
        glDisableVertexAttribArray(2);
    }

    // --- zZz while dozing -------------------------------------------------------
    if (shown == V_DOZE) {
        float head_top = (float)PET_HEAD_TOP(app_h);
        static const char zc[3] = { 'z', 'Z', 'Z' };
        for (int i = 0; i < 3; i++) {
            float p = fmodf(t * 0.22f + i * 0.33f, 1.0f);
            float a = sinf(p * (float)M_PI) * 0.8f;
            char g[2] = { zc[i], 0 };
            font_draw(g, cx + 30.0f + i * 8.0f + p * 10.0f,
                      head_top - 6.0f - p * 34.0f,
                      10.0f + i * 4.0f, (Vec4){ 0.753f, 0.729f, 1.0f, a }, 1);
        }
    }
}
