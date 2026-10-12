#include "scene.h"

#include <SDL3/SDL.h>
#include "glutil.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "math.h"

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------

// Meshes (voxels, rings, disc). Lighting matches the original three.js
// setup: hemisphere + directional key + two point lights, then FogExp2.
// u_tint multiplies the baked vertex color (used for colored primitives).
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

// Points (stars, sparks). Size in world units, converted to pixels.
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

// Fullscreen radial-gradient backdrop (the CSS stage background).
static const char *BG_VS =
    "#version 150\n"
    "in vec2 a_pos;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = a_pos * 0.5 + 0.5;\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char *BG_FS =
    "#version 150\n"
    "in vec2 v_uv;\n"
    "uniform vec2 u_res;\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "    vec2 c = vec2(0.5, 0.6);\n"
    "    vec2 d = (v_uv - c) * vec2(u_res.x / u_res.y, 1.0);\n"
    "    float r = length(d) * 2.0;\n"
    "    vec3 a = vec3(0.169, 0.114, 0.333);\n"
    "    vec3 b = vec3(0.071, 0.047, 0.149);\n"
    "    vec3 e = vec3(0.031, 0.024, 0.059);\n"
    "    vec3 rgb = r < 0.75 ? mix(a, b, r / 0.75) : mix(b, e, (r - 0.75) / 1.25);\n"
    "    frag = vec4(rgb, 1.0);\n"
    "}\n";

// ---------------------------------------------------------------------------
// Pixel maps for Clawd's six variants (ported from boss.js).
// C body, E eyes/lids, M mouth, W tooth, S floating sparks.
// ---------------------------------------------------------------------------

#define MAPW 15
#define T_ "..CCCCCCCCCCC.."
#define EYES_ "..CCECCCCCECC.."
#define ARMS_EYES_ "CCCCECCCCCECCCC"
#define ARMS_ "CCCCCCCCCCCCCCC"
#define GRIN_ "CCCCCCMWMCCCCCC"
#define MOUTH_ "CCCCCCCMCCCCCCC"
#define LEGS_ "...C.C...C.C..."
#define SPARKS_WIDE_ ".S...........S."
#define SPARKS_AROUND_ "...S.......S..."
#define BLANK_ "..............."

static const char *MAPS[6][11] = {
    { T_, T_, EYES_, ARMS_EYES_, ARMS_, T_, T_, LEGS_, LEGS_, NULL, NULL },
    { T_, T_, T_, ARMS_EYES_, ARMS_, T_, T_, LEGS_, LEGS_, NULL, NULL },
    { T_, T_, T_, ARMS_EYES_, MOUTH_, T_, T_, LEGS_, LEGS_, NULL, NULL },
    { SPARKS_WIDE_, BLANK_, T_, T_, EYES_, ARMS_EYES_, ARMS_, T_, T_, LEGS_, LEGS_ },
    { T_, T_, EYES_, ARMS_EYES_, GRIN_, T_, T_, LEGS_, LEGS_, NULL, NULL },
    { SPARKS_AROUND_, BLANK_, T_, T_, EYES_, ARMS_EYES_, ARMS_, T_, T_, LEGS_, LEGS_ },
};
enum { V_IDLE, V_WORK, V_DOZE, V_WAKE, V_HAPPY, V_DIZZY };

static int map_rows(int v) {
    for (int r = 0; r < 11; r++)
        if (!MAPS[v][r]) return r;
    return 11;
}

typedef struct { float bobA, bobF, tilt, jumpF, spin; } Motion;
static const Motion MOTION[6] = {
    [V_IDLE] = { 0.06f, 1.6f, 0.02f, 0, 0 },
    [V_WORK] = { 0.04f, 6.0f, 0.04f, 0, 0 },
    [V_DOZE] = { 0.05f, 0.8f, 0.06f, 0, 0 },
    [V_WAKE] = { 0.03f, 2.0f, 0.04f, 3.0f, 0 },
    [V_HAPPY] = { 0.02f, 1.0f, 0.03f, 5.0f, 0 },
    [V_DIZZY] = { 0.04f, 2.0f, 0.08f, 0, 2.6f },
};

// ---------------------------------------------------------------------------
// GL state
// ---------------------------------------------------------------------------

static GLuint mesh_prog, pt_prog, bg_prog;
static GLint mesh_u_mvp, mesh_u_model, mesh_u_cam, mesh_u_lit, mesh_u_tint, mesh_u_fogd, mesh_u_fogc;
static GLint pt_u_mvp, pt_u_model, pt_u_psize, pt_u_col, pt_u_cam, pt_u_fogd, pt_u_fogc;
static GLint bg_u_res;
static GLuint quad_vbo;

