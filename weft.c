#include "weft.h"
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define N 8

#if !defined(__has_attribute)
    #define  __has_attribute(x) 0
#endif

typedef struct { int id; } V0;  // Not really used or sensical; just enables inst() macro.
typedef weft_V8            V8;
typedef weft_V16           V16;
typedef weft_V32           V32;
typedef struct PInst       PInst;

// BInst/Builder notes:
//   - Value IDs x,y,z,w,id,lookup() are 1-indexed, with 0 meaning unused, N/A, etc.
//   - The inst array has insts active elements and grows by doubling, 0->1->2->4->8->...
//   - The cse table has insts id!=0 slots, and grows when 3/4 full, with capacity cse_cap.
//   - insts has already been incremented by 1 before insert()/just_insert() are called.

typedef struct {
    enum { MATH, SPLAT, UNIFORM, LOAD, STORE, DONE } kind;
    int  slots;
    void (*fn         )(const PInst*, int, unsigned, void*, void*, void* const ptr[]);
    void (*fn_and_done)(const PInst*, int, unsigned, void*, void*, void* const ptr[]);
    int  x,y,z,w;
    int  imm;
    int  unused;
} BInst;

typedef struct weft_Builder {
    BInst                 *inst;
    struct {int id,hash;} *cse;
    int                    insts;
    int                    cse_cap;
} Builder;

Builder* weft_builder(void) {
    return calloc(1, sizeof(Builder));
}

#if __has_attribute(no_sanitize)
    __attribute__((no_sanitize("unsigned-integer-overflow")))
#endif
static uint32_t fnv1a(const void* vp, size_t len) {
    uint32_t hash = 0x811c9dc5;
    for (const uint8_t* p = vp; len --> 0;) {
        hash ^= *p++;
        hash *= 0x01000193;
    }
    return hash;
}

static int lookup(Builder* b, int hash, const BInst* inst) {
    int i = hash & (b->cse_cap-1);
    for (int n = b->cse_cap; n --> 0;) {
        if (b->cse[i].id == 0) {
            break;
        }
        if (b->cse[i].hash == hash && !memcmp(inst, b->inst + b->cse[i].id-1, sizeof(*inst))) {
            return b->cse[i].id;
        }
        i = (i+1) & (b->cse_cap-1);
    }
    return 0;
}

static void just_insert(Builder* b, int id, int hash) {
    assert(b->insts <= b->cse_cap);
    int i = hash & (b->cse_cap-1);
    for (int n = b->cse_cap; n --> 0;) {
        if (b->cse[i].id == 0) {
            b->cse[i].id   = id;
            b->cse[i].hash = hash;
            return;
        }
        i = (i+1) & (b->cse_cap-1);
    }
    assert(false);
}

static void insert(Builder* b, int id, int hash) {
    if (b->insts > (b->cse_cap*3)/4) {
        Builder grown = *b;
        grown.cse_cap = b->cse_cap ? b->cse_cap*2 : 1;
        grown.cse     = calloc((size_t)grown.cse_cap, sizeof *grown.cse);

        for (int i = 0; i < b->cse_cap; i++) {
            if (b->cse[i].id) {
                just_insert(&grown, b->cse[i].id, b->cse[i].hash);
            }
        }

        free(b->cse);
        *b = grown;
    }
    just_insert(b,id,hash);
}

static int inst_(Builder* b, BInst inst) {
    int hash = (int)fnv1a(&inst, sizeof(inst));

    for (int id = lookup(b, hash, &inst); id;) {
        return id;
    }

    if ((b->insts & (b->insts-1)) == 0) {
        b->inst = realloc(b->inst, (size_t)(b->insts ? 2*b->insts : 1) * sizeof(*b->inst));
    }
    int id = ++b->insts;
    b->inst[id-1] = inst;
    insert(b,id,hash);
    return id;
}
#define inst(b,kind,bits,fn,...) (V##bits){inst_(b, (BInst){kind,bits/8,fn, __VA_ARGS__})}

