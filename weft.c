#include "weft.h"
#include <stdint.h>
#include <stdlib.h>

typedef struct PInst {
    void (*fn)(const struct PInst*, int i, int n, const char* v, char* r, void* ptr[]);
    int x,y,z,w;
    int imm;
    int unused;
} PInst;

typedef struct {
    void (*fn         )(const PInst*, int i, int n, const char* v, char* r, void* ptr[]);
    void (*fn_and_done)(const PInst*, int i, int n, const char* v, char* r, void* ptr[]);
    enum { MATH, SPLAT, UNIFORM, LOAD, STORE } kind;
    int  slots;
    int  x,y,z,w;
    int  imm;
    int  unused;
} BInst;

typedef struct weft_Builder {
    BInst* inst;
    int    insts;
    int    unused;
} Builder;

typedef struct weft_Program {
    PInst* inst;
    int    slots;
    int    unused;
} Program;


Builder* weft_builder(void) {
    Builder* b = malloc(sizeof(*b));
    return b;
}

Program* weft_compile(Builder* b) {
    free(b->inst);
    free(b);

    Program* p = malloc(sizeof(*p));
    return p;
}

void weft_run(const weft_Program* p, int n, void* const ptr[]) {
    (void)p;
    (void)n;
    (void)ptr;
}

void weft_drop(weft_Program* p) {
    free(p->inst);
    free(p);
}
