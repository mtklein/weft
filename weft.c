#include "weft.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define N 8

typedef struct { int id; } V0;
typedef weft_V8            V8;
typedef weft_V16           V16;
typedef weft_V32           V32;

typedef struct PInst {
    void (*fn)(const struct PInst*, int, unsigned, void*, void*, void* const ptr[]);
    int x,y,z,w;  // v+{x,y,z,w} gives the start of value {x,y,z,w}.
    int imm;
    int slot;
} PInst;

typedef struct {
    enum { MATH, SPLAT, UNIFORM, LOAD, STORE, DONE } kind;
    int  slots;
    void (*fn         )(const PInst*, int, unsigned, void*, void*, void* const ptr[]);
    void (*fn_and_done)(const PInst*, int, unsigned, void*, void*, void* const ptr[]);
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
                 void* v, void* r, void* const ptr[]) {
    (void)program;
    (void)off;
    (void)tail;
    (void)v;
    (void)r;
    (void)ptr;
}

Program* weft_compile(Builder* b) {
    if (b->insts == 0 || !b->inst[b->insts-1].fn_and_done) {
        (void)inst(b, DONE,0,done, .imm=0);
    }

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
                                     void* restrict V, void* restrict R, void* const ptr[])

#define each for (int i = 0; i < N; i++)
#define next(R) I[1].fn(I+1,off,tail,V,R,ptr); return
#define v(arg) (void*)( (char*)V + arg )

stage(splat_8 ) { int8_t  *r=R; each r[i] = (int8_t )I->imm; next(r+N); }
stage(splat_16) { int16_t *r=R; each r[i] = (int16_t)I->imm; next(r+N); }
stage(splat_32) { int32_t *r=R; each r[i] = (int32_t)I->imm; next(r+N); }

V8  weft_splat_8 (Builder* b, int bits) { return inst(b, SPLAT,8 ,splat_8 , .imm=bits); }
V16 weft_splat_16(Builder* b, int bits) { return inst(b, SPLAT,16,splat_16, .imm=bits); }
V32 weft_splat_32(Builder* b, int bits) { return inst(b, SPLAT,32,splat_32, .imm=bits); }

stage(uniform_8)  { int8_t  *r=R, u=*(const int8_t* )ptr[I->imm]; each r[i] = u; next(r+N); }
stage(uniform_16) { int16_t *r=R, u=*(const int16_t*)ptr[I->imm]; each r[i] = u; next(r+N); }
stage(uniform_32) { int32_t *r=R, u=*(const int32_t*)ptr[I->imm]; each r[i] = u; next(r+N); }

V8  weft_uniform_8 (Builder* b, int ptr) { return inst(b, UNIFORM,8 ,uniform_8 , .imm=ptr); }
V16 weft_uniform_16(Builder* b, int ptr) { return inst(b, UNIFORM,16,uniform_16, .imm=ptr); }
V32 weft_uniform_32(Builder* b, int ptr) { return inst(b, UNIFORM,32,uniform_32, .imm=ptr); }

stage(load_8) {
    int8_t* r = R;
    tail ? memcpy(r, (const int8_t*)ptr[I->imm] + off, 1*tail)
         : memcpy(r, (const int8_t*)ptr[I->imm] + off, 1*N);
    next(r+N);
}
stage(load_16) {
    int16_t* r = R;
    tail ? memcpy(r, (const int16_t*)ptr[I->imm] + off, 2*tail)
         : memcpy(r, (const int16_t*)ptr[I->imm] + off, 2*N);
    next(r+N);
}
stage(load_32) {
    int32_t* r = R;
    tail ? memcpy(r, (const int32_t*)ptr[I->imm] + off, 4*tail)
         : memcpy(r, (const int32_t*)ptr[I->imm] + off, 4*N);
    next(r+N);
}

V8  weft_load_8 (Builder* b, int ptr) { return inst(b, LOAD,8 ,load_8 , .imm=ptr); }
V16 weft_load_16(Builder* b, int ptr) { return inst(b, LOAD,16,load_16, .imm=ptr); }
V32 weft_load_32(Builder* b, int ptr) { return inst(b, LOAD,32,load_32, .imm=ptr); }