// PInst/Program/stage/weft_compile()/weft_run() notes:
//    - only weft_compile() uses PInst's slot field (those bytes would be unused padding anyway).
//    - each stage writes to R (result) and calls next() with R incremented past its writes.
//    - V+x is the start of argument value x, usually via macro v(I->x).  Same for y,z,w.
//    - off tracks weft_run()'s progress from 0 toward n, for offseting varying pointers.
//    - tail==0 when operating on a full N-sized chunk, or tail==k when it's only size k<N.

typedef struct PInst {
    void (*fn)(const PInst*, int, unsigned, void*, void*, void* const ptr[]);
    int x,y,z,w;
    int imm;
    int slot;
} PInst;

typedef struct weft_Program {
    int   slots;
    int   unused;
    PInst inst[];
} Program;

static void done(const PInst* I, int off, unsigned tail,
                 void* V, void* R, void* const ptr[]) {
    (void)I;
    (void)off;
    (void)tail;
    (void)V;
    (void)R;
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
    free(b->cse);
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


static const int f32_n0 = (int)0x80000000,
                 f32_p1 = (int)0x3f800000;

static bool is_splat(Builder* b, int id, int imm) {
    return b->inst[id-1].kind == SPLAT
        && b->inst[id-1].imm  == imm;
}

static void sort_commutative(int* x, int* y) {
    int lo = *x < *y ? *x : *y,
        hi = *x < *y ? *y : *x;
    *x = lo;
    *y = hi;
}

stage(add_f32) { float *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]+y[i]; next(r+N); }
stage(sub_f32) { float *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]-y[i]; next(r+N); }
stage(mul_f32) { float *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]*y[i]; next(r+N); }
stage(div_f32) { float *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]/y[i]; next(r+N); }

V32 weft_add_f32 (Builder* b, V32 x, V32 y) {
    sort_commutative(&x.id, &y.id);
    if (is_splat(b,y.id,      0)) { return x; }
    if (is_splat(b,y.id, f32_n0)) { return x; }
    if (is_splat(b,x.id,      0)) { return y; }
    if (is_splat(b,x.id, f32_n0)) { return y; }
    return inst(b, MATH,32, add_f32, .x=x.id, .y=y.id);
}
V32 weft_sub_f32 (Builder* b, V32 x, V32 y) {
    if (is_splat(b,y.id,      0)) { return x; }
    if (is_splat(b,y.id, f32_n0)) { return x; }
    return inst(b, MATH,32, sub_f32, .x=x.id, .y=y.id);
}
V32 weft_mul_f32 (Builder* b, V32 x, V32 y) {
    sort_commutative(&x.id, &y.id);
    // Note: x*0 isn't 0 when x=NaN.
    if (is_splat(b,y.id, f32_p1)) { return x; }
    if (is_splat(b,x.id, f32_p1)) { return y; }
    return inst(b, MATH,32, mul_f32, .x=x.id, .y=y.id);
}
V32 weft_div_f32 (Builder* b, V32 x, V32 y) {
    if (is_splat(b,y.id, f32_p1)) { return x; }
    return inst(b, MATH,32, div_f32, .x=x.id, .y=y.id);
}

stage( ceil_f32) { float *r=R, *x=v(I->x); each r[i] =  ceilf(x[i]); next(r+N); }
stage(floor_f32) { float *r=R, *x=v(I->x); each r[i] = floorf(x[i]); next(r+N); }
stage( sqrt_f32) { float *r=R, *x=v(I->x); each r[i] =  sqrtf(x[i]); next(r+N); }

V32  weft_ceil_f32(Builder* b, V32 x) { return inst(b, MATH,32, ceil_f32, .x=x.id); }
V32 weft_floor_f32(Builder* b, V32 x) { return inst(b, MATH,32,floor_f32, .x=x.id); }
V32  weft_sqrt_f32(Builder* b, V32 x) { return inst(b, MATH,32, sqrt_f32, .x=x.id); }

