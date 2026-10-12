#include "ui.h"

#include "glutil.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "font.h"
#include "game.h"
#include "math.h"
#include "scene.h"

// Palette (from style.css :root).
static const Vec4 C_TXT = { 0.957f, 0.941f, 1.0f, 1 };
static const Vec4 C_DIM = { 0.604f, 0.565f, 0.784f, 1 };
static const Vec4 C_GLASS = { 0.094f, 0.071f, 0.180f, 0.62f };
static const Vec4 C_LINE = { 0.627f, 0.510f, 1.0f, 0.22f };
static const Vec4 C_ACC = { 0.545f, 0.361f, 0.965f, 1 };
static const Vec4 C_ACC2 = { 0.753f, 0.518f, 0.988f, 1 };
static const Vec4 C_PINK = { 0.925f, 0.282f, 0.600f, 1 };
static const Vec4 C_HP = { 1.0f, 0.302f, 0.427f, 1 };
static const Vec4 C_HP2 = { 1.0f, 0.478f, 0.576f, 1 };
static const Vec4 C_XP = { 1.0f, 0.824f, 0.247f, 1 };
static const Vec4 C_XP2 = { 1.0f, 0.624f, 0.110f, 1 };
static const Vec4 C_OK = { 0.239f, 0.863f, 0.592f, 1 };
static const Vec4 C_WHITE = { 1, 1, 1, 1 };
static const Vec4 C_NONE = { 0, 0, 0, 0 };

static Vec4 rgba(int hex, float a) { Vec3 c = hex3(hex); Vec4 v = { c.x, c.y, c.z, a }; return v; }
static Vec4 mul(Vec4 c, float k) { Vec4 v = { c.r * k, c.g * k, c.b * k, c.a }; return v; }

// ---------------------------------------------------------------------------
// Rounded-rect shader: SDF corners, optional gradient fill, 1px-ish border.
// ---------------------------------------------------------------------------

static GLuint rr_prog, rr_vbo;
static GLint rr_u_mvp, rr_u_rect, rr_u_r, rr_u_fill, rr_u_fill2, rr_u_gaxis,
            rr_u_border, rr_u_bw;

static const char *RR_VS =
    "#version 150\n"
    "in vec2 a_pos;\n"
    "uniform mat4 u_mvp;\n"
    "out vec2 v_p;\n"
    "void main() {\n"
    "    v_p = a_pos;\n"
    "    gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char *RR_FS =
    "#version 150\n"
    "in vec2 v_p;\n"
    "uniform vec4 u_rect;   // x y w h\n"
    "uniform float u_r;     // corner radius\n"
    "uniform vec4 u_fill;\n"
    "uniform vec4 u_fill2;  // gradient end\n"
    "uniform float u_gaxis; // 0 none, 1 horizontal, 2 vertical\n"
    "uniform vec4 u_border;\n"
    "uniform float u_bw;   // border width\n"
    "out vec4 frag;\n"
    "float sd(vec2 p) {\n"
    "    vec2 h = u_rect.zw * 0.5 - vec2(u_r);\n"
    "    vec2 q = abs(p) - h;\n"
    "    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - u_r;\n"
    "}\n"
    "void main() {\n"
    "    vec2 c = v_p - (u_rect.xy + u_rect.zw * 0.5);\n"
    "    float d = sd(c);\n"
    "    float inside = 1.0 - smoothstep(-0.5, 0.5, d);\n"
    "    float band = 1.0 - smoothstep(u_bw - 0.5, u_bw + 0.5, abs(d));\n"
    "    float g = u_gaxis < 0.5 ? 0.0\n"
    "            : u_gaxis < 1.5 ? clamp((v_p.x - u_rect.x) / max(u_rect.z, 1.0), 0.0, 1.0)\n"
    "            : clamp((v_p.y - u_rect.y) / max(u_rect.w, 1.0), 0.0, 1.0);\n"
    "    vec4 fill = mix(u_fill, u_fill2, g);\n"
    "    fill.a *= inside;\n"
    "    vec4 b = vec4(u_border.rgb, u_border.a * band);\n"
    "    frag = vec4(b.rgb * b.a + fill.rgb * fill.a * (1.0 - b.a),\n"
    "                b.a + fill.a * (1.0 - b.a));\n"
    "}\n";

// 2D points (confetti).
static GLuint pt2_prog, pt2_vbo;
static GLint pt2_u_mvp;

static const char *PT2_VS =
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

