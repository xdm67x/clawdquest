#include "font.h"

#include <SDL3/SDL.h>
#include "glutil.h"

#include "app.h"
#include "font_atlas.h"

// Map a codepoint to an atlas index. ASCII is the fast path; the handful of
// extra glyphs after 0x7E use a tiny lookup built once at init.
static int idx_of[128];

static GLuint prog, tex, vbo;
static GLint u_mvp, u_tint;

static const char *VS =
    "#version 150\n"
    "in vec2 a_pos;\n"
    "in vec2 a_uv;\n"
    "in vec4 a_col;\n"
    "uniform mat4 u_mvp;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_col;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    v_col = a_col;\n"
    "    gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char *FS =
    "#version 150\n"
    "in vec2 v_uv;\n"
    "in vec4 v_col;\n"
    "uniform sampler2D u_tex;\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "    float a = texture(u_tex, v_uv).a;\n"
    "    frag = vec4(v_col.rgb, v_col.a * a);\n"
    "}\n";

static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

void font_init(void) {
    for (int i = 0; i < 128; i++) idx_of[i] = -1;
    for (int g = 0; g < FA_COUNT; g++)
        if (FA_CHARS[g] < 128) idx_of[FA_CHARS[g]] = g;

    // Decode base64 atlas into an RGBA texture (rgb = white, a = coverage).
    int nb = FA_B64_LEN / 4 * 3;
    uint8_t *raw = SDL_malloc((size_t)nb);
    for (int i = 0, o = 0; i < FA_B64_LEN; i += 4) {
        int v0 = b64_val(FA_B64[i]), v1 = b64_val(FA_B64[i + 1]);
        int v2 = b64_val(FA_B64[i + 2]), v3 = b64_val(FA_B64[i + 3]);
        uint32_t v = (uint32_t)v0 << 18 | (uint32_t)v1 << 12 | (uint32_t)v2 << 6 | (uint32_t)v3;
        raw[o++] = (uint8_t)(v >> 16);
        if (FA_B64[i + 2] != '=') raw[o++] = (uint8_t)(v >> 8);
        if (FA_B64[i + 3] != '=') raw[o++] = (uint8_t)v;
    }
    int px = FA_W * FA_H;
    uint8_t *rgba = SDL_malloc((size_t)px * 4);
    for (int i = 0; i < px; i++) {
        uint8_t nib = (i & 1) ? (raw[i / 2] >> 4) : (raw[i / 2] & 15);
        uint8_t cov = (uint8_t)(nib * 17);
        rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = cov;
    }

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FA_W, FA_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    SDL_free(raw);
    SDL_free(rgba);

    prog = gl_program(VS, FS);
    u_mvp = glGetUniformLocation(prog, "u_mvp");
    u_tint = glGetUniformLocation(prog, "u_tex");
    glGenBuffers(1, &vbo);
}

static int glyph_of(unsigned cp) {
    if (cp < 128) return idx_of[cp];
    for (int g = 95; g < FA_COUNT; g++)
        if (FA_CHARS[g] == cp) return g;
    return -1;
}

// Decode one UTF-8 sequence; *s advances past it.
static unsigned next_cp(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    unsigned c = *p++;
    if (c >= 0xF0) { c = ((c & 7) << 18) | ((*p++ & 63) << 12); c |= ((*p++ & 63) << 6); c |= (*p++ & 63); }
    else if (c >= 0xE0) { c = ((c & 15) << 12) | ((*p++ & 63) << 6); c |= (*p++ & 63); }
    else if (c >= 0xC0) { c = ((c & 31) << 6) | (*p++ & 63); }
    *s = (const char *)p;
    return c;
}

float font_measure(const char *utf8, float size) {
    float s = size / 28.0f, w = 0;
    const char *p = utf8;
    while (*p) {
        int g = glyph_of(next_cp(&p));
        if (g >= 0) w += FA_ADV[g] * s;
        else w += FA_ADV[idx_of['?']] * s;
    }
    return w;
}

float font_draw(const char *utf8, float x, float y, float size, Vec4 color, int bold) {
    float s = size / 28.0f;
    float scale = bold ? 1.0f + s * 0.4f : 1.0f; // fake-bold: extra pass, slightly wider
    int max_g = 512;
    float *verts = SDL_malloc((size_t)max_g * 6 * 8 * sizeof(float));
    int n = 0;

    for (int pass = 0; pass < (bold ? 2 : 1); pass++) {
        float pen = x + pass * s * 0.7f;
        const char *p = utf8;
        while (*p) {
            unsigned cp = next_cp(&p);
            int g = glyph_of(cp);
            if (g < 0) g = idx_of['?'];
            if (g < 0) continue;
            if (n >= max_g) break;
            float x0 = pen, y0 = y - FA_BASE * s;
            float x1 = x0 + FA_CW * s, y1 = y0 + FA_CH * s;
            float u0 = (float)(g % FA_COLS) / FA_COLS;
            float v0 = (float)(g / FA_COLS) / FA_ROWS;
            float u1 = u0 + (float)FA_CW / FA_W;
            float v1 = v0 + (float)FA_CH / FA_H;
            // Two triangles per glyph: x y u v r g b a
            const float quad[6][8] = {
                { x0, y0, u0, v0, color.r, color.g, color.b, color.a },
                { x1, y0, u1, v0, color.r, color.g, color.b, color.a },
                { x1, y1, u1, v1, color.r, color.g, color.b, color.a },
                { x0, y0, u0, v0, color.r, color.g, color.b, color.a },
                { x1, y1, u1, v1, color.r, color.g, color.b, color.a },
                { x0, y1, u0, v1, color.r, color.g, color.b, color.a },
            };
            for (int k = 0; k < 6; k++)
                for (int c = 0; c < 8; c++)
                    verts[n * 48 + k * 8 + c] = quad[k][c];
            n++;
            pen += FA_ADV[g] * s;
        }
        (void)scale;
    }

    if (n) {
        Mat4 m;
        m4_ortho(m, (float)app_w, (float)app_h);
        glUseProgram(prog);
        glUniformMatrix4fv(u_mvp, 1, GL_FALSE, m);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1i(u_tint, 0);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * 48 * sizeof(float)), verts, GL_STREAM_DRAW);
        GLsizei stride = 8 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void *)(2 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void *)(4 * sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, n * 6);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(2);
    }
    SDL_free(verts);
    return font_measure(utf8, size);
}
