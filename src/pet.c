// ---------------------------------------------------------------------------
// Clawd the desktop pet — 2D renderer.
//
// The window is transparent, so there is no arena: just the flat pixel
// sprite, a soft shadow, sparks, floating zZz while dozing, and the apple he
// eats when a quest completes. One quad per pixel of the variant maps; the
// model transform (translate/rotate/scale, in cell units) is shared by the
// sprite, the shadow and the apple.
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
// Shaders: flat colored 2D quads, and 2D points (sparks).
// ---------------------------------------------------------------------------

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
// Pixel maps for Clawd's variants. C body, E eyes, L closed lids, M mouth,
// W tooth, S floating sparks.
// ---------------------------------------------------------------------------

#define MAPW 15
#define T_ "..CCCCCCCCCCC.."
#define EYES_ "..CCECCCCCECC.."
#define ARMS_EYES_ "CCCCECCCCCECCCC"
#define ARMS_ "CCCCCCCCCCCCCCC"
#define LIDS_ "CCCCLCCCCCLCCCC"
#define GRIN_ "CCCCCCMWMCCCCCC"
#define MOUTH_ "CCCCCCCMCCCCCCC"
#define OMOUTH_ "CCCCCMMMMMCCCCC"
#define LEGS_ "...C.C...C.C..."
#define SPARKS_AROUND_ "...S.......S..."
#define BLANK_ "..............."

static const char *MAPS[9][11] = {
    { T_, T_, EYES_, ARMS_EYES_, ARMS_, T_, T_, LEGS_, LEGS_, NULL, NULL },
    { T_, T_, T_, ARMS_EYES_, ARMS_, T_, T_, LEGS_, LEGS_, NULL, NULL },
    { T_, T_, T_, ARMS_EYES_, MOUTH_, T_, T_, LEGS_, LEGS_, NULL, NULL },
    { SPARKS_AROUND_, BLANK_, T_, T_, EYES_, ARMS_EYES_, ARMS_, T_, T_, LEGS_, LEGS_ },
    { T_, T_, EYES_, ARMS_EYES_, GRIN_, T_, T_, LEGS_, LEGS_, NULL, NULL },
    { T_, T_, T_, LIDS_, ARMS_, T_, T_, LEGS_, LEGS_, NULL, NULL },
    { T_, T_, T_, LIDS_, GRIN_, T_, T_, LEGS_, LEGS_, NULL, NULL },
    { T_, T_, T_, LIDS_, OMOUTH_, T_, T_, LEGS_, LEGS_, NULL, NULL },
    { SPARKS_AROUND_, BLANK_, T_, T_, EYES_, ARMS_EYES_, ARMS_, T_, T_, LEGS_, LEGS_ },
};
enum { V_IDLE, V_WORK, V_DOZE, V_WAKE, V_HAPPY, V_BLINK, V_FED, V_EAT, V_DIZZY };

static int map_rows(int v) {
    for (int r = 0; r < 11; r++)
        if (!MAPS[v][r]) return r;
    return 11;
}

typedef struct { float bobA, bobF, tilt, jumpF, spin; } Motion;
static const Motion MOTION[9] = {
    [V_IDLE] = { 0.06f, 1.6f, 0.02f, 0, 0 },
    [V_WORK] = { 0.04f, 6.0f, 0.04f, 0, 0 },
    [V_DOZE] = { 0.05f, 0.8f, 0.06f, 0, 0 },
    [V_WAKE] = { 0.03f, 2.0f, 0.04f, 3.0f, 0 },
    [V_HAPPY] = { 0.02f, 1.0f, 0.03f, 5.0f, 0 },
    [V_BLINK] = { 0.06f, 1.6f, 0.02f, 0, 0 },
    [V_FED] = { 0.05f, 1.2f, 0.04f, 0, 0 },
    [V_EAT] = { 0.03f, 8.0f, 0.03f, 0, 0 },
    [V_DIZZY] = { 0.04f, 2.0f, 0.08f, 0, 2.6f },
};

// The apple: S stem, G leaf, R red, H highlight (5x5).
#define APPLE_ROWS 5
static const char *APPLE[APPLE_ROWS] = {
    "..S..",
    ".GS..",
    ".HRR.",
    "RRRRR",
    ".RRR.",
};

// ---------------------------------------------------------------------------
// GL state
// ---------------------------------------------------------------------------

// One quad mesh per pixel map (variants, apple), and its palette.
typedef struct { GLuint vbo, ibo; int nidx; } MapMesh;
typedef struct { char ch; int hex; } PixPal;

static GLuint flat_prog, pt_prog;
static GLint flat_u_mvp, flat_u_model, pt_u_mvp;
static GLuint sparks_vbo;
static GLuint shadow_vbo;

