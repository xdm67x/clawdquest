// ---------------------------------------------------------------------------
// Clawd the desktop pet — renderer.
//
// The window is transparent, so there is no arena any more: just Clawd, a
// soft shadow disc, hit sparks and zZz while he dozes. The camera frames a
// constant pixel height for the character regardless of window size, so he
// does not jump when the quest card opens above him.
// ---------------------------------------------------------------------------

#include "pet.h"

#include <SDL3/SDL.h>
#include "glutil.h"
#include <math.h>
#include <stdlib.h>

#include "app.h"
#include "font.h"
#include "math.h"

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------

// Voxels. Lighting matches the original three.js setup: hemisphere +
// directional key + two point lights; the fog term is kept but unused
// (density 0). u_tint multiplies the baked vertex color.
static const char *MESH_VS =
    "#version 150\n"
    "in vec3 a_pos;\n"
    "in vec3 a_norm;\n"
    "in vec4 a_col;\n"
    "uniform mat4 u_mvp;\n"
    "uniform mat4 u_model;\n"
    "out vec3 v_pos;\n"
    "out vec3 v_norm;\n"
    "out vec4 v_col;\n"
    "void main() {\n"
    "    vec4 w = u_model * vec4(a_pos, 1.0);\n"
    "    v_pos = w.xyz;\n"
    "    v_norm = mat3(u_model) * a_norm;\n"
    "    v_col = a_col;\n"
    "    gl_Position = u_mvp * w;\n"
    "}\n";

static const char *MESH_FS =
    "#version 150\n"
    "in vec3 v_pos;\n"
    "in vec3 v_norm;\n"
    "in vec4 v_col;\n"
    "uniform vec3 u_cam;\n"
    "uniform float u_lit;\n"
    "uniform vec4 u_tint;\n"
    "uniform float u_fogd;\n"
    "uniform vec3 u_fogc;\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "    vec4 base = v_col * u_tint;\n"
    "    vec3 rgb = base.rgb;\n"
    "    if (u_lit > 0.5) {\n"
    "        vec3 n = normalize(v_norm);\n"
    "        float hb = n.y * 0.5 + 0.5;\n"
    "        vec3 hemi = mix(vec3(0.102, 0.059, 0.200), vec3(0.725, 0.643, 1.0), hb) * 0.9;\n"
    "        vec3 key = vec3(max(dot(n, normalize(vec3(4.0, 6.0, 6.0))), 0.0) * 1.3);\n"
    "        vec3 lp1 = vec3(-6.0, 2.0, -3.0) - v_pos;\n"
    "        vec3 lp2 = vec3(6.0, -1.0, -2.0) - v_pos;\n"
    "        float d1 = length(lp1);\n"
    "        float d2 = length(lp2);\n"
    "        vec3 l1 = vec3(1.0, 0.302, 0.616)\n"
    "                * (2.2 * clamp(1.0 - d1 / 30.0, 0.0, 1.0)) * max(dot(n, lp1 / d1), 0.0);\n"
    "        vec3 l2 = vec3(0.302, 0.651, 1.0)\n"
    "                * (1.8 * clamp(1.0 - d2 / 30.0, 0.0, 1.0)) * max(dot(n, lp2 / d2), 0.0);\n"
    "        rgb = base.rgb * (hemi + key + l1 + l2);\n"
    "    }\n"
    "    float depth = length(u_cam - v_pos);\n"
    "    float f = 1.0 - exp(-pow(u_fogd * depth, 2.0));\n"
    "    frag = vec4(mix(rgb, u_fogc, f), base.a);\n"
    "}\n";

// Points (sparks). Size in world units, converted to pixels.
static const char *PT_VS =
    "#version 150\n"
    "in vec3 a_pos;\n"
    "in float a_size;\n"
    "uniform mat4 u_mvp;\n"
    "uniform mat4 u_model;\n"
    "uniform float u_psize;\n"
    "out vec3 v_pos;\n"
    "void main() {\n"
    "    vec4 w = u_model * vec4(a_pos, 1.0);\n"
    "    v_pos = w.xyz;\n"
    "    gl_Position = u_mvp * w;\n"
    "    float sz = u_psize * a_size / max(gl_Position.w, 0.001);\n"
    "    gl_PointSize = clamp(sz, 1.0, 64.0);\n"
    "}\n";