static const char *PT2_FS =
    "#version 150\n"
    "in vec4 v_col;\n"
    "out vec4 frag;\n"
    "void main() { frag = v_col; }\n";

void ui_init(void) {
    rr_prog = gl_program(RR_VS, RR_FS);
    rr_u_mvp = glGetUniformLocation(rr_prog, "u_mvp");
    rr_u_rect = glGetUniformLocation(rr_prog, "u_rect");
    rr_u_r = glGetUniformLocation(rr_prog, "u_r");
    rr_u_fill = glGetUniformLocation(rr_prog, "u_fill");
    rr_u_fill2 = glGetUniformLocation(rr_prog, "u_fill2");
    rr_u_gaxis = glGetUniformLocation(rr_prog, "u_gaxis");
    rr_u_border = glGetUniformLocation(rr_prog, "u_border");
    rr_u_bw = glGetUniformLocation(rr_prog, "u_bw");
    glGenBuffers(1, &rr_vbo);

    pt2_prog = gl_program(PT2_VS, PT2_FS);
    pt2_u_mvp = glGetUniformLocation(pt2_prog, "u_mvp");
    glGenBuffers(1, &pt2_vbo);
}

// Rounded rect: gradient from fill to fill2 when gaxis != 0, plus border.
static void rrect(float x, float y, float w, float h, float r,
                  Vec4 fill, Vec4 fill2, float gaxis, Vec4 border, float bw) {
    Mat4 m;
    m4_ortho(m, (float)app_w, (float)app_h);
    glUseProgram(rr_prog);
    glUniformMatrix4fv(rr_u_mvp, 1, GL_FALSE, m);
    glUniform4f(rr_u_rect, x, y, w, h);
    glUniform1f(rr_u_r, r);
    glUniform4f(rr_u_fill, fill.r, fill.g, fill.b, fill.a);
    glUniform4f(rr_u_fill2, fill2.r, fill2.g, fill2.b, fill2.a);
    glUniform1f(rr_u_gaxis, gaxis);
    glUniform4f(rr_u_border, border.r, border.g, border.b, border.a);
    glUniform1f(rr_u_bw, bw);
    float pad = 2.0f;
    const float quad[12] = { x - pad, y - pad, x + w + pad, y - pad, x + w + pad, y + h + pad,
                             x - pad, y - pad, x + w + pad, y + h + pad, x - pad, y + h + pad };
    glBindBuffer(GL_ARRAY_BUFFER, rr_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
}

// ---------------------------------------------------------------------------
// Text input buffer (capture bar)
// ---------------------------------------------------------------------------

static char input_buf[256];
static int input_len;

// ---------------------------------------------------------------------------
// Popups, toasts, confetti (fx.js)
// ---------------------------------------------------------------------------

#define MAXPOP 16
static struct { char text[32]; float x, y, t0; } pops[MAXPOP];
static int npops;
static struct { char text[80]; float t0; } toast;
static bool toast_on;

#define MAXCONF 400
static struct { float x, y, vx, vy, t; Vec4 c; } conf[MAXCONF];
static int nconf;

void ui_pop(const char *text, float x, float y) {
    int i = npops < MAXPOP ? npops++ : (rand() % MAXPOP);
    SDL_snprintf(pops[i].text, sizeof pops[i].text, "%s", text);
    pops[i].x = x < 0 ? app_w * 0.4f : x;
    pops[i].y = y < 0 ? 260 : y;
    pops[i].t0 = (float)SDL_GetTicks() / 1000.0f;
}

void ui_toast(const char *text) {
    SDL_snprintf(toast.text, sizeof toast.text, "%s", text);
    toast.t0 = (float)SDL_GetTicks() / 1000.0f;
    toast_on = true;
}

void ui_confetti(int n) {
    for (int i = 0; i < n && nconf < MAXCONF; i++) {
        conf[nconf].x = app_w * 0.4f;
        conf[nconf].y = app_h / 3.0f;
        conf[nconf].vx = (((float)rand() / (float)RAND_MAX) - 0.5f) * 16 * 60;
        conf[nconf].vy = -(((float)rand() / (float)RAND_MAX) * 14 + 2) * 60;
        conf[nconf].t = 1.5f; // 90 frames at 60fps
        Vec3 c = hsv((float)rand() / (float)RAND_MAX, 0.9f, 0.6f);
        conf[nconf].c = (Vec4){ c.x, c.y, c.z, 1 };
        nconf++;
    }
}

// ---------------------------------------------------------------------------
// Layout + hit testing
// ---------------------------------------------------------------------------

static float mouse_x = -1, mouse_y = -1;

typedef struct {
    Quest *q;
    float row_y;
    Rect cb;     // checkbox rect
    Rect txt;    // text rect
    Rect xbtn;   // delete rect
    int is_run;
} RowHit;
static RowHit rows[32];
static int nrows;
static Rect add_btn;
static float list_x, list_w;

// Truncate text to maxw pixels, appending "..." when it does not fit.
static void fit_text(const char *s, float size, float maxw, char *out, int outn) {
    if (font_measure(s, size) <= maxw) {
        SDL_snprintf(out, (size_t)outn, "%s", s);
        return;
    }
    int n = (int)SDL_strlen(s);
    while (n > 1) {
        char tmp[256];
        SDL_snprintf(tmp, sizeof tmp, "%.*s...", n - 1, s);
        if (font_measure(tmp, size) <= maxw) {
            SDL_snprintf(out, (size_t)outn, "%s", tmp);
            return;
        }
        n--;
    }
    SDL_snprintf(out, (size_t)outn, "...");
}

void ui_frame(float t, float dt) {
    (void)dt;
    int W = app_w, H = app_h;
    float mx = mouse_x, my = mouse_y;

    // --- Hero panel ----------------------------------------------------------
    const float px = 22, py = 18;
    float hero_h = 70;
    rrect(px, py, 430, hero_h, 20, C_GLASS, C_NONE, 0, C_LINE, 1);

    // Level badge: gradient tile with the level number.
    rrect(px + 12, py + 12, 46, 46, 14, C_ACC, C_PINK, 2, C_NONE, 0);
    {
        char n[8];
        SDL_snprintf(n, sizeof n, "%d", lvl(S.xp));
        float w = font_measure(n, 20);
        font_draw(n, px + 12 + 23 - w / 2, py + 12 + 30, 20, C_WHITE, 1);
    }

    // Rank title and streak.
    font_draw(title_of(lvl(S.xp)), px + 70, py + 29, 16, C_TXT, 1);
    {
        char s[48];
        SDL_snprintf(s, sizeof s, "%d-day streak", S.streak);
        font_draw(s, px + 70, py + 47, 12, C_DIM, 0);
    }

    // XP bar.
    int l = lvl(S.xp);
    long a = lvlStart(l), b = lvlStart(l + 1);
    float bx = px + 210, bw = 130;
    rrect(bx, py + 25, bw, 10, 5, rgba(0x000000, 0.40f), C_NONE, 0, rgba(0xffffff, 0.06f), 1);
    float frac = (float)(S.xp - a) / (float)(b - a);
    if (frac > 0) {
        float fw = bw * frac;
        if (fw < 10) fw = 10;
        if (frac > 1) fw = bw;
        rrect(bx, py + 25, fw, 10, 5, C_XP, C_XP2, 1, C_NONE, 0);
    }
    {
        char s[64];
        SDL_snprintf(s, sizeof s, "%ld / %ld XP to level %d", S.xp - a, b - a, l + 1);
        font_draw(s, bx, py + 47, 11, C_DIM, 0);
    }

    // Stats: quests completed and focused time.
    {
        char v[32];
        SDL_snprintf(v, sizeof v, "%ld", S.total);
        float w = font_measure(v, 20);
        font_draw(v, px + 356 - w / 2, py + 30, 20, C_TXT, 1);
        const char *lab = "QUESTS";
        font_draw(lab, px + 356 - font_measure(lab, 9) / 2, py + 48, 9, C_DIM, 0);
    }
    {
        char v[16];
        fmt_time_h(v, sizeof v, S.time);
        float w = font_measure(v, 20);
        font_draw(v, px + 430 - 22 - w / 2, py + 30, 20, C_TXT, 1);
        const char *lab = "FOCUSED";
        font_draw(lab, px + 430 - 22 - font_measure(lab, 9) / 2, py + 48, 9, C_DIM, 0);
    }

    // --- Boss bar ------------------------------------------------------------
    int tot = S.nquests, rem = 0;
    for (int i = 0; i < S.nquests; i++)
        if (!S.quests[i].done) rem++;
    bool dead = S.nquests > 0 && rem == 0;
    bool empty = S.nquests == 0;

    scene_set(S.foe, tot ? (float)rem / tot : 1, dead, empty, running);

    float bbw = W < 578 ? W - 60 : 520;
    float bbx = (W - bbw) / 2;
    const char *bname;
    char nb[64];
    if (empty) bname = "Clawd sleeps... add a quest to wake him";
    else if (dead) { SDL_snprintf(nb, sizeof nb, "Clawd — DEFEATED"); bname = nb; }
    else { SDL_snprintf(nb, sizeof nb, "Clawd · %d quest%s left", rem, rem > 1 ? "s" : ""); bname = nb; }
    font_draw(bname, (W - font_measure(bname, 17)) / 2, py + hero_h + 28, 17, C_TXT, 1);

    float hb_y = py + hero_h + 36;
    rrect(bbx, hb_y, bbw, 18, 9, rgba(0x000000, 0.67f), C_NONE, 0, rgba(0xff4d6d, 0.33f), 1);
    if (tot) {
        float fw = bbw * (float)rem / tot;
        if (fw < 18) fw = 18;
        rrect(bbx, hb_y, fw, 18, 9, C_HP, C_HP2, 1, C_NONE, 0);
    }
    if (tot) {
        char s[48];
        SDL_snprintf(s, sizeof s, "HP %d / %d", rem, tot);
        font_draw(s, (W - font_measure(s, 11)) / 2, hb_y + 13, 11, C_WHITE, 1);
    }

    // --- Quest list ----------------------------------------------------------
    list_w = W * 0.92f;
    if (list_w > 620) list_w = 620;
    list_x = (W - list_w) / 2;
    float cap_h = 52, cap_y = H - 24 - cap_h;
    float gap = 8, row_h = 44;
    int fit = (int)((148 + gap) / (row_h + gap)); // max visible rows
    if (fit < 1) fit = 1;
    if (fit > 32) fit = 32;
    int start = S.nquests - fit;
    if (start < 0) start = 0;

    // Sort done quests to the bottom (stable), take the tail that fits.
    int ord2[256];
    int m = 0;
    for (int i = 0; i < S.nquests; i++) if (!S.quests[i].done) ord2[m++] = i;
    for (int i = 0; i < S.nquests; i++) if (S.quests[i].done) ord2[m++] = i;

    int visible = m - start;
    if (visible < 0) visible = 0;

    float list_top = cap_y - gap - (visible > 0 ? visible * (row_h + gap) - gap : 0);
    nrows = 0;
    for (int vi = 0; vi < visible; vi++) {
        int qi = ord2[start + vi];
        Quest *q = &S.quests[qi];
        float ry = list_top + vi * (row_h + gap);
        bool hovered = mx >= list_x && mx <= list_x + list_w && my >= ry && my <= ry + row_h;
        bool is_act = q->id == active;
        int is_run = is_act && running;

        RowHit *rh = &rows[nrows++];
        rh->q = q;
        rh->is_run = is_run;
        rh->row_y = ry;

        // Row background.
        Vec4 bg = rgba(0xffffff, 0.04f);
        Vec4 border = C_NONE;
        if (is_act) {
            bg = rgba(0x8b5cf6, 0.20f);
            border = C_ACC2;
        } else if (hovered) {
            bg = rgba(0xffffff, 0.08f);
        }
        rrect(list_x, ry, list_w, row_h, 14, bg, C_NONE, 0, border, 1);

        // Checkbox.
        float cx = list_x + 12, cy = ry + (row_h - 22) / 2;
        rrect(cx, cy, 22, 22, 7, q->done ? C_OK : C_NONE, C_NONE, 0,
              q->done ? C_OK : rgba(0xffffff, 0.27f), 2);
        if (q->done) {
            unsigned ch = 0x2713;
            char g[5] = { 0 };
            // U+2713 as UTF-8
            g[0] = (char)(0xE0 | (ch >> 12)); g[1] = (char)(0x80 | ((ch >> 6) & 0x3F));
            g[2] = (char)(0x80 | (ch & 0x3F));
            float gw = font_measure(g, 13);
            font_draw(g, cx + 11 - gw / 2, cy + 16, 13, rgba(0x06281b, 1), 1);
        }
        rh->cb = (Rect){ cx, cy, 22, 22 };

        // Quest text (with ellipsis; strikethrough when done).
        float tx = cx + 32;
        float maxw = list_w - 32 - 22 - 90;
        char shown[256];
        fit_text(q->text, 14, maxw, shown, sizeof shown);
        Vec4 tcol = q->done ? mul(C_TXT, 0.55f) : C_TXT;
        font_draw(shown, tx, ry + row_h / 2 + 5, 14, tcol, 0);
        if (q->done) {
            float tw = font_measure(shown, 14);
            rrect(tx, ry + row_h / 2 - 1, tw, 1.5f, 1, tcol, C_NONE, 0, C_NONE, 0);
        }
        rh->txt = (Rect){ tx, ry, maxw, row_h };

        // Chronometer chip.
        long secs = q->secs;
        if (is_run || secs > 0) {
            char s[16];
            fmt_mmss(s, sizeof s, secs);
            float sw = font_measure(s, 11) + 16;
            float sx = list_x + list_w - 22 - 44 - sw;
            if (is_run) {
                rrect(sx, ry + (row_h - 18) / 2, sw, 18, 9, rgba(0x8b5cf6, 0.13f), C_NONE, 0, C_NONE, 0);
                font_draw(s, sx + 8, ry + row_h / 2 + 4, 11, C_ACC2, 1);
            } else if (is_act) {
                font_draw(s, sx + 8, ry + row_h / 2 + 4, 11, C_ACC2, 1);
            } else {
                font_draw(s, sx + 8, ry + row_h / 2 + 4, 11, C_DIM, 1);
            }
        }

        // Delete button, visible on hover.
        float xx = list_x + list_w - 30;
        float glyph_w = font_measure("\xC3\x97", 13);
        if (hovered)
            font_draw("\xC3\x97", xx + 8 - glyph_w / 2, ry + row_h / 2 + 5, 13, C_DIM, 0);
        rh->xbtn = (Rect){ xx, ry, 30, row_h };
    }

    // Empty board notice.
    if (!S.nquests) {
        float ey = cap_y - gap - 64;
        rrect(list_x, ey, list_w, 64, 14, rgba(0xffffff, 0.03f), C_NONE, 0, C_LINE, 1);
        const char *l1 = "Empty board.";
        font_draw(l1, list_x + (list_w - font_measure(l1, 13)) / 2, ey + 26, 13, C_DIM, 0);
        const char *l2 = "Capture your first quest below to wake the boss.";
        font_draw(l2, list_x + (list_w - font_measure(l2, 13)) / 2, ey + 46, 13, C_DIM, 0);
    }

    // --- Capture bar ---------------------------------------------------------
    rrect(list_x, cap_y, list_w, cap_h, 26, C_GLASS, C_NONE, 0, C_ACC2, 1);

    // Placeholder or typed text.
    if (input_len == 0) {
        const char *ph = "What must be done?";
        font_draw(ph, list_x + 18, cap_y + cap_h / 2 + 5, 14, C_DIM, 0);
    } else {
        font_draw(input_buf, list_x + 18, cap_y + cap_h / 2 + 5, 14, C_TXT, 0);
    }
    // Caret.
    if (fmodf(t, 1.0f) < 0.5f) {
        float cx2 = list_x + 18 + (input_len ? font_measure(input_buf, 14) : 0);
        rrect(cx2, cap_y + cap_h / 2 - 9, 1.5f, 18, 1, C_ACC2, C_NONE, 0, C_NONE, 0);
    }

    // Add button.
    float ab_w = 74;
    add_btn = (Rect){ list_x + list_w - ab_w - 9, cap_y + 9, ab_w, cap_h - 18 };
    bool ab_hover = mx >= add_btn.x && mx <= add_btn.x + add_btn.w && my >= add_btn.y && my <= add_btn.y + add_btn.h;
    rrect(add_btn.x, add_btn.y, add_btn.w, add_btn.h, 17, C_ACC, C_ACC2, 1, C_NONE, 0);
    if (ab_hover) rrect(add_btn.x, add_btn.y, add_btn.w, add_btn.h, 17, rgba(0xffffff, 0.12f), C_NONE, 0, C_NONE, 0);
    const char *add = "Add";
    font_draw(add, add_btn.x + (add_btn.w - font_measure(add, 14)) / 2, add_btn.y + add_btn.h / 2 + 5, 14, C_WHITE, 1);

    // --- Floating +XP pops ---------------------------------------------------
    for (int i = 0; i < npops; i++) {
        float age = t - pops[i].t0;
        if (age > 1.1f) {
            // drop from the pool
            pops[i] = pops[npops - 1];
            npops--;
            i--;
            continue;
        }
        float p = age / 1.1f;
        float ease = 1 - powf(1 - p, 3);
        float alpha = 1 - p * p;
        font_draw(pops[i].text, pops[i].x, pops[i].y - 80 * ease, 24, (Vec4){ C_XP.r, C_XP.g, C_XP.b, alpha }, 1);
    }

    // --- Toast ---------------------------------------------------------------
    if (toast_on) {
        float age = t - toast.t0;
        if (age > 2.8f) toast_on = false;
        else {
            float drop = age < 0.4f ? -30 * (1 - age / 0.4f) : 0;
            float alpha = age > 2.4f ? 1 - (age - 2.4f) / 0.4f : 1;
            float tw = font_measure(toast.text, 14) + 44;
            rrect((W - tw) / 2, 22 + drop, tw, 42, 16, C_ACC, C_PINK, 1, C_NONE, 0);
            font_draw(toast.text, (W - font_measure(toast.text, 14)) / 2, 22 + drop + 27, 14, C_WHITE, 1);
            (void)alpha;
        }
    }

    // --- Confetti ------------------------------------------------------------
    if (nconf) {
        for (int i = 0; i < nconf; i++) {
            conf[i].x += conf[i].vx * dt;
            conf[i].y += conf[i].vy * dt;
            conf[i].vy += 0.45f * 60 * 60 * dt; // gravity (0.45 px/frame^2 in the JS)
            conf[i].t -= dt;
        }
        float buf[MAXCONF * 7];
        int m2 = 0;
        for (int i = 0; i < nconf; i++) {
            if (conf[i].t <= 0) continue;
            buf[m2++] = conf[i].x;
            buf[m2++] = conf[i].y;
            buf[m2++] = conf[i].c.r;
            buf[m2++] = conf[i].c.g;
            buf[m2++] = conf[i].c.b;
            buf[m2++] = conf[i].c.a;
            buf[m2++] = 7;
        }
        // compact dead particles
        int w = 0;
        for (int i = 0; i < nconf; i++)
            if (conf[i].t > 0) conf[w++] = conf[i];
        nconf = w;

        if (m2) {
            Mat4 m;
            m4_ortho(m, (float)app_w, (float)app_h);
            glUseProgram(pt2_prog);
            glUniformMatrix4fv(pt2_u_mvp, 1, GL_FALSE, m);
            glBindBuffer(GL_ARRAY_BUFFER, pt2_vbo);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(m2 * sizeof(float)), buf, GL_STREAM_DRAW);
            GLsizei stride = 7 * sizeof(float);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void *)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void *)(2 * sizeof(float)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void *)(6 * sizeof(float)));
            glDrawArrays(GL_POINTS, 0, m2 / 7);
            glDisableVertexAttribArray(0);
            glDisableVertexAttribArray(1);
            glDisableVertexAttribArray(2);
        }
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

