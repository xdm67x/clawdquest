// ---------------------------------------------------------------------------
// The quest card. A rounded glass card that opens above Clawd when he is
// clicked: level badge, XP bar, streak stats, the quest list (scrollable,
// click a quest to run its chronometer, check to complete, x to delete),
// an add bar, and a close (x) button. Popups, toasts and confetti are
// anchored to the pet rather than a window edge.
// ---------------------------------------------------------------------------

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
#include "pet.h"

// Palette (from the original style.css :root).
static const Vec4 C_TXT = { 0.957f, 0.941f, 1.0f, 1 };
static const Vec4 C_DIM = { 0.604f, 0.565f, 0.784f, 1 };
static const Vec4 C_GLASS = { 0.094f, 0.071f, 0.180f, 0.82f };
static const Vec4 C_LINE = { 0.627f, 0.510f, 1.0f, 0.22f };
static const Vec4 C_ACC = { 0.545f, 0.361f, 0.965f, 1 };
static const Vec4 C_ACC2 = { 0.753f, 0.518f, 0.988f, 1 };
static const Vec4 C_PINK = { 0.925f, 0.282f, 0.600f, 1 };
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
    static const char *rr_attribs[] = { "a_pos", NULL };
    static const char *pt2_attribs[] = { "a_pos", "a_col", "a_size", NULL };
    rr_prog = gl_program_at(RR_VS, RR_FS, rr_attribs);
    rr_u_mvp = glGetUniformLocation(rr_prog, "u_mvp");
    rr_u_rect = glGetUniformLocation(rr_prog, "u_rect");
    rr_u_r = glGetUniformLocation(rr_prog, "u_r");
    rr_u_fill = glGetUniformLocation(rr_prog, "u_fill");
    rr_u_fill2 = glGetUniformLocation(rr_prog, "u_fill2");
    rr_u_gaxis = glGetUniformLocation(rr_prog, "u_gaxis");
    rr_u_border = glGetUniformLocation(rr_prog, "u_border");
    rr_u_bw = glGetUniformLocation(rr_prog, "u_bw");
    glGenBuffers(1, &rr_vbo);

    pt2_prog = gl_program_at(PT2_VS, PT2_FS, pt2_attribs);
    pt2_u_mvp = glGetUniformLocation(pt2_prog, "u_mvp");
    glGenBuffers(1, &pt2_vbo);
}

// Rounded rect: gradient from fill to fill2 when gaxis != 0, plus border.
// The shader outputs premultiplied alpha, so blend accordingly.
static void rrect(float x, float y, float w, float h, float r,
                  Vec4 fill, Vec4 fill2, float gaxis, Vec4 border, float bw) {
    Mat4 m;
    m4_ortho(m, (float)app_w, (float)app_h);
    glUseProgram(rr_prog);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
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
// Text input buffer (add bar)
// ---------------------------------------------------------------------------

static char input_buf[256];
static int input_len;

// ---------------------------------------------------------------------------
// Popups, toasts, confetti — anchored to the pet.
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
    if (x < 0) x = (app_w - font_measure(pops[i].text, 24)) * 0.5f;
    if (y < 0) y = (float)PET_HEAD_TOP(app_h) - 8;
    pops[i].x = x;
    pops[i].y = y;
    pops[i].t0 = (float)SDL_GetTicks() / 1000.0f;
}

void ui_toast(const char *text) {
    SDL_snprintf(toast.text, sizeof toast.text, "%s", text);
    toast.t0 = (float)SDL_GetTicks() / 1000.0f;
    toast_on = true;
}

void ui_confetti(int n) {
    float ox = app_w * 0.5f, oy = (float)PET_HEAD_TOP(app_h) - 30;
    for (int i = 0; i < n && nconf < MAXCONF; i++) {
        conf[nconf].x = ox;
        conf[nconf].y = oy;
        conf[nconf].vx = (((float)rand() / (float)RAND_MAX) - 0.5f) * 16 * 60;
        conf[nconf].vy = -(((float)rand() / (float)RAND_MAX) * 14 + 2) * 60;
        conf[nconf].t = 1.5f; // 90 frames at 60fps
        Vec3 c = hsv((float)rand() / (float)RAND_MAX, 0.9f, 0.6f);
        conf[nconf].c = (Vec4){ c.x, c.y, c.z, 1 };
        nconf++;
    }
}

