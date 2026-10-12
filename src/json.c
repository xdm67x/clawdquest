#include "json.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *s;
    size_t i;
    bool oom;
} P;

static JVal *jnew(JType t) {
    JVal *v = SDL_calloc(1, sizeof(JVal));
    if (!v) return NULL;
    v->t = t;
    return v;
}

void json_free(JVal *v) {
    if (!v) return;
    JVal *c = v->child;
    while (c) {
        JVal *n = c->next;
        json_free(c);
        c = n;
    }
    SDL_free(v->str);
    SDL_free((void *)v->key);
    SDL_free(v);
}

static void skip_ws(P *p) {
    while (p->s[p->i] == ' ' || p->s[p->i] == '\t' || p->s[p->i] == '\n' || p->s[p->i] == '\r')
        p->i++;
}

static JVal *parse_val(P *p);

static char *parse_str_raw(P *p) {
    if (p->s[p->i] != '"') return NULL;
    p->i++;
    size_t cap = 16, n = 0;
    char *out = SDL_malloc(cap);
    if (!out) return NULL;
    while (p->s[p->i] && p->s[p->i] != '"') {
        char c = p->s[p->i++];
        if (c == '\\' && p->s[p->i]) {
            char e = p->s[p->i++];
            if (e == 'n') c = '\n';
            else if (e == 't') c = '\t';
            else if (e == 'r') c = '\r';
            else if (e == 'b') c = '\b';
            else if (e == 'f') c = '\f';
            else if (e == 'u') { // keep it simple: skip 4 hex digits, emit '?'
                for (int k = 0; k < 4 && p->s[p->i]; k++) p->i++;
                c = '?';
            } else c = e;
        }
        if (n + 2 > cap) {
            cap *= 2;
            char *r = SDL_realloc(out, cap);
            if (!r) { SDL_free(out); return NULL; }
            out = r;
        }
        out[n++] = c;
    }
    if (p->s[p->i] == '"') p->i++;
    out[n] = 0;
    return out;
}

static JVal *parse_val(P *p) {
    skip_ws(p);
    char c = p->s[p->i];
    if (c == '{' || c == '[') {
        JVal *v = jnew(c == '{' ? J_OBJ : J_ARR);
        if (!v) { p->oom = true; return NULL; }
        p->i++;
        JVal **tail = &v->child;
        skip_ws(p);
        if (p->s[p->i] == (c == '{' ? '}' : ']')) { p->i++; return v; }
        for (;;) {
            skip_ws(p);
            const char *key = NULL;
            if (c == '{') {
                char *k = parse_str_raw(p);
                if (!k) { p->oom = true; return v; }
                skip_ws(p);
                if (p->s[p->i] == ':') p->i++;
                key = k;
            }
            JVal *e = parse_val(p);
            if (!e) { SDL_free((void *)key); p->oom = true; return v; }
            e->key = key;
            *tail = e;
            tail = &e->next;
            skip_ws(p);
            if (p->s[p->i] == ',') { p->i++; continue; }
            if (p->s[p->i] == '}' || p->s[p->i] == ']') { p->i++; return v; }
            if (!p->s[p->i]) return v; // malformed; keep what we have
            p->i++;
        }
    }
    if (c == '"') {
        JVal *v = jnew(J_STR);
        if (!v) { p->oom = true; return NULL; }
        v->str = parse_str_raw(p);
        if (!v->str) { p->oom = true; return NULL; }
        return v;
    }
    if (c == 't' || c == 'f') {
        bool b = c == 't';
        JVal *v = jnew(J_BOOL);
        if (!v) { p->oom = true; return NULL; }
        v->num = b;
        while (p->s[p->i] && p->s[p->i] != ',' && p->s[p->i] != '}' && p->s[p->i] != ']' &&
               p->s[p->i] != ' ' && p->s[p->i] != '\n')
            p->i++;
        return v;
    }
    if (c == 'n') {
        while (p->s[p->i] && p->s[p->i] != ',' && p->s[p->i] != '}' && p->s[p->i] != ']')
            p->i++;
        return jnew(J_NULL);
    }
    {
        char *end = NULL;
        double d = strtod(p->s + p->i, &end);
        if (end == p->s + p->i) { p->oom = true; return NULL; }
        p->i = (size_t)(end - p->s);
        JVal *v = jnew(J_NUM);
        if (!v) { p->oom = true; return NULL; }
        v->num = d;
        return v;
    }
}

