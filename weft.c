#include "weft.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define N 8

typedef struct { int id; } V0;
typedef weft_V8            V8;
typedef weft_V16           V16;
typedef weft_V32           V32;

typedef struct PInst {
    void (*fn)(const struct PInst*, int, unsigned, const void*, void*, void* const ptr[]);
    int x,y,z,w;  // v+{x,y,z,w} gives the start of value {x,y,z,w}.
    int imm;
    int slot;
} PInst;

typedef struct {
    enum { MATH, SPLAT, UNIFORM, LOAD, STORE, DONE } kind;
    int  slots;
    void (*fn         )(const PInst*, int, unsigned, const void*, void*, void* const ptr[]);
    void (*fn_and_done)(const PInst*, int, unsigned, const void*, void*, void* const ptr[]);
    int  x,y,z,w;  // 1-indexed; {x,y,z,w}==0 indicates an unused argument.
    int  imm;
    int  unused;
} BInst;

typedef struct weft_Builder {
    BInst* inst;
    int    insts;
    int    unused;
} Builder;

typedef struct weft_Program {
    int   slots;
    int   unused;
    PInst inst[];
} Program;


Builder* weft_builder(void) {
    Builder* b = malloc(sizeof(*b));
    b->inst  = NULL;
    b->insts = 0;
    return b;
}

static int inst_(Builder* b, BInst inst) {
    if ((b->insts & (b->insts-1)) == 0) {
        b->inst = realloc(b->inst, (size_t)(b->insts ? 2*b->insts : 1) * sizeof(*b->inst));
    }
    b->inst[b->insts++] = inst;
    return b->insts;  // BInst IDs are 1-indexed.
}
#define inst(b,kind,bits,fn,...) (V##bits){inst_(b, (BInst){kind,bits/8,fn, __VA_ARGS__})}

static void done(const PInst* program, int off, unsigned tail,
                 const void* v, void* r, void* const ptr[]) {
    (void)program;
    (void)off;
    (void)tail;
    (void)v;
    (void)r;
    (void)ptr;
}

Program* weft_compile(Builder* b) {
    (void)inst(b, DONE,0,done, .imm=0);

    Program* p = malloc(sizeof(*p) + (size_t)b->insts * sizeof(*p->inst));
    p->slots = 0;

    for (int i = 0; i < b->insts; i++) {
        BInst inst = b->inst[i];

        p->inst[i].fn = (i == b->insts-1 && inst.fn_and_done) ? inst.fn_and_done
                                                              : inst.fn;

        p->inst[i].x    = inst.x ? p->inst[inst.x-1].slot * N : 0;
        p->inst[i].y    = inst.y ? p->inst[inst.y-1].slot * N : 0;
        p->inst[i].z    = inst.z ? p->inst[inst.z-1].slot * N : 0;
        p->inst[i].w    = inst.w ? p->inst[inst.w-1].slot * N : 0;
        p->inst[i].imm  = inst.imm;
        p->inst[i].slot = p->slots;

        p->slots += inst.slots;
    }

    free(b->inst);
    free(b);

    return p;
}


void weft_run(const weft_Program* p, int n, void* const ptr[]) {
    void* v = malloc(N * (size_t)p->slots);

    const PInst* inst = p->inst;
    for (int off = 0; off+N <= n; off += N) {
        inst->fn(inst,off,0,v,v,ptr);
    }
    for (unsigned tail = (unsigned)(n - n/N*N); tail; ) {
        inst->fn(inst,n/N*N,tail,v,v,ptr);
        break;
    }

    free(v);
}

#define stage(name) static void name(const PInst* I, int off, unsigned tail, \
                                     const void* restrict V, void* restrict R, void* const ptr[])

#define each for (int i = 0; i < N; i++)
#define next(R) I[1].fn(I+1,off,tail,V,R,ptr); return
#define v(arg) (const void*)( (const char*)V + arg )

stage(splat8 ) { int8_t*  r = R; each *r++ = (int8_t )I->imm; next(r); }
stage(splat16) { int16_t* r = R; each *r++ = (int16_t)I->imm; next(r); }
stage(splat32) { int32_t* r = R; each *r++ = (int32_t)I->imm; next(r); }

V8  weft_splat8 (Builder* b, int bits) { return inst(b, SPLAT,8 ,splat8 , .imm=bits); }
V16 weft_splat16(Builder* b, int bits) { return inst(b, SPLAT,16,splat16, .imm=bits); }
V32 weft_splat32(Builder* b, int bits) { return inst(b, SPLAT,32,splat32, .imm=bits); }

stage(load8) {
    int8_t* r = R;
    tail ? memcpy(r, (const int8_t*)ptr[I->imm] + off, 1*tail)
         : memcpy(r, (const int8_t*)ptr[I->imm] + off, 1*N);
    next(r+N);
}
stage(load16) {
    int16_t* r = R;
    tail ? memcpy(r, (const int16_t*)ptr[I->imm] + off, 2*tail)
         : memcpy(r, (const int16_t*)ptr[I->imm] + off, 2*N);
    next(r+N);
}
stage(load32) {
    int32_t* r = R;
    tail ? memcpy(r, (const int32_t*)ptr[I->imm] + off, 4*tail)
         : memcpy(r, (const int32_t*)ptr[I->imm] + off, 4*N);
    next(r+N);
}

V8  weft_load8 (Builder* b, int ptr) { return inst(b, LOAD,8 ,load8 , .imm=ptr); }
V16 weft_load16(Builder* b, int ptr) { return inst(b, LOAD,16,load16, .imm=ptr); }
V32 weft_load32(Builder* b, int ptr) { return inst(b, LOAD,32,load32, .imm=ptr); }

stage(store8) {
    tail ? memcpy((int8_t*)ptr[I->imm] + off, v(I->x), 1*tail)
         : memcpy((int8_t*)ptr[I->imm] + off, v(I->x), 1*N);
    next(R);
}
stage(store16) {
    tail ? memcpy((int16_t*)ptr[I->imm] + off, v(I->x), 2*tail)
         : memcpy((int16_t*)ptr[I->imm] + off, v(I->x), 2*N);
    next(R);
}
stage(store32) {
    tail ? memcpy((int32_t*)ptr[I->imm] + off, v(I->x), 4*tail)
         : memcpy((int32_t*)ptr[I->imm] + off, v(I->x), 4*N);
    next(R);
}

void weft_store8 (Builder* b, int ptr, V8  x) { inst(b, STORE,0,store8 , .x=x.id, .imm=ptr); }
void weft_store16(Builder* b, int ptr, V16 x) { inst(b, STORE,0,store16, .x=x.id, .imm=ptr); }
void weft_store32(Builder* b, int ptr, V32 x) { inst(b, STORE,0,store32, .x=x.id, .imm=ptr); }