static const char *PT_FS =
    "#version 150\n"
    "in vec3 v_pos;\n"
    "uniform vec4 u_col;\n"
    "uniform vec3 u_cam;\n"
    "uniform float u_fogd;\n"
    "uniform vec3 u_fogc;\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "    float depth = length(u_cam - v_pos);\n"
    "    float f = 1.0 - exp(-pow(u_fogd * depth, 2.0));\n"
    "    frag = vec4(mix(u_col.rgb, u_fogc, f), u_col.a);\n"
    "}\n";

// ---------------------------------------------------------------------------
// Pixel maps for Clawd's variants (ported from the original boss.js, plus a
// closed-eye blink frame). C body, E eyes, L closed lids, M mouth, W tooth,
// S floating sparks.
// ---------------------------------------------------------------------------

#define MAPW 15
#define T_ "..CCCCCCCCCCC.."
#define EYES_ "..CCECCCCCECC.."
#define ARMS_EYES_ "CCCCECCCCCECCCC"
#define ARMS_ "CCCCCCCCCCCCCCC"
#define LIDS_ "CCCCLCCCCCLCCCC"
#define GRIN_ "CCCCCCMWMCCCCCC"
#define MOUTH_ "CCCCCCCMCCCCCCC"
#define LEGS_ "...C.C...C.C..."
#define SPARKS_AROUND_ "...S.......S..."
#define BLANK_ "..............."

static const char *MAPS[7][11] = {
    { T_, T_, EYES_, ARMS_EYES_, ARMS_, T_, T_, LEGS_, LEGS_, NULL, NULL },
    { T_, T_, T_, ARMS_EYES_, ARMS_, T_, T_, LEGS_, LEGS_, NULL, NULL },
    { T_, T_, T_, ARMS_EYES_, MOUTH_, T_, T_, LEGS_, LEGS_, NULL, NULL },
    { SPARKS_AROUND_, BLANK_, T_, T_, EYES_, ARMS_EYES_, ARMS_, T_, T_, LEGS_, LEGS_ },
    { T_, T_, EYES_, ARMS_EYES_, GRIN_, T_, T_, LEGS_, LEGS_, NULL, NULL },
    { T_, T_, EYES_, LIDS_, ARMS_, T_, T_, LEGS_, LEGS_, NULL, NULL },
    { SPARKS_AROUND_, BLANK_, T_, T_, EYES_, ARMS_EYES_, ARMS_, T_, T_, LEGS_, LEGS_ },
};
enum { V_IDLE, V_WORK, V_DOZE, V_WAKE, V_HAPPY, V_BLINK, V_DIZZY };

static int map_rows(int v) {
    for (int r = 0; r < 11; r++)
        if (!MAPS[v][r]) return r;
    return 11;
}

typedef struct { float bobA, bobF, tilt, jumpF, spin; } Motion;
static const Motion MOTION[7] = {
    [V_IDLE] = { 0.06f, 1.6f, 0.02f, 0, 0 },
    [V_WORK] = { 0.04f, 6.0f, 0.04f, 0, 0 },
    [V_DOZE] = { 0.05f, 0.8f, 0.06f, 0, 0 },
    [V_WAKE] = { 0.03f, 2.0f, 0.04f, 3.0f, 0 },
    [V_HAPPY] = { 0.02f, 1.0f, 0.03f, 5.0f, 0 },
    [V_BLINK] = { 0.06f, 1.6f, 0.02f, 0, 0 },
    [V_DIZZY] = { 0.04f, 2.0f, 0.08f, 0, 2.6f },
};

// ---------------------------------------------------------------------------
// GL state
// ---------------------------------------------------------------------------

static GLuint mesh_prog, pt_prog;
static GLint mesh_u_mvp, mesh_u_model, mesh_u_cam, mesh_u_lit, mesh_u_tint, mesh_u_fogd, mesh_u_fogc;
static GLint pt_u_mvp, pt_u_model, pt_u_psize, pt_u_col, pt_u_cam, pt_u_fogd, pt_u_fogc;
static GLuint disc_vbo;
static GLuint sparks_vbo;

