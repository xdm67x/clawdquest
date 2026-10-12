// ---------------------------------------------------------------------------
// Minimal column-major 4x4 matrix and small vector helpers.
// Enough for the Quest Log renderer: perspective, ortho, lookAt, transforms.
// ---------------------------------------------------------------------------
#pragma once

#include <math.h>
#include <string.h>

typedef float Mat4[16];
typedef struct { float x, y, z; } Vec3;
typedef struct { float r, g, b, a; } Vec4;
typedef struct { float x, y, w, h; } Rect;

static inline void m4_identity(Mat4 m) {
    memset(m, 0, sizeof(Mat4));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// Right-handed, depth [0, 1] (matches OpenGL clip space used by GL 3.2 core).
static inline void m4_perspective(Mat4 m, float fovy, float aspect, float zn, float zf) {
    float f = 1.0f / tanf(fovy * 0.5f);
    memset(m, 0, sizeof(Mat4));
    m[0] = f / aspect;
    m[5] = f;
    m[10] = zf / (zf - zn);
    m[11] = -1.0f;
    m[14] = zf * zn / (zn - zf);
}

// Pixel-space orthographic projection: y down, (0,0) top-left.
static inline void m4_ortho(Mat4 m, float w, float h) {
    memset(m, 0, sizeof(Mat4));
    m[0] = 2.0f / w;
    m[5] = -2.0f / h;
    m[10] = -1.0f;
    m[12] = -1.0f;
    m[13] = 1.0f;
    m[15] = 1.0f;
}

static inline void m4_lookAt(Mat4 m, Vec3 eye, Vec3 at, Vec3 up) {
    Vec3 f = { at.x - eye.x, at.y - eye.y, at.z - eye.z };
    float fl = 1.0f / sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
    f.x *= fl; f.y *= fl; f.z *= fl;
    Vec3 s = { f.y * up.z - f.z * up.y, f.z * up.x - f.x * up.z, f.x * up.y - f.y * up.x };
    float sl = 1.0f / sqrtf(s.x * s.x + s.y * s.y + s.z * s.z);
    s.x *= sl; s.y *= sl; s.z *= sl;
    Vec3 u = { s.y * f.z - s.z * f.y, s.z * f.x - s.x * f.z, s.x * f.y - s.y * f.x };
    m[0] = s.x; m[4] = s.y; m[8] = s.z;   m[12] = -(s.x * eye.x + s.y * eye.y + s.z * eye.z);
    m[1] = u.x; m[5] = u.y; m[9] = u.z;   m[13] = -(u.x * eye.x + u.y * eye.y + u.z * eye.z);
    m[2] = -f.x; m[6] = -f.y; m[10] = -f.z; m[14] = f.x * eye.x + f.y * eye.y + f.z * eye.z;
    m[3] = 0; m[7] = 0; m[11] = 0; m[15] = 1.0f;
}

// out = a * b (transform by b first, then a).
static inline void m4_mul(Mat4 out, const Mat4 a, const Mat4 b) {
    Mat4 res;
    for (int c = 0; c < 4; c++)
        for (int i = 0; i < 4; i++)
            res[c * 4 + i] = a[i] * b[c * 4] + a[4 + i] * b[c * 4 + 1]
                           + a[8 + i] * b[c * 4 + 2] + a[12 + i] * b[c * 4 + 3];
    memcpy(out, res, sizeof(Mat4));
}

static inline void m4_translate(Mat4 m, float x, float y, float z) {
    m4_identity(m);
    m[12] = x; m[13] = y; m[14] = z;
}

static inline void m4_rotX(Mat4 m, float a) {
    m4_identity(m);
    m[5] = cosf(a); m[6] = sinf(a); m[9] = -sinf(a); m[10] = cosf(a);
}

static inline void m4_rotY(Mat4 m, float a) {
    m4_identity(m);
    m[0] = cosf(a); m[2] = -sinf(a); m[8] = sinf(a); m[10] = cosf(a);
}

static inline void m4_rotZ(Mat4 m, float a) {
    m4_identity(m);
    m[0] = cosf(a); m[1] = sinf(a); m[4] = -sinf(a); m[5] = cosf(a);
}

static inline void m4_scale(Mat4 m, float x, float y, float z) {
    m4_identity(m);
    m[0] = x; m[5] = y; m[10] = z;
}

static inline float v3_len(Vec3 v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }

// Pack 0xRRGGBB into a Vec3/Vec4.
static inline Vec3 hex3(int c) {
    return (Vec3){ ((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f };
}
static inline Vec4 hex4(int c, float a) { Vec3 v = hex3(c); return (Vec4){ v.x, v.y, v.z, a }; }

// HSV for confetti hues.
static inline Vec3 hsv(float h, float s, float v) {
    float i = floorf(h * 6.0f);
    float f = h * 6.0f - i;
    float p = v * (1.0f - s), q = v * (1.0f - f * s), t = v * (1.0f - (1.0f - f) * s);
    switch ((int)i % 6) {
        case 0: return (Vec3){ v, t, p };
        case 1: return (Vec3){ q, v, p };
        case 2: return (Vec3){ p, v, t };
        case 3: return (Vec3){ p, q, v };
        case 4: return (Vec3){ t, p, v };
        default: return (Vec3){ v, p, q };
    }
}