JVal *json_parse(const char *s) {
    P p = { s, 0, false };
    JVal *v = parse_val(&p);
    if (p.oom) {
        json_free(v);
        return NULL;
    }
    return v;
}

JVal *json_get(const JVal *obj, const char *key) {
    if (!obj || obj->t != J_OBJ) return NULL;
    for (JVal *c = obj->child; c; c = c->next)
        if (c->key && strcmp(c->key, key) == 0) return c;
    return NULL;
}

// --- builders -----------------------------------------------------------------

JVal *jnew_(JType t) { return jnew(t); }

void jadd_(JVal *parent, JVal *child) {
    if (!parent || !child) return;
    if (!parent->child) { parent->child = child; return; }
    JVal *c = parent->child;
    while (c->next) c = c->next;
    c->next = child;
}

void jkey_(JVal *v, const char *key) {
    if (!v) return;
    SDL_free((void *)v->key);
    v->key = SDL_strdup(key);
}

void jnum_(JVal *obj, const char *key, double v) {
    JVal *c = jnew(J_NUM);
    if (!c) return;
    c->num = v;
    c->key = SDL_strdup(key);
    jadd_(obj, c);
}

void jstr_(JVal *obj, const char *key, const char *s) {
    JVal *c = jnew(J_STR);
    if (!c) return;
    c->str = SDL_strdup(s);
    c->key = SDL_strdup(key);
    jadd_(obj, c);
}

// --- writer ----------------------------------------------------------------

typedef struct {
    char *buf;
    size_t n, cap;
    bool oom;
} W;

static void wput(W *w, char c) {
    if (w->n + 2 > w->cap) {
        size_t nc = w->cap ? w->cap * 2 : 512;
        char *r = SDL_realloc(w->buf, nc);
        if (!r) { w->oom = true; return; }
        w->buf = r;
        w->cap = nc;
    }
    w->buf[w->n++] = c;
    w->buf[w->n] = 0;
}

static void wputs(W *w, const char *s) {
    while (*s) wput(w, *s++);
}

static void write_val(W *w, const JVal *v) {
    if (w->oom || !v) return;
    switch (v->t) {
        case J_NULL: wputs(w, "null"); break;
        case J_BOOL: wputs(w, v->num ? "true" : "false"); break;
        case J_NUM: {
            char tmp[64];
            snprintf(tmp, sizeof tmp, "%g", v->num);
            wputs(w, tmp);
            break;
        }
        case J_STR: {
            wput(w, '"');
            for (const char *s = v->str ? v->str : ""; *s; s++) {
                unsigned char c = (unsigned char)*s;
                if (c == '"' || c == '\\') { wput(w, '\\'); wput(w, (char)c); }
                else if (c == '\n') wputs(w, "\\n");
                else if (c == '\t') wputs(w, "\\t");
                else if (c < 0x20) { char t[8]; snprintf(t, sizeof t, "\\u%04x", c); wputs(w, t); }
                else wput(w, (char)c);
            }
            wput(w, '"');
            break;
        }
        case J_ARR:
        case J_OBJ: {
            wput(w, v->t == J_ARR ? '[' : '{');
            int first = 1;
            for (const JVal *c = v->child; c; c = c->next) {
                if (!first) wput(w, ',');
                first = 0;
                if (v->t == J_OBJ) {
                    wput(w, '"');
                    wputs(w, c->key ? c->key : "");
                    wputs(w, "\":");
                }
                write_val(w, c);
                if (w->oom) return;
            }
            wput(w, v->t == J_ARR ? ']' : '}');
            break;
        }
    }
}

char *json_write(const JVal *v) {
    W w = { NULL, 0, 0, false };
    write_val(&w, v);
    if (w.oom) { SDL_free(w.buf); return NULL; }
    if (!w.buf) return SDL_strdup("");
    return w.buf;
}
