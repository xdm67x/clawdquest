// ---------------------------------------------------------------------------
// Tiny JSON reader/writer — just enough for the Quest Log save file.
// DOM-style parser: each value is one malloc'd node; keys are attached to
// object children, siblings form a linked list.
// ---------------------------------------------------------------------------
#pragma once

typedef struct JVal JVal;

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JType;

struct JVal {
    JType t;
    double num;      // J_NUM / J_BOOL
    char *str;       // J_STR
    const char *key; // set on children of J_OBJ
    JVal *child;     // first element of J_ARR / J_OBJ
    JVal *next;      // next sibling
};

JVal *json_parse(const char *s);
void json_free(JVal *v);
JVal *json_get(const JVal *obj, const char *key);

// Builders for the save writer.
JVal *jnew_(JType t);                       // new node
void jadd_(JVal *parent, JVal *child);      // append child (steals nothing)
void jkey_(JVal *v, const char *key);       // strdup a key onto the node
void jnum_(JVal *obj, const char *key, double v);       // obj["key"] = number
void jstr_(JVal *obj, const char *key, const char *s);  // obj["key"] = string

// Writer: appends into a caller-growable buffer (SDL_ functions for memory).
char *json_write(const JVal *v); // returns malloc'd string, caller frees