static MapMesh variant[9];
static MapMesh apple;

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

// Build a 2D quad mesh from a pixel map: one quad (2 tris) per non-empty
// pixel, positions in cell units (x: column, y: row from the top).
static void build_map(const char *map[], int rows, int mapw,
                      const PixPal pal[], int npal,
                      MapMesh *out) {
    float *verts = SDL_calloc((size_t)(mapw * rows) * 4 * 6, sizeof(float));
    unsigned *idx = SDL_calloc((size_t)(mapw * rows) * 6, sizeof(unsigned));
    int nv = 0, ni = 0;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < mapw && map[r][c]; c++) {
            char ch = map[r][c];
            if (ch == '.') continue;
            Vec3 col = { 1, 0, 1 };
            for (int p = 0; p < npal; p++)
                if (pal[p].ch == ch) { col = hex3(pal[p].hex); break; }
            const float q[4][6] = {
                { (float)c, (float)r, col.x, col.y, col.z, 1 },
                { (float)c + 1, (float)r, col.x, col.y, col.z, 1 },
                { (float)c + 1, (float)r + 1, col.x, col.y, col.z, 1 },
                { (float)c, (float)r + 1, col.x, col.y, col.z, 1 },
            };
            unsigned base = (unsigned)nv;
            for (int k = 0; k < 4; k++) {
                float *o = verts + nv * 6;
                for (int f = 0; f < 6; f++) o[f] = q[k][f];
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
    glGenBuffers(1, &out->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, out->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nv * 6 * sizeof(float)), verts, GL_STATIC_DRAW);
    glGenBuffers(1, &out->ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(ni * sizeof(unsigned)), idx, GL_STATIC_DRAW);
    out->nidx = ni;
    SDL_free(verts);
    SDL_free(idx);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void pet_init(void) {
    static const char *flat_attribs[] = { "a_pos", "a_col", NULL };
    static const char *pt_attribs[] = { "a_pos", "a_col", "a_size", NULL };
    flat_prog = gl_program_at(FLAT_VS, FLAT_FS, flat_attribs);
    flat_u_mvp = glGetUniformLocation(flat_prog, "u_mvp");
    flat_u_model = glGetUniformLocation(flat_prog, "u_model");
    pt_prog = gl_program_at(PT_VS, PT_FS, pt_attribs);
    pt_u_mvp = glGetUniformLocation(pt_prog, "u_mvp");

    static const PixPal PAL[] = {
        { 'C', 0xde886d }, { 'E', 0x1a0e08 }, { 'L', 0x1a0e08 },
        { 'M', 0x2b120a }, { 'W', 0xfff3e8 }, { 'S', 0xffbd2e },
    };
    for (int v = 0; v < 9; v++)
        build_map(MAPS[v], map_rows(v), MAPW, PAL, 6, &variant[v]);

    static const PixPal APAL[] = {
        { 'S', 0x6b3f1d }, { 'G', 0x3ddc97 }, { 'R', 0xff4d6d }, { 'H', 0xff8098 },
    };
    build_map(APPLE, APPLE_ROWS, 5, APAL, 4, &apple);

    // Soft shadow: a squashed fan, alpha fading from the center to the rim.
    // Built in cell units (radius ~6.5 cells wide, 2.2 tall).
    {
        int n = 34 * 6;
        float *verts = SDL_calloc((size_t)n, sizeof(float));
        int o = 0;
        verts[o++] = 0; verts[o++] = 0; verts[o++] = 0; verts[o++] = 0; verts[o++] = 0; verts[o++] = 0.22f;
        for (int i = 0; i <= 32; i++) {
            float a = (float)i / 32 * 2.0f * (float)M_PI;
            verts[o++] = cosf(a) * 6.5f;
            verts[o++] = sinf(a) * 2.0f;
            verts[o++] = 0; verts[o++] = 0; verts[o++] = 0; verts[o++] = 0;
        }
        glGenBuffers(1, &shadow_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, shadow_vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(o * sizeof(float)), verts, GL_STATIC_DRAW);
        SDL_free(verts);
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
    cameo(V_HAPPY, V_FED, 2200, (float)SDL_GetTicks() / 1000.0f);
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

// Draw a map mesh with the given model transform.
static void draw_mesh(MapMesh *m, Mat4 model) {
    glUniformMatrix4fv(flat_u_model, 1, GL_FALSE, model);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    bind_flat_attribs();
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo);
    glDrawElements(GL_TRIANGLES, m->nidx, GL_UNSIGNED_INT, NULL);
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
    int disp = shown;
    if (feed_state == 2) disp = V_EAT;
    else if (blink_on > 0 && (shown == V_IDLE || shown == V_WORK)) disp = V_BLINK;

    // Layout: feet on the bottom margin, sprite 9 rows tall (PET_PX px).
    float cell = (float)PET_PX / 9.0f;
    float cx = app_w * 0.5f;
    float feet_y = (float)app_h - (float)PET_FEET_MARGIN;
    int rows = map_rows(disp);

    const Motion M = MOTION[disp];
    phase += dt;
    pop_t += dt * 4;
    if (pop_t > 1) pop_t = 1;
    float pop = 1 - powf(1 - pop_t, 3);

    float bob = sinf(phase * M.bobF) * M.bobA;
    if (M.jumpF) bob += fabsf(sinf(phase * M.jumpF)) * 0.35f;
    // Chomping squash.
    if (feed_state == 2) bob += 0.05f * fabsf(sinf(feed_t * 22.0f));
    spin_acc += M.spin * dt;
    float rotz = sinf(phase * 1.1f) * M.tilt;

    // Model: feet-centered cell grid, scaled to pixels, placed on screen.
    Mat4 model, t_, rz, s_;
    m4_translate(t_, -7.5f, (float)-rows, 0);
    m4_rotZ(rz, rotz);
    m4_scale(s_, cell * pop, cell * pop, 1);
    m4_mul(model, rz, t_);
    m4_mul(model, s_, model);
    m4_translate(t_, cx, feet_y - bob * cell, 0);
    m4_mul(model, t_, model);

    glUseProgram(flat_prog);
    glUniformMatrix4fv(flat_u_mvp, 1, GL_FALSE, ortho);
    glEnable(GL_BLEND);
    // Source-over: alpha must accumulate as coverage (a out = a + dst.a*(1-a)),
    // or the shadow would end up nearly invisible on the desktop.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    // --- Shadow ----------------------------------------------------------------
    {
        Mat4 sm, s_;
        m4_scale(s_, cell, cell, 1);
        m4_translate(t_, cx, feet_y + 0.35f * cell, 0);
        m4_mul(sm, t_, s_);
        glUniformMatrix4fv(flat_u_model, 1, GL_FALSE, sm);
        glBindBuffer(GL_ARRAY_BUFFER, shadow_vbo);
        bind_flat_attribs();
        glDrawArrays(GL_TRIANGLE_FAN, 0, 34);
    }

    // --- Clawd -----------------------------------------------------------------
    draw_mesh(&variant[disp], model);

    // --- The apple -------------------------------------------------------------
    if (feed_state) {
        // Mouth position (variant rows are laid out top-down; the mouth sits
        // around row 4.5).
        float mouth_x = cx + 1.0f * cell;
        float mouth_y = feet_y - 4.5f * cell;
        float ax, ay, ascale = 1.0f, arot = 0;
        if (feed_state == 1) {
            float p = feed_t / 0.6f;
            if (p > 1) p = 1;
            float ease = 1 - powf(1 - p, 3);
            float sx = cx + 34.0f, sy = feet_y + 26.0f;
            ax = sx + (mouth_x - sx) * ease + sinf(p * (float)M_PI) * 14.0f;
            ay = sy + (mouth_y - sy) * ease;
            arot = sinf(p * (float)M_PI * 2.0f) * 0.4f;
        } else {
            ax = mouth_x;
            ay = mouth_y;
            ascale = 1.0f - feed_t / 0.75f; // each chomp takes a bite
            arot = sinf(feed_t * 22.0f) * 0.15f;
        }
        Mat4 am;
        m4_translate(t_, -2.5f, -2.5f, 0); // center the 5x5 map
        m4_rotZ(rz, arot);
        m4_mul(am, rz, t_);
        m4_scale(s_, cell * 0.95f * ascale, cell * 0.95f * ascale, 1);
        m4_mul(am, s_, am);
        m4_translate(t_, ax, ay, 0);
        m4_mul(am, t_, am);
        draw_mesh(&apple, am);
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
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    // --- zZz while dozing -------------------------------------------------------
    if (shown == V_DOZE) {
        float head_top = (float)PET_HEAD_TOP(app_h);
        static const char zc[3] = { 'z', 'Z', 'Z' };
        for (int i = 0; i < 3; i++) {
            float p = fmodf(t * 0.22f + i * 0.33f, 1.0f);
            float a = sinf(p * (float)M_PI) * 0.8f;
            char g[2] = { zc[i], 0 };
            font_draw(g, cx + 40.0f + i * 9.0f + p * 12.0f,
                      head_top - 6.0f - p * 34.0f,
                      10.0f + i * 4.0f, (Vec4){ 0.753f, 0.729f, 1.0f, a }, 1);
        }
    }
}