#define INT_STAGES(B,ST,UT) \
    stage(shl_i##B) {ST *r=R, *x=v(I->x), *y=v(I->y); each r[i] =(ST)(x[i] << y[i]); next(r+N);} \
    stage(shr_s##B) {ST *r=R, *x=v(I->x), *y=v(I->y); each r[i] =     x[i] >> y[i] ; next(r+N);} \
    stage(shr_u##B) {UT *r=R, *x=v(I->x), *y=v(I->y); each r[i] =     x[i] >> y[i] ; next(r+N);} \
    stage(add_i##B) {UT *r=R, *x=v(I->x), *y=v(I->y); each r[i] =     x[i] +  y[i] ; next(r+N);} \
    stage(sub_i##B) {UT *r=R, *x=v(I->x), *y=v(I->y); each r[i] =     x[i] -  y[i] ; next(r+N);} \
    stage(mul_i##B) {UT *r=R, *x=v(I->x), *y=v(I->y); each r[i] =     x[i] *  y[i] ; next(r+N);} \
    stage(and_ ##B) {UT *r=R, *x=v(I->x), *y=v(I->y); each r[i] =     x[i] &  y[i] ; next(r+N);} \
    stage(bic_ ##B) {UT *r=R, *x=v(I->x), *y=v(I->y); each r[i] =     x[i] & ~y[i] ; next(r+N);} \
    stage( or_ ##B) {UT *r=R, *x=v(I->x), *y=v(I->y); each r[i] =     x[i] |  y[i] ; next(r+N);} \
    stage(xor_ ##B) {UT *r=R, *x=v(I->x), *y=v(I->y); each r[i] =     x[i] ^  y[i] ; next(r+N);} \
    stage(sel_ ##B) {                                \
        UT *r=R, *x=v(I->x), *y=v(I->y), *z=v(I->z); \
        each r[i] = ( x[i] & y[i])                   \
                  | (~x[i] & z[i]);                  \
        next(r+N);                                   \
    }

INT_STAGES( 8, int8_t, uint8_t)
INT_STAGES(16,int16_t,uint16_t)
INT_STAGES(32,int32_t,uint32_t)

V8 weft_add_i8(Builder* b, V8 x, V8 y) {
    sort_commutative(&x.id, &y.id);
    if (is_splat(b,y.id, 0)) { return x; }
    if (is_splat(b,x.id, 0)) { return y; }
    return inst(b, MATH,8,add_i8, .x=x.id, .y=y.id);
}
V8 weft_sub_i8(Builder* b, V8 x, V8 y) {
    if (is_splat(b,y.id, 0)) { return x; }
    return inst(b, MATH,8,sub_i8, .x=x.id, .y=y.id);
}
V8 weft_mul_i8(Builder* b, V8 x, V8 y) {
    sort_commutative(&x.id, &y.id);
    if (is_splat(b,y.id, 0)) { return y; }
    if (is_splat(b,x.id, 0)) { return x; }
    if (is_splat(b,y.id, 1)) { return x; }
    if (is_splat(b,x.id, 1)) { return y; }
    return inst(b, MATH,8,mul_i8, .x=x.id, .y=y.id);
}

V16 weft_add_i16(Builder* b, V16 x, V16 y) {
    sort_commutative(&x.id, &y.id);
    if (is_splat(b,y.id, 0)) { return x; }
    if (is_splat(b,x.id, 0)) { return y; }
    return inst(b, MATH,16,add_i16, .x=x.id, .y=y.id);
}
V16 weft_sub_i16(Builder* b, V16 x, V16 y) {
    if (is_splat(b,y.id, 0)) { return x; }
    return inst(b, MATH,16,sub_i16, .x=x.id, .y=y.id);
}
V16 weft_mul_i16(Builder* b, V16 x, V16 y) {
    sort_commutative(&x.id, &y.id);
    if (is_splat(b,y.id, 0)) { return y; }
    if (is_splat(b,x.id, 0)) { return x; }
    if (is_splat(b,y.id, 1)) { return x; }
    if (is_splat(b,x.id, 1)) { return y; }
    return inst(b, MATH,16,mul_i16, .x=x.id, .y=y.id);
}

V32 weft_add_i32(Builder* b, V32 x, V32 y) {
    sort_commutative(&x.id, &y.id);
    if (is_splat(b,y.id, 0)) { return x; }
    if (is_splat(b,x.id, 0)) { return y; }
    return inst(b, MATH,32,add_i32, .x=x.id, .y=y.id);
}
V32 weft_sub_i32(Builder* b, V32 x, V32 y) {
    if (is_splat(b,y.id, 0)) { return x; }
    return inst(b, MATH,32,sub_i32, .x=x.id, .y=y.id);
}
V32 weft_mul_i32(Builder* b, V32 x, V32 y) {
    sort_commutative(&x.id, &y.id);
    if (is_splat(b,y.id, 0)) { return y; }
    if (is_splat(b,x.id, 0)) { return x; }
    if (is_splat(b,y.id, 1)) { return x; }
    if (is_splat(b,x.id, 1)) { return y; }
    return inst(b, MATH,32,mul_i32, .x=x.id, .y=y.id);
}

V8 weft_shl_i8(Builder* b, V8 x, V8 y) {
    if (is_splat(b,y.id,0)) { return x; }
    return inst(b, MATH,8,shl_i8, .x=x.id, .y=y.id);
}
V8 weft_shr_s8(Builder* b, V8 x, V8 y) {
    if (is_splat(b,y.id,0)) { return x; }
    return inst(b, MATH,8,shr_s8, .x=x.id, .y=y.id);
}
V8 weft_shr_u8(Builder* b, V8 x, V8 y) {
    if (is_splat(b,y.id,0)) { return x; }
    return inst(b, MATH,8,shr_u8, .x=x.id, .y=y.id);
}

V16 weft_shl_i16(Builder* b, V16 x, V16 y) {
    if (is_splat(b,y.id,0)) { return x; }
    return inst(b, MATH,16,shl_i16, .x=x.id, .y=y.id);
}
V16 weft_shr_s16(Builder* b, V16 x, V16 y) {
    if (is_splat(b,y.id,0)) { return x; }
    return inst(b, MATH,16,shr_s16, .x=x.id, .y=y.id);
}
V16 weft_shr_u16(Builder* b, V16 x, V16 y) {
    if (is_splat(b,y.id,0)) { return x; }
    return inst(b, MATH,16,shr_u16, .x=x.id, .y=y.id);
}

V32 weft_shl_i32(Builder* b, V32 x, V32 y) {
    if (is_splat(b,y.id,0)) { return x; }
    return inst(b, MATH,32,shl_i32, .x=x.id, .y=y.id);
}
V32 weft_shr_s32(Builder* b, V32 x, V32 y) {
    if (is_splat(b,y.id,0)) { return x; }
    return inst(b, MATH,32,shr_s32, .x=x.id, .y=y.id);
}
V32 weft_shr_u32(Builder* b, V32 x, V32 y) {
    if (is_splat(b,y.id,0)) { return x; }
    return inst(b, MATH,32,shr_u32, .x=x.id, .y=y.id);
}

V8 weft_and_8(Builder* b, V8 x, V8 y) {
    sort_commutative(&x.id, &y.id);
    if (x.id == y.id) { return x; }
    if (is_splat(b,y.id, 0)) { return y; }
    if (is_splat(b,x.id, 0)) { return x; }
    if (is_splat(b,y.id,-1)) { return x; }
    if (is_splat(b,x.id,-1)) { return y; }
    return inst(b, MATH,8,and_8, .x=x.id, .y=y.id);
}
V8 weft_or_8(Builder* b, V8 x, V8 y) {
    sort_commutative(&x.id, &y.id);
    if (x.id == y.id) { return x; }
    if (is_splat(b,y.id, 0)) { return x; }
    if (is_splat(b,x.id, 0)) { return y; }
    if (is_splat(b,y.id,-1)) { return y; }
    if (is_splat(b,x.id,-1)) { return x; }
    return inst(b, MATH,8, or_8, .x=x.id, .y=y.id);
}
V8 weft_xor_8(Builder* b, V8 x, V8 y) {
    sort_commutative(&x.id, &y.id);
    if (x.id == y.id) { return weft_splat_8(b,0); }
    if (is_splat(b,y.id, 0)) { return x; }
    if (is_splat(b,x.id, 0)) { return y; }
    return inst(b, MATH,8,xor_8, .x=x.id, .y=y.id);
}
V8 weft_sel_8(Builder* b, V8 x, V8 y, V8 z) {
    if (is_splat(b,x.id, 0)) { return z; }
    if (is_splat(b,x.id,-1)) { return y; }
    if (is_splat(b,z.id, 0)) { return weft_and_8(b,x,y); }
    if (is_splat(b,y.id, 0)) { return inst(b, MATH,8,bic_8, .x=z.id, .y=x.id); }
    return inst(b, MATH,8,sel_8, .x=x.id, .y=y.id, .z=z.id);
}

V16 weft_and_16(Builder* b, V16 x, V16 y) {
    sort_commutative(&x.id, &y.id);
    if (x.id == y.id) { return x; }
    if (is_splat(b,y.id, 0)) { return y; }
    if (is_splat(b,x.id, 0)) { return x; }
    if (is_splat(b,y.id,-1)) { return x; }
    if (is_splat(b,x.id,-1)) { return y; }
    return inst(b, MATH,16,and_16, .x=x.id, .y=y.id);
}
V16 weft_or_16(Builder* b, V16 x, V16 y) {
    sort_commutative(&x.id, &y.id);
    if (x.id == y.id) { return x; }
    if (is_splat(b,y.id, 0)) { return x; }
    if (is_splat(b,x.id, 0)) { return y; }
    if (is_splat(b,y.id,-1)) { return y; }
    if (is_splat(b,x.id,-1)) { return x; }
    return inst(b, MATH,16, or_16, .x=x.id, .y=y.id);
}
V16 weft_xor_16(Builder* b, V16 x, V16 y) {
    sort_commutative(&x.id, &y.id);
    if (x.id == y.id) { return weft_splat_16(b,0); }
    if (is_splat(b,y.id, 0)) { return x; }
    if (is_splat(b,x.id, 0)) { return y; }
    return inst(b, MATH,16,xor_16, .x=x.id, .y=y.id);
}
V16 weft_sel_16(Builder* b, V16 x, V16 y, V16 z) {
    if (is_splat(b,x.id, 0)) { return z; }
    if (is_splat(b,x.id,-1)) { return y; }
    if (is_splat(b,z.id, 0)) { return weft_and_16(b,x,y); }
    if (is_splat(b,y.id, 0)) { return inst(b, MATH,16,bic_16, .x=z.id, .y=x.id); }
    return inst(b, MATH,16,sel_16, .x=x.id, .y=y.id, .z=z.id);
}

V32 weft_and_32(Builder* b, V32 x, V32 y) {
    sort_commutative(&x.id, &y.id);
    if (x.id == y.id) { return x; }
    if (is_splat(b,y.id, 0)) { return y; }
    if (is_splat(b,x.id, 0)) { return x; }
    if (is_splat(b,y.id,-1)) { return x; }
    if (is_splat(b,x.id,-1)) { return y; }
    return inst(b, MATH,32,and_32, .x=x.id, .y=y.id);
}
V32 weft_or_32(Builder* b, V32 x, V32 y) {
    sort_commutative(&x.id, &y.id);
    if (x.id == y.id) { return x; }
    if (is_splat(b,y.id, 0)) { return x; }
    if (is_splat(b,x.id, 0)) { return y; }
    if (is_splat(b,y.id,-1)) { return y; }
    if (is_splat(b,x.id,-1)) { return x; }
    return inst(b, MATH,32, or_32, .x=x.id, .y=y.id);
}
V32 weft_xor_32(Builder* b, V32 x, V32 y) {
    sort_commutative(&x.id, &y.id);
    if (x.id == y.id) { return weft_splat_32(b,0); }
    if (is_splat(b,y.id, 0)) { return x; }
    if (is_splat(b,x.id, 0)) { return y; }
    return inst(b, MATH,32,xor_32, .x=x.id, .y=y.id);
}
V32 weft_sel_32(Builder* b, V32 x, V32 y, V32 z) {
    if (is_splat(b,x.id, 0)) { return z; }
    if (is_splat(b,x.id,-1)) { return y; }
    if (is_splat(b,z.id, 0)) { return weft_and_32(b,x,y); }
    if (is_splat(b,y.id, 0)) { return inst(b, MATH,32,bic_32, .x=z.id, .y=x.id); }
    return inst(b, MATH,32,sel_32, .x=x.id, .y=y.id, .z=z.id);
}
