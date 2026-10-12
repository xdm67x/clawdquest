// ---------------------------------------------------------------------------
// Shader helpers shared by the 3D scene and the 2D HUD.
// ---------------------------------------------------------------------------
#pragma once

#include <stdio.h>
#include <string.h>

// Core-profile GL 3.2: macOS exposes it via gl3.h, elsewhere SDL's header
// (which includes the system GL) does the job.
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

static inline GLuint gl_compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        SDL_Log("shader compile failed:\n%s\n%s", log, src);
        return 0;
    }
    return s;
}

static inline GLuint gl_program(const char *vs, const char *fs) {
    GLuint p = glCreateProgram();
    GLuint v = gl_compile(GL_VERTEX_SHADER, vs);
    GLuint f = gl_compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        SDL_Log("program link failed:\n%s", log);
        return 0;
    }
    return p;
}