stage(store_8) {
    tail ? memcpy((int8_t*)ptr[I->imm] + off, v(I->x), 1*tail)
         : memcpy((int8_t*)ptr[I->imm] + off, v(I->x), 1*N);
    next(R);
}
stage(store_8_and_done) {
    tail ? memcpy((int8_t*)ptr[I->imm] + off, v(I->x), 1*tail)
         : memcpy((int8_t*)ptr[I->imm] + off, v(I->x), 1*N);
    (void)R;
}
stage(store_16) {
    tail ? memcpy((int16_t*)ptr[I->imm] + off, v(I->x), 2*tail)
         : memcpy((int16_t*)ptr[I->imm] + off, v(I->x), 2*N);
    next(R);
}
stage(store_16_and_done) {
    tail ? memcpy((int16_t*)ptr[I->imm] + off, v(I->x), 2*tail)
         : memcpy((int16_t*)ptr[I->imm] + off, v(I->x), 2*N);
    (void)R;
}
stage(store_32) {
    tail ? memcpy((int32_t*)ptr[I->imm] + off, v(I->x), 4*tail)
         : memcpy((int32_t*)ptr[I->imm] + off, v(I->x), 4*N);
    next(R);
}
stage(store_32_and_done) {
    tail ? memcpy((int32_t*)ptr[I->imm] + off, v(I->x), 4*tail)
         : memcpy((int32_t*)ptr[I->imm] + off, v(I->x), 4*N);
    (void)R;
}

void weft_store_8(Builder* b, int ptr, V8  x) {
    inst(b, STORE,0,store_8 , .fn_and_done=store_8_and_done , .x=x.id, .imm=ptr);
}
void weft_store_16(Builder* b, int ptr, V16 x) {
    inst(b, STORE,0,store_16, .fn_and_done=store_16_and_done, .x=x.id, .imm=ptr);
}
void weft_store_32(Builder* b, int ptr, V32 x) {
    inst(b, STORE,0,store_32, .fn_and_done=store_32_and_done, .x=x.id, .imm=ptr);
}

static bool is_splat(Builder* b, int id, int* imm) {
    *imm = b->inst[id-1].imm;
    return b->inst[id-1].kind == SPLAT;
}

stage( add_f32) { float *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]+y[i];   next(r+N); }
stage( sub_f32) { float *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]-y[i];   next(r+N); }
stage( mul_f32) { float *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]*y[i];   next(r+N); }
stage( div_f32) { float *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]/y[i];   next(r+N); }
stage(sqrt_f32) { float *r=R, *x=v(I->x);             each r[i] = sqrtf(x[i]); next(r+N); }

V32 weft_add_f32 (Builder* b, V32 x, V32 y) {
    for (int imm; is_splat(b,y.id,&imm) && (imm == 0 || imm == (int)0x80000000);) { return x; }
    for (int imm; is_splat(b,x.id,&imm) && (imm == 0 || imm == (int)0x80000000);) { return y; }
    return inst(b, MATH,32, add_f32, .x=x.id, .y=y.id);
}
V32 weft_sub_f32 (Builder* b, V32 x, V32 y) {
    for (int imm; is_splat(b,y.id,&imm) && (imm == 0 || imm == (int)0x80000000);) { return x; }
    return inst(b, MATH,32, sub_f32, .x=x.id, .y=y.id);
}
V32 weft_mul_f32 (Builder* b, V32 x, V32 y) {
    // Note: x*0 isn't 0 when x=NaN.
    for (int imm; is_splat(b,y.id,&imm) && imm == 0x3f800000;) { return x; }
    for (int imm; is_splat(b,x.id,&imm) && imm == 0x3f800000;) { return y; }
    return inst(b, MATH,32, mul_f32, .x=x.id, .y=y.id);
}
V32 weft_div_f32 (Builder* b, V32 x, V32 y) {
    for (int imm; is_splat(b,y.id,&imm) && imm == 0x3f800000;) { return x; }
    return inst(b, MATH,32, div_f32, .x=x.id, .y=y.id);
}
V32 weft_sqrt_f32(Builder* b, V32 x) {
    return inst(b, MATH,32,sqrt_f32, .x=x.id);
}

stage(add_i32) { int32_t *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]+y[i]; next(r+N); }
stage(sub_i32) { int32_t *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]-y[i]; next(r+N); }
stage(mul_i32) { int32_t *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]*y[i]; next(r+N); }

V32 weft_add_i32(Builder* b, V32 x, V32 y) {
    for (int imm; is_splat(b,y.id,&imm) && imm == 0;) { return x; }
    for (int imm; is_splat(b,x.id,&imm) && imm == 0;) { return y; }
    return inst(b, MATH,32,add_i32, .x=x.id, .y=y.id);
}
V32 weft_sub_i32(Builder* b, V32 x, V32 y) {
    for (int imm; is_splat(b,y.id,&imm) && imm == 0;) { return x; }
    return inst(b, MATH,32,sub_i32, .x=x.id, .y=y.id);
}
V32 weft_mul_i32(Builder* b, V32 x, V32 y) {
    for (int imm; is_splat(b,y.id,&imm) && imm == 0;) { return y; }
    for (int imm; is_splat(b,x.id,&imm) && imm == 0;) { return x; }
    for (int imm; is_splat(b,y.id,&imm) && imm == 1;) { return x; }
    for (int imm; is_splat(b,x.id,&imm) && imm == 1;) { return y; }
    return inst(b, MATH,32,mul_i32, .x=x.id, .y=y.id);
}