#define SPARK_N 160
static float sp_pos[SPARK_N][3], sp_vel[SPARK_N][3], sp_life[SPARK_N];
static Vec4 spark_col = { 1, 0.824f, 0.247f, 1 };
static bool spark_alive;

static struct { GLuint vbo, ibo; int nidx; } variant[7];

// Pet animation state.
static int shown = V_IDLE;
static float revert_at = -1;
static int revert_to = V_IDLE;
static float pop_t = 1, phase = 0, punch = 0, spin_acc = 0;
static bool dead_flag, dormant_flag, working_flag, drag_flag;
static float idle_secs;            // seconds since the last interaction
static float blink_t = 3, blink_on = 0;
static int vp_w = PET_W, vp_h = PET_H;

static float rnd(void) { return (float)rand() / (float)RAND_MAX; }

// Clawd's body in world units (voxel size 0.46, 9 rows, base at -2.05).
#define FEET_Y (-2.28f)
#define HEAD_TOP_Y 1.86f
#define SHADOW_Y (-2.32f)

// Build one Clawd variant: a box per non-empty pixel, extruded per pixel type.
static void build_variant(int v) {
    static const struct { char ch; int hex; int depth; } PAL[] = {
        { 'C', 0xde886d, 4 }, { 'E', 0x1a0e08, 6 }, { 'L', 0x1a0e08, 6 },
        { 'M', 0x2b120a, 6 }, { 'W', 0xfff3e8, 6 }, { 'S', 0xffbd2e, 1 },
    };
    const float V = 0.46f, BASE_Y = -2.05f;
    int rows = map_rows(v);

    int maxvox = MAPW * 11;
    float *verts = SDL_calloc((size_t)maxvox * 24 * 10, sizeof(float));
    unsigned *idx = SDL_calloc((size_t)maxvox * 36, sizeof(unsigned));
    int nv = 0, ni = 0;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < MAPW && MAPS[v][r][c]; c++) {
            char ch = MAPS[v][r][c];
            if (ch == '.') continue;
            Vec3 col = { 1, 0, 1 };
            int depth = 1;
            for (unsigned p = 0; p < sizeof PAL / sizeof PAL[0]; p++)
                if (PAL[p].ch == ch) { col = hex3(PAL[p].hex); depth = PAL[p].depth; break; }
            float cx = (c - 7) * V;
            float cy = (rows - 1 - r) * V + BASE_Y;
            float hx = V * 0.5f, hy = V * 0.5f, hz = depth * V * 0.5f;
            // 6 faces: normal, then 4 corners (CCW from outside).
            static const float F[6][5][3] = {
                { { 0, 0, 1 },  { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } },
                { { 0, 0, -1 }, { 1, -1, -1 }, { -1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 } },
                { { 1, 0, 0 },  { 1, -1, 1 }, { 1, -1, -1 }, { 1, 1, -1 }, { 1, 1, 1 } },
                { { -1, 0, 0 }, { -1, -1, -1 }, { -1, -1, 1 }, { -1, 1, 1 }, { -1, 1, -1 } },
                { { 0, 1, 0 },  { -1, 1, 1 }, { 1, 1, 1 }, { 1, 1, -1 }, { -1, 1, -1 } },
                { { 0, -1, 0 }, { -1, -1, -1 }, { 1, -1, -1 }, { 1, -1, 1 }, { -1, -1, 1 } },
            };
            for (int f = 0; f < 6; f++) {
                unsigned base = (unsigned)nv;
                for (int k = 0; k < 4; k++) {
                    float *out = verts + (nv * 10);
                    out[0] = cx + F[f][k + 1][0] * hx;
                    out[1] = cy + F[f][k + 1][1] * hy;
                    out[2] = F[f][k + 1][2] * hz;
                    out[3] = F[f][0][0];
                    out[4] = F[f][0][1];
                    out[5] = F[f][0][2];
                    out[6] = col.x;
                    out[7] = col.y;
                    out[8] = col.z;
                    out[9] = 1.0f;
                    nv++;
                }
                idx[ni++] = base + 0;
                idx[ni++] = base + 1;
                idx[ni++] = base + 2;
                idx[ni++] = base + 0;
                idx[ni++] = base + 2;
                idx[ni++] = base + 3;
            }
        }
    }
    glGenBuffers(1, &variant[v].vbo);
    glBindBuffer(GL_ARRAY_BUFFER, variant[v].vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nv * 10 * sizeof(float)), verts, GL_STATIC_DRAW);
    glGenBuffers(1, &variant[v].ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, variant[v].ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(ni * sizeof(unsigned)), idx, GL_STATIC_DRAW);
    variant[v].nidx = ni;
    SDL_free(verts);
    SDL_free(idx);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void pet_init(void) {
    static const char *mesh_attribs[] = { "a_pos", "a_norm", "a_col", NULL };
    static const char *pt_attribs[] = { "a_pos", "a_size", NULL };
    mesh_prog = gl_program_at(MESH_VS, MESH_FS, mesh_attribs);
    pt_prog = gl_program_at(PT_VS, PT_FS, pt_attribs);
    mesh_u_mvp = glGetUniformLocation(mesh_prog, "u_mvp");
    mesh_u_model = glGetUniformLocation(mesh_prog, "u_model");
    mesh_u_cam = glGetUniformLocation(mesh_prog, "u_cam");
    mesh_u_lit = glGetUniformLocation(mesh_prog, "u_lit");
    mesh_u_tint = glGetUniformLocation(mesh_prog, "u_tint");
    mesh_u_fogd = glGetUniformLocation(mesh_prog, "u_fogd");
    mesh_u_fogc = glGetUniformLocation(mesh_prog, "u_fogc");
    pt_u_mvp = glGetUniformLocation(pt_prog, "u_mvp");
    pt_u_model = glGetUniformLocation(pt_prog, "u_model");
    pt_u_psize = glGetUniformLocation(pt_prog, "u_psize");
    pt_u_col = glGetUniformLocation(pt_prog, "u_col");
    pt_u_cam = glGetUniformLocation(pt_prog, "u_cam");
    pt_u_fogd = glGetUniformLocation(pt_prog, "u_fogd");
    pt_u_fogc = glGetUniformLocation(pt_prog, "u_fogc");

    for (int v = 0; v < 7; v++) build_variant(v);

    // Soft shadow disc on the floor: center + 64 rim points, white, tinted.
    {
        float verts[66 * 10];
        int n = 0;
        verts[n++] = 0; verts[n++] = SHADOW_Y; verts[n++] = 0;
        verts[n++] = 0; verts[n++] = 1; verts[n++] = 0;
        verts[n++] = 1; verts[n++] = 1; verts[n++] = 1; verts[n++] = 1;
        for (int i = 0; i <= 64; i++) {
            float a = (float)i / 64 * 2.0f * (float)M_PI;
            verts[n++] = cosf(a) * 2.3f;
            verts[n++] = SHADOW_Y;
            verts[n++] = sinf(a) * 2.3f;
            verts[n++] = 0;
            verts[n++] = 1;
            verts[n++] = 0;
            verts[n++] = 1;
            verts[n++] = 1;
            verts[n++] = 1;
            verts[n++] = 1;
        }
        glGenBuffers(1, &disc_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, disc_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof verts, verts, GL_STATIC_DRAW);
    }

    for (int i = 0; i < SPARK_N; i++) sp_pos[i][1] = -99;
    float sparks[SPARK_N * 4];
    for (int i = 0; i < SPARK_N; i++) {
        sparks[i * 4 + 0] = sp_pos[i][0];
        sparks[i * 4 + 1] = sp_pos[i][1];
        sparks[i * 4 + 2] = sp_pos[i][2];
        sparks[i * 4 + 3] = 0.16f;
    }
    glGenBuffers(1, &sparks_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, sparks_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof sparks, sparks, GL_STREAM_DRAW);
}