static bool in_rect(Rect r, float x, float y) {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

void ui_click(float x, float y) {
    for (int i = 0; i < nrows; i++) {
        RowHit *r = &rows[i];
        if (!in_rect(r->cb, x, y)) continue;
        if (r->q->done) undo(r->q);
        else complete(r->q, x, y);
        return;
    }
    for (int i = 0; i < nrows; i++) {
        RowHit *r = &rows[i];
        if (in_rect(r->txt, x, y)) {
            toggle_timer(r->q->id);
            return;
        }
    }
    for (int i = 0; i < nrows; i++) {
        RowHit *r = &rows[i];
        if (in_rect(r->xbtn, x, y)) {
            del_quest(r->q);
            return;
        }
    }
    if (in_rect(add_btn, x, y)) ui_key(SDLK_RETURN);
}

void ui_motion(float x, float y) {
    mouse_x = x;
    mouse_y = y;
}

void ui_text(const char *utf8) {
    int n = (int)SDL_strlen(utf8);
    if (input_len + n < (int)sizeof input_buf - 1) {
        memcpy(input_buf + input_len, utf8, (size_t)n);
        input_len += n;
        input_buf[input_len] = 0;
    }
}

void ui_key(SDL_Keycode key) {
    if (key == SDLK_BACKSPACE) {
        // Remove the last UTF-8 sequence.
        while (input_len > 0 && (input_buf[input_len - 1] & 0xC0) == 0x80) input_len--;
        if (input_len > 0) input_len--;
        input_buf[input_len] = 0;
        return;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        // Trim like the JS form did (leading/trailing whitespace).
        char *s = input_buf;
        while (*s == ' ') s++;
        char *e = s + SDL_strlen(s);
        while (e > s && e[-1] == ' ') e--;
        *e = 0;
        if (*s) {
            add_quest(s);
            input_len = 0;
            input_buf[0] = 0;
        }
        return;
    }
}