stage(shlv_i32) {  int32_t *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]<<y[i]; next(r+N); }
stage(shrv_s32) {  int32_t *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]>>y[i]; next(r+N); }
stage(shrv_u32) { uint32_t *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]>>y[i]; next(r+N); }

stage(shli_i32) {  int32_t *r=R, *x=v(I->x); each r[i] = x[i]<<I->imm; next(r+N); }
stage(shri_s32) {  int32_t *r=R, *x=v(I->x); each r[i] = x[i]>>I->imm; next(r+N); }
stage(shri_u32) { uint32_t *r=R, *x=v(I->x); each r[i] = x[i]>>I->imm; next(r+N); }


V32 weft_shl_i32(Builder* b, V32 x, V32 y) {
    for (int imm; is_splat(b,y.id,&imm);) {
        if (imm == 0) { return x; }
        return inst(b, MATH,32,shli_i32, .x=x.id, .imm=imm);
    }
    return inst(b, MATH,32,shlv_i32, .x=x.id, .y=y.id);
}
V32 weft_shr_s32(Builder* b, V32 x, V32 y) {
    for (int imm; is_splat(b,y.id,&imm);) {
        if (imm == 0) { return x; }
        return inst(b, MATH,32,shri_s32, .x=x.id, .imm=imm);
    }
    return inst(b, MATH,32,shrv_s32, .x=x.id, .y=y.id);
}
V32 weft_shr_u32(Builder* b, V32 x, V32 y) {
    for (int imm; is_splat(b,y.id,&imm);) {
        if (imm == 0) { return x; }
        return inst(b, MATH,32,shri_u32, .x=x.id, .imm=imm);
    }
    return inst(b, MATH,32,shrv_u32, .x=x.id, .y=y.id);
}

stage(and_32) { int32_t *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]&y[i]; next(r+N); }
stage( or_32) { int32_t *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]|y[i]; next(r+N); }
stage(xor_32) { int32_t *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]^y[i]; next(r+N); }
stage(sel_32) {
    int32_t *r=R, *x=v(I->x), *y=v(I->y), *z=v(I->z);
    each r[i] = ( x[i] & y[i])
              | (~x[i] & z[i]);
    next(r+N);
}

V32 weft_and_32(Builder* b, V32 x, V32 y) {
    if (x.id == y.id) { return x; }
    for (int imm; is_splat(b,y.id,&imm) && imm ==  0;) { return y; }
    for (int imm; is_splat(b,x.id,&imm) && imm ==  0;) { return x; }
    for (int imm; is_splat(b,y.id,&imm) && imm == -1;) { return x; }
    for (int imm; is_splat(b,x.id,&imm) && imm == -1;) { return y; }
    return inst(b, MATH,32,and_32, .x=x.id, .y=y.id);
}
V32 weft_or_32(Builder* b, V32 x, V32 y) {
    if (x.id == y.id) { return x; }
    for (int imm; is_splat(b,y.id,&imm) && imm ==  0;) { return x; }
    for (int imm; is_splat(b,x.id,&imm) && imm ==  0;) { return y; }
    for (int imm; is_splat(b,y.id,&imm) && imm == -1;) { return y; }
    for (int imm; is_splat(b,x.id,&imm) && imm == -1;) { return x; }
    return inst(b, MATH,32, or_32, .x=x.id, .y=y.id);
}
V32 weft_xor_32(Builder* b, V32 x, V32 y) {
    if (x.id == y.id) { return weft_splat_32(b,0); }
    for (int imm; is_splat(b,y.id,&imm) && imm ==  0;) { return x; }
    for (int imm; is_splat(b,x.id,&imm) && imm ==  0;) { return y; }
    return inst(b, MATH,32,xor_32, .x=x.id, .y=y.id);
}
V32 weft_sel_32(Builder* b, V32 x, V32 y, V32 z) {
    for (int imm; is_splat(b,x.id,&imm) && imm ==  0;) { return z; }
    for (int imm; is_splat(b,x.id,&imm) && imm == -1;) { return y; }
    for (int imm; is_splat(b,z.id,&imm) && imm ==  0;) { return weft_and_32(b,x,y); }
    return inst(b, MATH,32,sel_32, .x=x.id, .y=y.id, .z=z.id);
}