void pet_resize(int w, int h) {
    vp_w = w;
    vp_h = h;
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
    Vec3 c = hex3(hex);
    spark_col.r = c.x; spark_col.g = c.y; spark_col.b = c.z; spark_col.a = 1;
    for (int i = 0, cnt = 0; i < SPARK_N && cnt < n; i++) {
        if (sp_life[i] <= 0) {
            cnt++;
            sp_life[i] = 1;
            sp_pos[i][0] = 0;
            sp_pos[i][1] = 0;
            sp_pos[i][2] = 1;
            float a = rnd() * 6.28f;
            float b = rnd() * 3.14f - 1.2f;
            float s = 2 + rnd() * 5;
            sp_vel[i][0] = cosf(a) * s * cosf(b);
            sp_vel[i][1] = sinf(b) * s + 2;
            sp_vel[i][2] = sinf(a) * s * cosf(b);
        }
    }
}

void pet_hit(bool big) {
    punch = 1;
    burst(big ? 100 : 55, big ? 0xff4d6d : 0xffd23f);
}

void pet_win(void) {
    cameo(V_HAPPY, V_DIZZY, 2200, (float)SDL_GetTicks() / 1000.0f);
    burst(150, 0x3ddc97);
}

static int steady_variant(void) {
    return dead_flag ? V_DIZZY : working_flag ? V_WORK
         : (dormant_flag || idle_secs > 300.0f) ? V_DOZE : V_IDLE;
}