static GLuint torus_vbo, torus_ibo, torus_nidx;
static GLuint disc_vbo;
static GLuint stars_vbo;
static GLuint sparks_vbo;

#define SPARK_N 160
static float sp_pos[SPARK_N][3], sp_vel[SPARK_N][3], sp_life[SPARK_N];
static Vec4 spark_col = { 1, 0.824f, 0.247f, 1 };
static bool spark_alive;

static struct { GLuint vbo, ibo; int nidx; } variant[6];

// Boss animation state (ported from the BOSS state machine).
static int shown = V_DOZE;
static float revert_at = -1;
static int revert_to = V_DOZE;
static float pop_t = 1, phase = 0, punch = 0, spin_acc = 0, hp_scale = 1;
static bool dead_flag;
static float shake = 0, root_y = 0;
static int vp_w = 1280, vp_h = 800;

static float rnd(void) { return (float)rand() / (float)RAND_MAX; }

// Build one Clawd variant: a box per non-empty pixel, extruded per pixel type.
static void build_variant(int v) {
    static const struct { char ch; int hex; int depth; } PAL[] = {
        { 'C', 0xde886d, 4 }, { 'E', 0x1a0e08, 6 }, { 'M', 0x2b120a, 6 },
        { 'W', 0xfff3e8, 6 }, { 'S', 0xffbd2e, 1 },
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
                    out[3] = F[f][k][0];
                    out[4] = F[f][k][1];
                    out[5] = F[f][k][2];
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

// Flat torus in the XZ plane, around the origin, white vertex color.
static void build_torus(float major, float minor, int seg, int ring) {
    float *verts = SDL_calloc((size_t)((seg + 1) * (ring + 1)) * 10, sizeof(float));
    unsigned *idx = SDL_calloc((size_t)(seg * ring * 6), sizeof(unsigned));
    int n = 0, m = 0;
    for (int i = 0; i <= seg; i++) {
        float u = (float)i / seg * 2.0f * (float)M_PI;
        for (int j = 0; j <= ring; j++) {
            float v = (float)j / ring * 2.0f * (float)M_PI;
            float cw = major + minor * cosf(v);
            verts[n++] = cw * cosf(u);
            verts[n++] = minor * sinf(v);
            verts[n++] = cw * sinf(u);
            verts[n++] = cosf(v) * cosf(u);
            verts[n++] = sinf(v);
            verts[n++] = cosf(v) * sinf(u);
            verts[n++] = 1;
            verts[n++] = 1;
            verts[n++] = 1;
            verts[n++] = 1;
        }
    }
    for (int i = 0; i < seg; i++)
        for (int j = 0; j < ring; j++) {
            int a = i * (ring + 1) + j, b = a + ring + 1;
            idx[m++] = (unsigned)a;
            idx[m++] = (unsigned)b;
            idx[m++] = (unsigned)(a + 1);
            idx[m++] = (unsigned)b;
            idx[m++] = (unsigned)(b + 1);
            idx[m++] = (unsigned)(a + 1);
        }
    glGenBuffers(1, &torus_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, torus_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof(float)), verts, GL_STATIC_DRAW);
    glGenBuffers(1, &torus_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, torus_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(m * sizeof(unsigned)), idx, GL_STATIC_DRAW);
    torus_nidx = m;
    SDL_free(verts);
    SDL_free(idx);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void scene_init(void) {
    mesh_prog = gl_program(MESH_VS, MESH_FS);
    pt_prog = gl_program(PT_VS, PT_FS);
    bg_prog = gl_program(BG_VS, BG_FS);
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
    bg_u_res = glGetUniformLocation(bg_prog, "u_res");

    for (int v = 0; v < 6; v++) build_variant(v);
    build_torus(2.6f, 0.04f, 80, 12);

    // Disc on the floor: center + 64 rim points, white color, tinted per draw.
    {
        float verts[66 * 10];
        int n = 0;
        verts[n++] = 0; verts[n++] = -2.1f; verts[n++] = 0;
        verts[n++] = 0; verts[n++] = 1; verts[n++] = 0;
        verts[n++] = 1; verts[n++] = 1; verts[n++] = 1; verts[n++] = 1;
        for (int i = 0; i <= 64; i++) {
            float a = (float)i / 64 * 2.0f * (float)M_PI;
            verts[n++] = cosf(a) * 2.6f;
            verts[n++] = -2.1f;
            verts[n++] = sinf(a) * 2.6f;
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

    // Starfield: 300 points far behind the arena.
    {
        float verts[300 * 4];
        int n = 0;
        for (int i = 0; i < 300; i++) {
            verts[n++] = (rnd() - 0.5f) * 40;
            verts[n++] = (rnd() - 0.5f) * 24;
            verts[n++] = -6 - rnd() * 14;
            verts[n++] = 0.07f;
        }
        glGenBuffers(1, &stars_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, stars_vbo);
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

    // Backdrop quad buffer.
    static const float quad[12] = { -1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1 };
    glGenBuffers(1, &quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
}

void scene_resize(int w, int h) {
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

void scene_boss_hit(bool big) {
    punch = 1;
    shake = big ? 1.6f : 0.9f;
    burst(big ? 100 : 55, big ? 0xff4d6d : 0xffd23f);
}

void scene_boss_win(void) {
    cameo(V_HAPPY, V_DIZZY, 2200, (float)SDL_GetTicks() / 1000.0f);
    burst(150, 0x3ddc97);
    shake = 1.8f;
}

void scene_set(int foe, float hp, bool dead, bool dormant, bool working) {
    (void)foe;
    hp_scale = 0.8f + 0.2f * hp;
    dead_flag = dead;
    if (revert_at >= 0) return; // a cameo (wake/happy) is playing out

    int steady = dead ? V_DIZZY : dormant ? V_DOZE : working ? V_WORK : V_IDLE;
    if (shown == V_DOZE && steady != V_DOZE)
        cameo(V_WAKE, steady, 1800, (float)SDL_GetTicks() / 1000.0f);
    else
        show(steady);
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

void scene_frame(float t, float dt) {
    float aspect = (float)vp_w / (float)(vp_h ? vp_h : 1);
    float cam_z = aspect < 1.1f ? 11.0f : 9.0f;

    // Camera with decaying shake.
    shake -= dt * 3;
    if (shake < 0) shake = 0;
    Vec3 cam = { (rnd() - 0.5f) * shake * 0.4f,
                 0.4f + (rnd() - 0.5f) * shake * 0.4f,
                 cam_z };
    Vec3 at = { 0, -0.1f, 0 };
    Vec3 up = { 0, 1, 0 };

    Mat4 proj, view, vp;
    m4_perspective(proj, 42.0f * (float)M_PI / 180.0f, aspect, 0.1f, 100.0f);
    m4_lookAt(view, cam, at, up);
    m4_mul(vp, proj, view);

    if (revert_at >= 0 && t >= revert_at) {
        revert_at = -1;
        show(revert_to);
    }

    // --- Backdrop gradient ---------------------------------------------------
    glDisable(GL_DEPTH_TEST);
    glUseProgram(bg_prog);
    glUniform2f(bg_u_res, (float)vp_w, (float)vp_h);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // --- Stars ---------------------------------------------------------------
    {
        glUseProgram(pt_prog);
        glUniformMatrix4fv(pt_u_mvp, 1, GL_FALSE, vp);
        Mat4 rot;
        m4_rotY(rot, t * 0.01f);
        glUniformMatrix4fv(pt_u_model, 1, GL_FALSE, rot);
        glUniform1f(pt_u_psize, (float)vp_h * 0.5f / tanf(21.0f * (float)M_PI / 180.0f));
        Vec4 col = hex4(0xcbb8ff, 0.8f);
        glUniform4f(pt_u_col, col.r, col.g, col.b, col.a);
        glUniform1f(pt_u_fogd, 0.045f);
        Vec3 fogc = hex3(0x120c26);
        glUniform3f(pt_u_fogc, fogc.x, fogc.y, fogc.z);
        glUniform3f(pt_u_cam, cam.x, cam.y, cam.z);
        glBindBuffer(GL_ARRAY_BUFFER, stars_vbo);
        GLsizei stride = 4 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
        glDrawArrays(GL_POINTS, 0, 300);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
    }

    // --- Rings and disc -------------------------------------------------------
    glUseProgram(mesh_prog);
    glUniformMatrix4fv(mesh_u_mvp, 1, GL_FALSE, vp);
    glUniform3f(mesh_u_cam, cam.x, cam.y, cam.z);
    glUniform1f(mesh_u_fogd, 0.045f);
    Vec3 fogc = hex3(0x120c26);
    glUniform3f(mesh_u_fogc, fogc.x, fogc.y, fogc.z);
    glUniform1f(mesh_u_lit, 0);
    glBindBuffer(GL_ARRAY_BUFFER, torus_vbo);
    bind_mesh_attribs();
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, torus_ibo);

    // Inner purple ring. (The original spun it about its own axis, which is
    // invisible on a symmetric torus — skipped.)
    Mat4 model;
    m4_translate(model, 0, -2.1f, 0);
    glUniformMatrix4fv(mesh_u_model, 1, GL_FALSE, model);
    Vec4 t1 = hex4(0x8b5cf6, 1);
    glUniform4f(mesh_u_tint, t1.r, t1.g, t1.b, t1.a);
    glDrawElements(GL_TRIANGLES, torus_nidx, GL_UNSIGNED_INT, NULL);

    // Outer pink ring: 1.35x scale, pulsing opacity.
    Mat4 sc;
    m4_scale(sc, 1.35f, 1.35f, 1.35f);
    m4_mul(model, model, sc);
    glUniformMatrix4fv(mesh_u_model, 1, GL_FALSE, model);
    Vec4 t2 = hex4(0xec4899, 0.35f + 0.15f * sinf(t * 2.0f));
    glUniform4f(mesh_u_tint, t2.r, t2.g, t2.b, t2.a);
    glDrawElements(GL_TRIANGLES, torus_nidx, GL_UNSIGNED_INT, NULL);

    // Floor disc.
    glBindBuffer(GL_ARRAY_BUFFER, disc_vbo);
    bind_mesh_attribs();
    m4_identity(model);
    glUniformMatrix4fv(mesh_u_model, 1, GL_FALSE, model);
    Vec4 t3 = hex4(0x8b5cf6, 0.12f);
    glUniform4f(mesh_u_tint, t3.r, t3.g, t3.b, t3.a);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 66);

    // --- Clawd ---------------------------------------------------------------
    {
        const Motion M = MOTION[shown];
        phase += dt;
        pop_t += dt * 4;
        if (pop_t > 1) pop_t = 1;
        float pop = 1 - powf(1 - pop_t, 3);
        punch -= dt * 3;
        if (punch < 0) punch = 0;
        float sy = pop * (1 - punch * 0.15f) * hp_scale;
        float sxz = pop * (1 + punch * 0.12f) * hp_scale;

        float y = sinf(phase * M.bobF) * M.bobA;
        if (M.jumpF) y += fabsf(sinf(phase * M.jumpF)) * 0.35f;
        if (dead_flag) y -= 0.3f;
        float k = dt * 6 < 1 ? dt * 6 : 1;
        root_y += (y - root_y) * k;

        spin_acc += M.spin * dt;
        float rotz = sinf(phase * 1.1f) * M.tilt + punch * 0.08f * sinf(phase * 40.0f);
        float roty = spin_acc + sinf(phase * 0.5f) * 0.1f;

        Mat4 t_, rz, ry, s_;
        m4_translate(t_, 0, root_y, 0);
        m4_rotZ(rz, rotz);
        m4_rotY(ry, roty);
        m4_scale(s_, sxz, sy, sxz);
        m4_mul(model, t_, rz);
        m4_mul(model, model, ry);
        m4_mul(model, model, s_);

        glUniformMatrix4fv(mesh_u_model, 1, GL_FALSE, model);
        glUniform1f(mesh_u_lit, 1);
        glUniform4f(mesh_u_tint, 1, 1, 1, 1);
        glBindBuffer(GL_ARRAY_BUFFER, variant[shown].vbo);
        bind_mesh_attribs();
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, variant[shown].ibo);
        glDrawElements(GL_TRIANGLES, variant[shown].nidx, GL_UNSIGNED_INT, NULL);
    }

    // --- Sparks ---------------------------------------------------------------
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
        glUniform1f(pt_u_psize, (float)vp_h * 0.5f / tanf(21.0f * (float)M_PI / 180.0f));
        glUniform4f(pt_u_col, spark_col.r, spark_col.g, spark_col.b, 1.0f);
        glUniform1f(pt_u_fogd, 0.045f);
        glUniform3f(pt_u_fogc, fogc.x, fogc.y, fogc.z);
        glUniform3f(pt_u_cam, cam.x, cam.y, cam.z);
        glBindBuffer(GL_ARRAY_BUFFER, sparks_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof buf, buf, GL_STREAM_DRAW);
        GLsizei stride = 4 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
        glBlendFunc(GL_SRC_ALPHA, GL_ONE); // additive
        glDepthMask(GL_FALSE);
        glDrawArrays(GL_POINTS, 0, SPARK_N);
        glDepthMask(GL_TRUE);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
}