// ---------------------------------------------------------------------------
// Panel state + hit testing
// ---------------------------------------------------------------------------

static bool panel_open;
static bool quit_req;
static float mouse_x = -1, mouse_y = -1;

// Quest list scrolling: the target is authoritative (wheel and drag set it).
static float target_scroll;
static bool dragging;
static float drag_y0, drag_scroll0;
static bool drag_moved;

typedef struct {
    Quest *q;
    Rect cb;    // checkbox rect
    Rect txt;  // text rect
    Rect xbtn; // delete rect
    bool is_run;
} RowHit;
static RowHit rows[256]; // one slot per visible row (the band shows ~5)
static int nrows;
static Rect card_rect, band_rect, add_btn, close_btn;

bool ui_panel_open(void) { return panel_open; }
bool ui_should_quit(void) { return quit_req; }

void ui_set_panel(bool open) {
    panel_open = open;
    pet_interact();
    if (open) return;
    input_len = 0;
    input_buf[0] = 0;
    target_scroll = 0;
}

bool ui_on_card(float x, float y) {
    return panel_open && x >= card_rect.x && x <= card_rect.x + card_rect.w &&
           y >= card_rect.y && y <= card_rect.y + card_rect.h;
}

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

// ---------------------------------------------------------------------------
// Panel layout
// ---------------------------------------------------------------------------