void pet_set(bool dead, bool dormant, bool working) {
    dead_flag = dead;
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

static void bind_mesh_attribs(void) {
    GLsizei stride = 10 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void *)(6 * sizeof(float)));
}

void pet_frame(float t, float dt) {
    float H = (float)(vp_h ? vp_h : 1);
    float W = (float)vp_w;
    float aspect = W / H;

    // Frame a constant pixel height for Clawd regardless of window size.
    // The voxels have depth (front faces at z=+0.92, toward the camera), so
    // the perspective magnifies the subject: aim through the front faces.
    float f = (H * 0.5f) / tanf(21.0f * (float)M_PI / 180.0f);
    const float FRONT_Z = 0.92f;
    float dist = FRONT_Z + (HEAD_TOP_Y - FEET_Y) * f / (float)PET_PX;
    float aim_y = FEET_Y + ((H * 0.5f - (float)PET_FEET_MARGIN) / f) * (dist - FRONT_Z) - 0.45f;

    // A slightly raised camera, like the old arena view (top faces read 3D).
    Vec3 cam = { 0, aim_y + 0.45f, dist };
    Vec3 at = { 0, aim_y - 0.12f, 0 };
    Vec3 up = { 0, 1, 0 };

    Mat4 proj, view, vp;
    m4_perspective(proj, 42.0f * (float)M_PI / 180.0f, aspect, 0.1f, 100.0f);
    m4_lookAt(view, cam, at, up);
    m4_mul(vp, proj, view);

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

    // Blinking: a brief closed-eye frame while awake.
    if (shown == V_IDLE || shown == V_WORK) {
        if (blink_on > 0) {
            blink_on -= dt;
        } else {
            blink_t -= dt;
            if (blink_t <= 0) {
                blink_on = 0.13f;
                blink_t = 2.5f + rnd() * 4.0f;
            }
        }
    }
    int disp = (blink_on > 0 && (shown == V_IDLE || shown == V_WORK)) ? V_BLINK : shown;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    // Source-over: alpha must accumulate as coverage (a out = a + dst.a*(1-a)),
    // or the shadow disc would end up nearly invisible on the desktop.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(mesh_prog);
    glUniformMatrix4fv(mesh_u_mvp, 1, GL_FALSE, vp);
    glUniform3f(mesh_u_cam, cam.x, cam.y, cam.z);
    glUniform1f(mesh_u_fogd, 0.0f);
    Vec3 fogc = { 0, 0, 0 };
    glUniform3f(mesh_u_fogc, fogc.x, fogc.y, fogc.z);
    glUniform1f(mesh_u_lit, 0);

    // --- Shadow disc ----------------------------------------------------------
    {
        Mat4 model;
        m4_identity(model);
        glUniformMatrix4fv(mesh_u_model, 1, GL_FALSE, model);
        float a = 0.20f + 0.05f * sinf(phase * MOTION[V_IDLE].bobF);
        glUniform4f(mesh_u_tint, 0, 0, 0, a);
        glBindBuffer(GL_ARRAY_BUFFER, disc_vbo);
        bind_mesh_attribs();
        glDrawArrays(GL_TRIANGLE_FAN, 0, 66);
    }

    // --- Clawd ----------------------------------------------------------------
    {
        const Motion M = MOTION[disp];
        phase += dt;
        pop_t += dt * 4;
        if (pop_t > 1) pop_t = 1;
        float pop = 1 - powf(1 - pop_t, 3);
        punch -= dt * 3;
        if (punch < 0) punch = 0;
        float sy = pop * (1 - punch * 0.15f);
        float sxz = pop * (1 + punch * 0.12f);

        float y = sinf(phase * M.bobF) * M.bobA;
        if (M.jumpF) y += fabsf(sinf(phase * M.jumpF)) * 0.35f;
        if (disp == V_DIZZY && dead_flag) y -= 0.3f;

        spin_acc += M.spin * dt;
        float rotz = sinf(phase * 1.1f) * M.tilt + punch * 0.08f * sinf(phase * 40.0f);
        float roty = spin_acc + sinf(phase * 0.5f) * 0.1f;

        Mat4 t_, rz, ry, s_, model;
        m4_translate(t_, 0, y, 0);
        m4_rotZ(rz, rotz);
        m4_rotY(ry, roty);
        m4_scale(s_, sxz, sy, sxz);
        m4_mul(model, t_, rz);
        m4_mul(model, model, ry);
        m4_mul(model, model, s_);

        glUniformMatrix4fv(mesh_u_model, 1, GL_FALSE, model);
        glUniform1f(mesh_u_lit, 1);
        glUniform4f(mesh_u_tint, 1, 1, 1, 1);
        glBindBuffer(GL_ARRAY_BUFFER, variant[disp].vbo);
        bind_mesh_attribs();
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, variant[disp].ibo);
        glDrawElements(GL_TRIANGLES, variant[disp].nidx, GL_UNSIGNED_INT, NULL);
    }

    // --- Sparks -----------------------------------------------------------------
    spark_alive = false;
    float buf[SPARK_N * 4];
    for (int i = 0; i < SPARK_N; i++) {
        if (sp_life[i] > 0) {
            spark_alive = true;
            sp_life[i] -= dt * 0.9f;
            sp_vel[i][1] -= 9 * dt;
            for (int k = 0; k < 3; k++) sp_pos[i][k] += sp_vel[i][k] * dt;
            if (sp_life[i] <= 0) sp_pos[i][1] = -99; // park the dead particle
        }
        buf[i * 4 + 0] = sp_pos[i][0];
        buf[i * 4 + 1] = sp_pos[i][1];
        buf[i * 4 + 2] = sp_pos[i][2];
        buf[i * 4 + 3] = 0.16f;
    }
    if (spark_alive) {
        glUseProgram(pt_prog);
        glUniformMatrix4fv(pt_u_mvp, 1, GL_FALSE, vp);
        Mat4 id;
        m4_identity(id);
        glUniformMatrix4fv(pt_u_model, 1, GL_FALSE, id);
        glUniform1f(pt_u_psize, H * 0.5f / tanf(21.0f * (float)M_PI / 180.0f));
        glUniform4f(pt_u_col, spark_col.r, spark_col.g, spark_col.b, 1.0f);
        glUniform1f(pt_u_fogd, 0.0f);
        glUniform3f(pt_u_fogc, 0, 0, 0);
        glUniform3f(pt_u_cam, cam.x, cam.y, cam.z);
        glBindBuffer(GL_ARRAY_BUFFER, sparks_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof buf, buf, GL_STREAM_DRAW);
        GLsizei stride = 4 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE); // additive
        glDepthMask(GL_FALSE);
        glDrawArrays(GL_POINTS, 0, SPARK_N);
        glDepthMask(GL_TRUE);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                            GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }

    glDisable(GL_DEPTH_TEST);

    // --- zZz while dozing -------------------------------------------------------
    if (shown == V_DOZE) {
        float head_top = (float)PET_HEAD_TOP(vp_h);
        static const char zc[3] = { 'z', 'Z', 'Z' };
        for (int i = 0; i < 3; i++) {
            float p = fmodf(t * 0.22f + i * 0.33f, 1.0f);
            float a = sinf(p * (float)M_PI) * 0.8f;
            char g[2] = { zc[i], 0 };
            font_draw(g, W * 0.5f + 40.0f + i * 9.0f + p * 12.0f,
                      head_top - 6.0f - p * 34.0f,
                      10.0f + i * 4.0f, (Vec4){ 0.753f, 0.729f, 1.0f, a }, 1);
        }
    }
}