static void panel_frame(float t) {
    (void)t;
    float cw = 300;
    if (cw > app_w - 20) cw = (float)app_w - 20;
    float cx = (app_w - cw) * 0.5f, cy = 10, ch = 274;
    card_rect = (Rect){ cx, cy, cw, ch };
    rrect(cx, cy, cw, ch, 18, C_GLASS, C_NONE, 0, C_LINE, 1);

    float ix = cx + 14, iw = cw - 28; // inner box

    // Header: level badge, name, rank.
    rrect(ix, cy + 12, 30, 30, 9, C_ACC, C_PINK, 2, C_NONE, 0);
    {
        char n[8];
        SDL_snprintf(n, sizeof n, "%d", lvl(S.xp));
        float w = font_measure(n, 13);
        font_draw(n, ix + 15 - w / 2, cy + 32, 13, C_WHITE, 1);
    }
    font_draw("Clawd Quest", ix + 40, cy + 25, 14, C_TXT, 1);
    {
        char s[48];
        SDL_snprintf(s, sizeof s, "Lv %d \xE2\x80\xA2 %s", lvl(S.xp), title_of(lvl(S.xp)));
        font_draw(s, ix + 40, cy + 41, 10, C_DIM, 0);
    }

    // Close button, top-right.
    float qb = 22;
    close_btn = (Rect){ cx + cw - 14 - qb, cy + 15, qb, qb };
    bool q_hover = mouse_x >= close_btn.x && mouse_x <= close_btn.x + close_btn.w &&
                   mouse_y >= close_btn.y && mouse_y <= close_btn.y + close_btn.h;
    rrect(close_btn.x, close_btn.y, close_btn.w, close_btn.h, 8,
          rgba(0xffffff, q_hover ? 0.18f : 0.07f), C_NONE, 0, rgba(0xffffff, 0.16f), 1);
    {
        const char *x = "\xC3\x97"; // × (U+00D7)
        font_draw(x, close_btn.x + (qb - font_measure(x, 12)) * 0.5f,
                  close_btn.y + 15, 12, q_hover ? C_WHITE : C_TXT, 0);
    }

    // XP bar.
    int l = lvl(S.xp);
    long a = lvlStart(l), b = lvlStart(l + 1);
    float bx = ix, bw = iw - 92, by = cy + 52;
    rrect(bx, by, bw, 7, 3.5f, rgba(0x000000, 0.40f), C_NONE, 0, rgba(0xffffff, 0.06f), 1);
    float frac = (float)(S.xp - a) / (float)(b - a);
    if (frac > 0) {
        float fw = bw * frac;
        if (fw < 7) fw = 7;
        if (frac > 1) fw = bw;
        rrect(bx, by, fw, 7, 3.5f, C_XP, C_XP2, 1, C_NONE, 0);
    }
    {
        char s[48];
        SDL_snprintf(s, sizeof s, "%ld / %ld XP", S.xp - a, b - a);
        font_draw(s, cx + cw - 14 - font_measure(s, 9), by + 7.5f, 9, C_DIM, 0);
    }

    // Stats line.
    {
        char s[96];
        char th[16];
        fmt_time_h(th, sizeof th, S.time);
        SDL_snprintf(s, sizeof s, "%d-day streak \xE2\x80\xA2 %ld done \xE2\x80\xA2 %s focused", S.streak, S.total, th);
        font_draw(s, ix, cy + 78, 10, C_DIM, 0);
    }

    // Quest band (scrollable). The add bar is docked to the card bottom.
    float add_y = cy + ch - 14 - 32;
    float row_h = 30, gap = 6;
    band_rect = (Rect){ ix, cy + 92, iw, add_y - 8 - (cy + 92) };

    // Sort done quests to the bottom (stable).
    int ord2[256];
    int m = 0;
    for (int i = 0; i < S.nquests; i++) if (!S.quests[i].done) ord2[m++] = i;
    for (int i = 0; i < S.nquests; i++) if (S.quests[i].done) ord2[m++] = i;

    float content_h = m > 0 ? m * (row_h + gap) - gap : 0;
    float max_scroll = content_h > band_rect.h ? content_h - band_rect.h : 0;
    if (target_scroll < 0) target_scroll = 0;
    if (target_scroll > max_scroll) target_scroll = max_scroll;

    nrows = 0;
    if (band_rect.h > 0) {
        float scale = (float)app_pw / (float)app_w;
        glEnable(GL_SCISSOR_TEST);
        glScissor((int)(band_rect.x * scale),
                  (int)((app_h - (band_rect.y + band_rect.h)) * scale),
                  (int)(band_rect.w * scale), (int)(band_rect.h * scale));
        for (int vi = 0; vi < m; vi++) {
            float ry = band_rect.y + vi * (row_h + gap) - target_scroll;
            if (ry + row_h < band_rect.y || ry > band_rect.y + band_rect.h) continue;
            int qi = ord2[vi];
            Quest *q = &S.quests[qi];
            bool hovered = mouse_x >= band_rect.x && mouse_x <= band_rect.x + band_rect.w &&
                           mouse_y >= ry && mouse_y <= ry + row_h;
            bool is_act = q->id == active;
            bool is_run = is_act && running;

            // Row background.
            Vec4 bg = rgba(0xffffff, 0.04f);
            Vec4 border = C_NONE;
            if (is_act) {
                bg = rgba(0x8b5cf6, 0.20f);
                border = C_ACC2;
            } else if (hovered) {
                bg = rgba(0xffffff, 0.08f);
            }
            rrect(band_rect.x, ry, band_rect.w, row_h, 10, bg, C_NONE, 0, border, 1);

            RowHit *rh = &rows[nrows++];
            rh->q = q;
            rh->is_run = is_run;

            // Checkbox.
            float ccx = band_rect.x + 5, ccy = ry + (row_h - 18) / 2;
            rrect(ccx, ccy, 18, 18, 6, q->done ? C_OK : C_NONE, C_NONE, 0,
                  q->done ? C_OK : rgba(0xffffff, 0.27f), 2);
            if (q->done) {
                unsigned ch = 0x2713;
                char g[5] = { 0 };
                g[0] = (char)(0xE0 | (ch >> 12)); g[1] = (char)(0x80 | ((ch >> 6) & 0x3F));
                g[2] = (char)(0x80 | (ch & 0x3F));
                float gw = font_measure(g, 11);
                font_draw(g, ccx + 9 - gw / 2, ccy + 13.5f, 11, rgba(0x06281b, 1), 1);
            }
            rh->cb = (Rect){ ccx, ccy, 18, 18 };

            // Quest text (with ellipsis; strikethrough when done).
            float tx = ccx + 26;
            float maxw = band_rect.w - 26 - 26 - 56;
            char shown[256];
            fit_text(q->text, 12, maxw, shown, sizeof shown);
            Vec4 tcol = q->done ? mul(C_TXT, 0.55f) : C_TXT;
            font_draw(shown, tx, ry + row_h / 2 + 4, 12, tcol, 0);
            if (q->done) {
                float tw = font_measure(shown, 12);
                rrect(tx, ry + row_h / 2 - 2, tw, 1.5f, 1, tcol, C_NONE, 0, C_NONE, 0);
            }
            rh->txt = (Rect){ tx, ry, maxw, row_h };

            // Chronometer chip.
            if (is_run || q->secs > 0) {
                char s[16];
                fmt_mmss(s, sizeof s, q->secs);
                float sw = font_measure(s, 10) + 12;
                float sx = band_rect.x + band_rect.w - 26 - sw;
                if (is_run) {
                    rrect(sx, ry + (row_h - 16) / 2, sw, 16, 8, rgba(0x8b5cf6, 0.13f), C_NONE, 0, C_NONE, 0);
                    font_draw(s, sx + 6, ry + row_h / 2 + 3.5f, 10, C_ACC2, 1);
                } else {
                    font_draw(s, sx + 6, ry + row_h / 2 + 3.5f, 10, C_DIM, 0);
                }
            }

            // Delete button, visible on hover.
            float xx = band_rect.x + band_rect.w - 24;
            if (hovered)
                font_draw("\xC3\x97", xx + (26 - font_measure("\xC3\x97", 12)) * 0.5f,
                          ry + row_h / 2 + 4, 12, C_DIM, 0);
            rh->xbtn = (Rect){ xx, ry, 26, row_h };
        }
        glDisable(GL_SCISSOR_TEST);
    }

    // Empty board notice.
    if (!S.nquests) {
        const char *l1 = "No quests \xE2\x80\xA2 add one below.";
        font_draw(l1, cx + (cw - font_measure(l1, 11)) * 0.5f,
                  band_rect.y + band_rect.h * 0.5f + 4, 11, C_DIM, 0);
    }

    // --- Add bar -------------------------------------------------------------
    rrect(ix, add_y, iw, 32, 16, C_GLASS, C_NONE, 0, C_LINE, 1);
    if (input_len == 0) {
        const char *ph = "Add a quest...";
        font_draw(ph, ix + 12, add_y + 21, 12, C_DIM, 0);
    } else {
        font_draw(input_buf, ix + 12, add_y + 21, 12, C_TXT, 0);
    }
    if (fmodf((float)SDL_GetTicks() / 1000.0f, 1.0f) < 0.5f) {
        float cx2 = ix + 12 + (input_len ? font_measure(input_buf, 12) : 0);
        rrect(cx2, add_y + 10, 1.5f, 13, 1, C_ACC2, C_NONE, 0, C_NONE, 0);
    }
    float ab_w = 56;
    add_btn = (Rect){ ix + iw - ab_w - 4, add_y + 4, ab_w, 24 };
    bool ab_hover = mouse_x >= add_btn.x && mouse_x <= add_btn.x + add_btn.w &&
                    mouse_y >= add_btn.y && mouse_y <= add_btn.y + add_btn.h;
    rrect(add_btn.x, add_btn.y, add_btn.w, add_btn.h, 12, C_ACC, C_ACC2, 1, C_NONE, 0);
    if (ab_hover) rrect(add_btn.x, add_btn.y, add_btn.w, add_btn.h, 12, rgba(0xffffff, 0.12f), C_NONE, 0, C_NONE, 0);
    const char *add = "Add";
    font_draw(add, add_btn.x + (add_btn.w - font_measure(add, 12)) * 0.5f,
              add_btn.y + 15.5f, 12, C_WHITE, 1);
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void ui_frame(float t, float dt) {
    // Keep the pet in sync with the game state every frame: fed when every
    // quest is done, dormant when the board is empty, working while a
    // chronometer runs.
    int rem = 0;
    for (int i = 0; i < S.nquests; i++)
        if (!S.quests[i].done) rem++;
    pet_set(S.nquests > 0 && rem == 0, S.nquests == 0, running);

    if (panel_open) panel_frame(t);

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
        font_draw(pops[i].text, pops[i].x, pops[i].y - 80 * ease, 24,
                  (Vec4){ C_XP.r, C_XP.g, C_XP.b, alpha }, 1);
    }

    // --- Toast ---------------------------------------------------------------
    if (toast_on) {
        float age = t - toast.t0;
        if (age > 2.8f) toast_on = false;
        else {
            float tw = font_measure(toast.text, 13) + 36;
            rrect((app_w - tw) * 0.5f, 12, tw, 34, 14, C_ACC, C_PINK, 1, C_NONE, 0);
            font_draw(toast.text, (app_w - font_measure(toast.text, 13)) * 0.5f, 33, 13, C_WHITE, 1);
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
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
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

static void ui_click(float x, float y) {
    for (int i = 0; i < nrows; i++) {
        RowHit *r = &rows[i];
        if (!in_rect(r->cb, x, y)) continue;
        if (r->q->done) undo(r->q);
        else complete(r->q, x, y);
        return;
    }
    for (int i = 0; i < nrows; i++) {
        RowHit *r = &rows[i];
        if (in_rect(r->xbtn, x, y)) {
            del_quest(r->q);
            return;
        }
    }
    for (int i = 0; i < nrows; i++) {
        RowHit *r = &rows[i];
        if (in_rect(r->txt, x, y)) {
            toggle_timer(r->q->id);
            return;
        }
    }
    if (in_rect(add_btn, x, y)) ui_key(SDLK_RETURN);
    if (in_rect(close_btn, x, y)) quit_req = true;
}

void ui_mouse_down(float x, float y) {
    if (!panel_open) return;
    dragging = x >= band_rect.x && x <= band_rect.x + band_rect.w &&
               y >= band_rect.y && y <= band_rect.y + band_rect.h;
    drag_moved = false;
    drag_y0 = y;
    drag_scroll0 = target_scroll;
}

void ui_mouse_up(float x, float y) {
    // A press that stayed put is a click; a drag suppresses the click.
    if (dragging && !drag_moved && panel_open) ui_click(x, y);
    dragging = false;
}

void ui_wheel(float dy) {
    if (!panel_open) return;
    if (mouse_x >= band_rect.x && mouse_x <= band_rect.x + band_rect.w &&
        mouse_y >= band_rect.y && mouse_y <= band_rect.y + band_rect.h)
        target_scroll -= dy * 36; // one row per notch
}

void ui_motion(float x, float y) {
    mouse_x = x;
    mouse_y = y;
    if (dragging) {
        float dy = y - drag_y0;
        if (dy > 6 || dy < -6) drag_moved = true;
        if (drag_moved) target_scroll = drag_scroll0 - dy; // rows follow the finger
    }
}

void ui_text(const char *utf8) {
    if (!panel_open) return;
    int n = (int)SDL_strlen(utf8);
    if (input_len + n < (int)sizeof input_buf - 1) {
        memcpy(input_buf + input_len, utf8, (size_t)n);
        input_len += n;
        input_buf[input_len] = 0;
    }
}

void ui_key(SDL_Keycode key) {
    if (!panel_open) return;
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
