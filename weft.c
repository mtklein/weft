#include "weft.h"
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// TODO: loadN/storeN
// TODO: widen/narrow/cast
// TODO: full condition coverage in tests

#define N 8

typedef weft_V8  V8;
typedef weft_V16 V16;
typedef weft_V32 V32;
typedef weft_V64 V64;

typedef struct PInst {
    void (*fn)(const struct PInst*, int, unsigned, void*, void*, void* const ptr[]);
    int x,y,z,w;
    int : (sizeof(void*) == 4 ? 32 : 0);
    int64_t imm;
} PInst;

typedef struct weft_Program {
    int   slots;
    int   unused;
    int   loop_inst;
    int   loop_slot;
    PInst inst[];
} Program;

typedef struct {
    enum { MATH, SPLAT, UNIFORM, LOAD, STORE, DONE } kind;
    int slots;
    void (*fn         )(const PInst*, int, unsigned, void*, void*, void* const ptr[]);
    void (*fn_and_done)(const PInst*, int, unsigned, void*, void*, void* const ptr[]);
    int x,y,z,w;  // All BInst/Builder value IDs are 1-indexed so 0 can mean unused, N/A, etc.
    int64_t imm;
} BInst;

typedef struct weft_Builder {
    BInst                 *inst;
    int                    inst_len;
    int                    inst_cap;
    struct {int id,hash;} *cse;
    int                    cse_len;
    int                    cse_cap;
} Builder;

Builder* weft_builder(void) {
    return calloc(1, sizeof(Builder));
}

#if defined(__clang__)
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

static int lookup_cse(Builder* b, int hash, const BInst* inst) {
    for (int i = hash & (b->cse_cap-1), n = b->cse_cap; n --> 0;) {
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

static void insert_cse(Builder* b, int id, int hash) {
    assert(b->cse_len < b->cse_cap);
    for (int i = hash & (b->cse_cap-1), n = b->cse_cap; n --> 0;) {
        if (b->cse[i].id == 0) {
            b->cse[i].id   = id;
            b->cse[i].hash = hash;
            b->cse_len++;
            return;
        }
        i = (i+1) & (b->cse_cap-1);
    }
    assert(false);
}

// Each stage writes to R ("result") and calls next() with R incremented past its writes.
// Argument x starts at v(I->x); ditto for y,z,w.
// off tracks weft_run()'s progress [0,n), for offseting varying pointers.
// When operating on full N-sized chunks, tail is 0; tail is k for the final k<N sized chunk.
#define stage(name) static void name(const PInst* I, int off, unsigned tail, \
                                     void* restrict V, void* restrict R, void* const ptr[])
#define each    for (int i = 0; i < N; i++)
#define next(R) I[1].fn(I+1,off,tail,V,R,ptr); return
#define v(arg)  (void*)( (char*)V + arg )

stage(done) {
    (void)I;
    (void)off;
    (void)tail;
    (void)V;
    (void)R;
    (void)ptr;
}

static int constant_prop(Builder* b, const BInst* inst) {
    if (inst->kind == MATH
            && (!inst->x || b->inst[inst->x-1].kind == SPLAT)
            && (!inst->y || b->inst[inst->y-1].kind == SPLAT)
            && (!inst->z || b->inst[inst->z-1].kind == SPLAT)
            && (!inst->w || b->inst[inst->w-1].kind == SPLAT)) {
        PInst program[6], *p=program;

        const int* arg = &inst->x;
        int slot[4], slots = 0;
        for (int i = 0; i < 4; i++) {
            if (arg[i]) {
                *p++    = (PInst){.fn=b->inst[arg[i]-1].fn, .imm=b->inst[arg[i]-1].imm};
                slot[i] = slots;
                slots  += b->inst[arg[i]-1].slots;
            }
        }
        *p++ = (PInst){
            .fn  = inst->fn,
            .x   = inst->x ? slot[0] * N : 0,
            .y   = inst->y ? slot[1] * N : 0,
            .z   = inst->z ? slot[2] * N : 0,
            .w   = inst->w ? slot[3] * N : 0,
            .imm = inst->imm,
        };
        *p++ = (PInst){.fn=done};

        int64_t imm;
        char v[5*sizeof(imm)*N];
        assert((slots + inst->slots)*N <= (int)sizeof(v));
        program->fn(program,0,0,v,v,NULL);
        memcpy(&imm, v + slots*N, sizeof(imm));

        switch (inst->slots) {
            case 1: return weft_splat_8 (b, (int8_t )imm).id;
            case 2: return weft_splat_16(b, (int16_t)imm).id;
            case 4: return weft_splat_32(b, (int32_t)imm).id;
            case 8: return weft_splat_64(b,          imm).id;
        }
    }
    return 0;
}

static int inst_(Builder* b, BInst inst) {
    const int hash = (int)fnv1a(&inst, sizeof(inst));

    for (int id = lookup_cse(b, hash, &inst); id;) {
        return id;
    }
    for (int id = constant_prop(b, &inst); id;) {
        return id;
    }

    const int id = ++b->inst_len;

    if (b->inst_cap < b->inst_len) {
        b->inst_cap = b->inst_cap ? 2*b->inst_cap : 1;
        b->inst = realloc(b->inst, (size_t)b->inst_cap * sizeof(*b->inst));
    }
    b->inst[id-1] = inst;

    if (inst.kind <= UNIFORM) {
        if (b->cse_len >= b->cse_cap*3/4) {
            Builder grown = *b;
            grown.cse_len = 0;
            grown.cse_cap = b->cse_cap ? b->cse_cap*2 : 1;
            grown.cse     = calloc((size_t)grown.cse_cap, sizeof *grown.cse);
            for (int i = 0; i < b->cse_cap; i++) {
                if (b->cse[i].id) {
                    insert_cse(&grown, b->cse[i].id, b->cse[i].hash);
                }
            }
            free(b->cse);
            *b = grown;
        }
        insert_cse(b,id,hash);
    }
    return id;
}
#define inst(b,kind,bits,fn,...) (V##bits){inst_(b, (BInst){kind,bits/8,fn, __VA_ARGS__})}

void weft_run(const weft_Program* p, int n, void* const ptr[]) {
    void* V = malloc(N * (size_t)p->slots);

    void* R = V;
    const PInst* inst = p->inst;

    for (int off = 0; off+N <= n; off += N) {
        inst->fn(inst,off,0,V,R,ptr);
        inst = p->inst + p->loop_inst;
        R    = (char*)V + (N * p->loop_slot);
    }
    for (unsigned tail = (unsigned)(n - n/N*N); tail; ) {
        inst->fn(inst,n/N*N,tail,V,R,ptr);
        break;
    }

    free(V);
}

Program* weft_compile(Builder* b) {
    if (b->inst_len == 0 || !b->inst[b->inst_len-1].fn_and_done) {
        inst_(b, (BInst){DONE, .fn_and_done=done});
    }

    union {
        struct { bool live, loop_dependent; };
        int slot;
    } *meta = calloc((size_t)b->inst_len, sizeof *meta);

    int live_insts = 0;
    for (int i = b->inst_len; i --> 0;) {
        const BInst inst = b->inst[i];
        if (inst.kind >= STORE) {
            meta[i].live = true;
        }
        if (meta[i].live) {
            live_insts++;
            if (inst.x) { meta[inst.x-1].live = true; }
            if (inst.y) { meta[inst.y-1].live = true; }
            if (inst.z) { meta[inst.z-1].live = true; }
            if (inst.w) { meta[inst.w-1].live = true; }
        }
    }

    for (int i = 0; i < b->inst_len; i++) {
        const BInst inst = b->inst[i];
        meta[i].loop_dependent = inst.kind >= LOAD
                              || (inst.x && meta[inst.x-1].loop_dependent)
                              || (inst.y && meta[inst.y-1].loop_dependent)
                              || (inst.z && meta[inst.z-1].loop_dependent)
                              || (inst.w && meta[inst.w-1].loop_dependent);
    }

    Program* p = malloc(sizeof(*p) + (size_t)live_insts * sizeof(*p->inst));
    p->slots   = 0;
    int insts  = 0;

    for (int loop_dependent = 0; loop_dependent < 2; loop_dependent++) {
        if (loop_dependent) {
            p->loop_inst = insts;
            p->loop_slot = p->slots;
        }
        for (int i = 0; i < b->inst_len; i++) {
            if (meta[i].live && meta[i].loop_dependent == loop_dependent) {
                const BInst inst = b->inst[i];
                p->inst[insts++] = (PInst) {
                    .fn  = (i == b->inst_len-1) ? inst.fn_and_done : inst.fn,
                    .x   = inst.x ? meta[inst.x-1].slot * N : 0,
                    .y   = inst.y ? meta[inst.y-1].slot * N : 0,
                    .z   = inst.z ? meta[inst.z-1].slot * N : 0,
                    .w   = inst.w ? meta[inst.w-1].slot * N : 0,
                    .imm = inst.imm,
                };
                meta[i].slot = p->slots;
                p->slots += inst.slots;
            }
        }
    }
    assert(insts == live_insts);

    free(meta);
    free(b->inst);
    free(b->cse);
    free(b);
    return p;
}


stage(splat_8 ) { int8_t  *r=R; each r[i] = (int8_t )I->imm; next(r+N); }
stage(splat_16) { int16_t *r=R; each r[i] = (int16_t)I->imm; next(r+N); }
stage(splat_32) { int32_t *r=R; each r[i] = (int32_t)I->imm; next(r+N); }
stage(splat_64) { int64_t *r=R; each r[i] = (int64_t)I->imm; next(r+N); }

V8  weft_splat_8 (Builder* b, int8_t  bits) { return inst(b, SPLAT,8 ,splat_8 , .imm=bits); }
V16 weft_splat_16(Builder* b, int16_t bits) { return inst(b, SPLAT,16,splat_16, .imm=bits); }
V32 weft_splat_32(Builder* b, int32_t bits) { return inst(b, SPLAT,32,splat_32, .imm=bits); }
V64 weft_splat_64(Builder* b, int64_t bits) { return inst(b, SPLAT,64,splat_64, .imm=bits); }

stage(uniform_8)  { int8_t  *r=R, u=*(const int8_t* )ptr[I->imm]; each r[i] = u; next(r+N); }
stage(uniform_16) { int16_t *r=R, u=*(const int16_t*)ptr[I->imm]; each r[i] = u; next(r+N); }
stage(uniform_32) { int32_t *r=R, u=*(const int32_t*)ptr[I->imm]; each r[i] = u; next(r+N); }
stage(uniform_64) { int64_t *r=R, u=*(const int64_t*)ptr[I->imm]; each r[i] = u; next(r+N); }

V8  weft_uniform_8 (Builder* b, int ptr) { return inst(b, UNIFORM,8 ,uniform_8 , .imm=ptr); }
V16 weft_uniform_16(Builder* b, int ptr) { return inst(b, UNIFORM,16,uniform_16, .imm=ptr); }
V32 weft_uniform_32(Builder* b, int ptr) { return inst(b, UNIFORM,32,uniform_32, .imm=ptr); }
V64 weft_uniform_64(Builder* b, int ptr) { return inst(b, UNIFORM,64,uniform_64, .imm=ptr); }

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
stage(load_64) {
    int64_t* r = R;
    tail ? memcpy(r, (const int64_t*)ptr[I->imm] + off, 8*tail)
         : memcpy(r, (const int64_t*)ptr[I->imm] + off, 8*N);
    next(r+N);
}

V8  weft_load_8 (Builder* b, int ptr) { return inst(b, LOAD,8 ,load_8 , .imm=ptr); }
V16 weft_load_16(Builder* b, int ptr) { return inst(b, LOAD,16,load_16, .imm=ptr); }
V32 weft_load_32(Builder* b, int ptr) { return inst(b, LOAD,32,load_32, .imm=ptr); }
V64 weft_load_64(Builder* b, int ptr) { return inst(b, LOAD,64,load_64, .imm=ptr); }

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
stage(store_64) {
    tail ? memcpy((int64_t*)ptr[I->imm] + off, v(I->x), 8*tail)
         : memcpy((int64_t*)ptr[I->imm] + off, v(I->x), 8*N);
    next(R);
}
stage(store_64_and_done) {
    tail ? memcpy((int64_t*)ptr[I->imm] + off, v(I->x), 8*tail)
         : memcpy((int64_t*)ptr[I->imm] + off, v(I->x), 8*N);
    (void)R;
}

void weft_store_8 (Builder* b, int ptr, V8  x) {
    inst_(b, (BInst){STORE, .fn=store_8 , .fn_and_done=store_8_and_done , .x=x.id, .imm=ptr});
}
void weft_store_16(Builder* b, int ptr, V16 x) {
    inst_(b, (BInst){STORE, .fn=store_16, .fn_and_done=store_16_and_done, .x=x.id, .imm=ptr});
}
void weft_store_32(Builder* b, int ptr, V32 x) {
    inst_(b, (BInst){STORE, .fn=store_32, .fn_and_done=store_32_and_done, .x=x.id, .imm=ptr});
}
void weft_store_64(Builder* b, int ptr, V64 x) {
    inst_(b, (BInst){STORE, .fn=store_64, .fn_and_done=store_64_and_done, .x=x.id, .imm=ptr});
}


static const int64_t f16_n0 = 0x8000, f32_n0 = 0x80000000, f64_n0 = (int64_t)0x8000000000000000,
                     f16_p1 = 0x3c00, f32_p1 = 0x3f800000, f64_p1 =          0x3ff0000000000000;

static bool is_splat(Builder* b, int id, int64_t imm) {
    return b->inst[id-1].kind == SPLAT
        && b->inst[id-1].imm  == imm;
}
static bool any_splat(Builder* b, int id, int64_t* imm) {
    *imm = b->inst[id-1].imm;
    return b->inst[id-1].kind == SPLAT;
}

static void sort_commutative(int* x, int* y) {
    int lo = *x < *y ? *x : *y,
        hi = *x < *y ? *y : *x;
    *x = lo;
    *y = hi;
}

stage(add_f16) {
    __fp16 *r=R, *x=v(I->x), *y=v(I->y);
    each r[i] = (__fp16)((float)x[i] + (float)y[i]);
    next(r+N);
}
stage(sub_f16) {
    __fp16 *r=R, *x=v(I->x), *y=v(I->y);
    each r[i] = (__fp16)((float)x[i] - (float)y[i]);
    next(r+N);
}
stage(mul_f16) {
    __fp16 *r=R, *x=v(I->x), *y=v(I->y);
    each r[i] = (__fp16)((float)x[i] * (float)y[i]);
    next(r+N);
}
stage(div_f16) {
    __fp16 *r=R, *x=v(I->x), *y=v(I->y);
    each r[i] = (__fp16)((float)x[i] / (float)y[i]);
    next(r+N);
}

V16 weft_add_f16(Builder* b, V16 x, V16 y) {
    sort_commutative(&x.id, &y.id);
    if (is_splat(b,y.id,      0)) { return x; }
    if (is_splat(b,y.id, f16_n0)) { return x; }
    if (is_splat(b,x.id,      0)) { return y; }
    if (is_splat(b,x.id, f16_n0)) { return y; }
    return inst(b, MATH,16, add_f16, .x=x.id, .y=y.id);
}
V16 weft_sub_f16(Builder* b, V16 x, V16 y) {
    if (is_splat(b,y.id,      0)) { return x; }
    if (is_splat(b,y.id, f16_n0)) { return x; }
    return inst(b, MATH,16, sub_f16, .x=x.id, .y=y.id);
}
V16 weft_mul_f16(Builder* b, V16 x, V16 y) {
    sort_commutative(&x.id, &y.id);
    // Note: x*0 isn't 0 when x=NaN.
    if (is_splat(b,y.id, f16_p1)) { return x; }
    if (is_splat(b,x.id, f16_p1)) { return y; }
    return inst(b, MATH,16, mul_f16, .x=x.id, .y=y.id);
}
V16 weft_div_f16(Builder* b, V16 x, V16 y) {
    if (is_splat(b,y.id, f16_p1)) { return x; }
    return inst(b, MATH,16, div_f16, .x=x.id, .y=y.id);
}

stage(add_f32) { float *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]+y[i]; next(r+N); }
stage(sub_f32) { float *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]-y[i]; next(r+N); }
stage(mul_f32) { float *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]*y[i]; next(r+N); }
stage(div_f32) { float *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]/y[i]; next(r+N); }

V32 weft_add_f32(Builder* b, V32 x, V32 y) {
    sort_commutative(&x.id, &y.id);
    if (is_splat(b,y.id,      0)) { return x; }
    if (is_splat(b,y.id, f32_n0)) { return x; }
    if (is_splat(b,x.id,      0)) { return y; }
    if (is_splat(b,x.id, f32_n0)) { return y; }
    return inst(b, MATH,32, add_f32, .x=x.id, .y=y.id);
}
V32 weft_sub_f32(Builder* b, V32 x, V32 y) {
    if (is_splat(b,y.id,      0)) { return x; }
    if (is_splat(b,y.id, f32_n0)) { return x; }
    return inst(b, MATH,32, sub_f32, .x=x.id, .y=y.id);
}
V32 weft_mul_f32(Builder* b, V32 x, V32 y) {
    sort_commutative(&x.id, &y.id);
    // Note: x*0 isn't 0 when x=NaN.
    if (is_splat(b,y.id, f32_p1)) { return x; }
    if (is_splat(b,x.id, f32_p1)) { return y; }
    return inst(b, MATH,32, mul_f32, .x=x.id, .y=y.id);
}
V32 weft_div_f32(Builder* b, V32 x, V32 y) {
    if (is_splat(b,y.id, f32_p1)) { return x; }
    return inst(b, MATH,32, div_f32, .x=x.id, .y=y.id);
}

stage(add_f64) { double *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]+y[i]; next(r+N); }
stage(sub_f64) { double *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]-y[i]; next(r+N); }
stage(mul_f64) { double *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]*y[i]; next(r+N); }
stage(div_f64) { double *r=R, *x=v(I->x), *y=v(I->y); each r[i] = x[i]/y[i]; next(r+N); }

V64 weft_add_f64(Builder* b, V64 x, V64 y) {
    sort_commutative(&x.id, &y.id);
    if (is_splat(b,y.id,      0)) { return x; }
    if (is_splat(b,y.id, f64_n0)) { return x; }
    if (is_splat(b,x.id,      0)) { return y; }
    if (is_splat(b,x.id, f64_n0)) { return y; }
    return inst(b, MATH,64, add_f64, .x=x.id, .y=y.id);
}
V64 weft_sub_f64(Builder* b, V64 x, V64 y) {
    if (is_splat(b,y.id,      0)) { return x; }
    if (is_splat(b,y.id, f64_n0)) { return x; }
    return inst(b, MATH,64, sub_f64, .x=x.id, .y=y.id);
}
V64 weft_mul_f64(Builder* b, V64 x, V64 y) {
    sort_commutative(&x.id, &y.id);
    // Note: x*0 isn't 0 when x=NaN.
    if (is_splat(b,y.id, f64_p1)) { return x; }
    if (is_splat(b,x.id, f64_p1)) { return y; }
    return inst(b, MATH,64, mul_f64, .x=x.id, .y=y.id);
}
V64 weft_div_f64(Builder* b, V64 x, V64 y) {
    if (is_splat(b,y.id, f64_p1)) { return x; }
    return inst(b, MATH,64, div_f64, .x=x.id, .y=y.id);
}

stage( ceil_f16) { __fp16 *r=R, *x=v(I->x); each r[i] = (__fp16) ceilf((float)x[i]); next(r+N); }
stage( ceil_f32) { float  *r=R, *x=v(I->x); each r[i] =          ceilf(       x[i]); next(r+N); }
stage( ceil_f64) { double *r=R, *x=v(I->x); each r[i] =          ceil (       x[i]); next(r+N); }
stage(floor_f16) { __fp16 *r=R, *x=v(I->x); each r[i] = (__fp16)floorf((float)x[i]); next(r+N); }
stage(floor_f32) { float  *r=R, *x=v(I->x); each r[i] =         floorf(       x[i]); next(r+N); }
stage(floor_f64) { double *r=R, *x=v(I->x); each r[i] =         floor (       x[i]); next(r+N); }
stage( sqrt_f16) { __fp16 *r=R, *x=v(I->x); each r[i] = (__fp16) sqrtf((float)x[i]); next(r+N); }
stage( sqrt_f32) { float  *r=R, *x=v(I->x); each r[i] =          sqrtf(       x[i]); next(r+N); }
stage( sqrt_f64) { double *r=R, *x=v(I->x); each r[i] =          sqrt (       x[i]); next(r+N); }

V16  weft_ceil_f16(Builder* b, V16 x) { return inst(b, MATH,16, ceil_f16, .x=x.id); }
V32  weft_ceil_f32(Builder* b, V32 x) { return inst(b, MATH,32, ceil_f32, .x=x.id); }
V64  weft_ceil_f64(Builder* b, V64 x) { return inst(b, MATH,64, ceil_f64, .x=x.id); }
V16 weft_floor_f16(Builder* b, V16 x) { return inst(b, MATH,16,floor_f16, .x=x.id); }
V32 weft_floor_f32(Builder* b, V32 x) { return inst(b, MATH,32,floor_f32, .x=x.id); }
V64 weft_floor_f64(Builder* b, V64 x) { return inst(b, MATH,64,floor_f64, .x=x.id); }
V16  weft_sqrt_f16(Builder* b, V16 x) { return inst(b, MATH,16, sqrt_f16, .x=x.id); }
V32  weft_sqrt_f32(Builder* b, V32 x) { return inst(b, MATH,32, sqrt_f32, .x=x.id); }
V64  weft_sqrt_f64(Builder* b, V64 x) { return inst(b, MATH,64, sqrt_f64, .x=x.id); }

#define INT_STAGES(B,S,U) \
    stage(add_i ##B) {U *r=R, *x=v(I->x), *y=v(I->y); each r[i] =    x[i] +  y[i] ; next(r+N);}  \
    stage(sub_i ##B) {U *r=R, *x=v(I->x), *y=v(I->y); each r[i] =    x[i] -  y[i] ; next(r+N);}  \
    stage(mul_i ##B) {U *r=R, *x=v(I->x), *y=v(I->y); each r[i] =    x[i] *  y[i] ; next(r+N);}  \
    stage(shlv_i##B) {S *r=R, *x=v(I->x), *y=v(I->y); each r[i] =(S)(x[i] << y[i]); next(r+N);}  \
    stage(shrv_s##B) {S *r=R, *x=v(I->x), *y=v(I->y); each r[i] =    x[i] >> y[i] ; next(r+N);}  \
    stage(shrv_u##B) {U *r=R, *x=v(I->x), *y=v(I->y); each r[i] =    x[i] >> y[i] ; next(r+N);}  \
    stage(shli_i##B) {S *r=R, *x=v(I->x);             each r[i] =(S)(x[i]<<I->imm); next(r+N);}  \
    stage(shri_s##B) {S *r=R, *x=v(I->x);             each r[i] =    x[i]>>I->imm ; next(r+N);}  \
    stage(shri_u##B) {U *r=R, *x=v(I->x);             each r[i] =    x[i]>>I->imm ; next(r+N);}  \
    stage(and_  ##B) {U *r=R, *x=v(I->x), *y=v(I->y); each r[i] =    x[i] &  y[i] ; next(r+N);}  \
    stage(bic_  ##B) {U *r=R, *x=v(I->x), *y=v(I->y); each r[i] =    x[i] & ~y[i] ; next(r+N);}  \
    stage( or_  ##B) {U *r=R, *x=v(I->x), *y=v(I->y); each r[i] =    x[i] |  y[i] ; next(r+N);}  \
    stage(xor_  ##B) {U *r=R, *x=v(I->x), *y=v(I->y); each r[i] =    x[i] ^  y[i] ; next(r+N);}  \
    stage(sel_ ##B) {                                                                            \
        U *r=R, *x=v(I->x), *y=v(I->y), *z=v(I->z);                                              \
        each r[i] = ( x[i] & y[i])                                                               \
                  | (~x[i] & z[i]);                                                              \
        next(r+N);                                                                               \
    }                                                                                            \
    V##B weft_add_i##B(Builder* b, V##B x, V##B y) {                                             \
        sort_commutative(&x.id, &y.id);                                                          \
        if (is_splat(b,y.id, 0)) { return x; }                                                   \
        if (is_splat(b,x.id, 0)) { return y; }                                                   \
        return inst(b, MATH,B,add_i##B, .x=x.id, .y=y.id);                                       \
    }                                                                                            \
    V##B weft_sub_i##B(Builder* b, V##B x, V##B y) {                                             \
        if (is_splat(b,y.id, 0)) { return x; }                                                   \
        return inst(b, MATH,B,sub_i##B, .x=x.id, .y=y.id);                                       \
    }                                                                                            \
    V##B weft_mul_i##B(Builder* b, V##B x, V##B y) {                                             \
        sort_commutative(&x.id, &y.id);                                                          \
        if (is_splat(b,y.id, 0)) { return y; }                                                   \
        if (is_splat(b,x.id, 0)) { return x; }                                                   \
        if (is_splat(b,y.id, 1)) { return x; }                                                   \
        if (is_splat(b,x.id, 1)) { return y; }                                                   \
        return inst(b, MATH,B,mul_i##B, .x=x.id, .y=y.id);                                       \
    }                                                                                            \
    V##B weft_shl_i##B(Builder* b, V##B x, V##B y) {                                             \
        if (is_splat(b,y.id,0)) { return x; }                                                    \
        for (int64_t imm; any_splat(b,y.id,&imm);) {                                             \
            return inst(b, MATH,B,shli_i##B, .x=x.id, .imm=imm);                                 \
        }                                                                                        \
        return inst(b, MATH,B,shlv_i##B, .x=x.id, .y=y.id);                                      \
    }                                                                                            \
    V##B weft_shr_s##B(Builder* b, V##B x, V##B y) {                                             \
        if (is_splat(b,y.id,0)) { return x; }                                                    \
        for (int64_t imm; any_splat(b,y.id,&imm);) {                                             \
            return inst(b, MATH,B,shri_s##B, .x=x.id, .imm=imm);                                 \
        }                                                                                        \
        return inst(b, MATH,B,shrv_s##B, .x=x.id, .y=y.id);                                      \
    }                                                                                            \
    V##B weft_shr_u##B(Builder* b, V##B x, V##B y) {                                             \
        if (is_splat(b,y.id,0)) { return x; }                                                    \
        for (int64_t imm; any_splat(b,y.id,&imm);) {                                             \
            return inst(b, MATH,B,shri_u##B, .x=x.id, .imm=imm);                                 \
        }                                                                                        \
        return inst(b, MATH,B,shrv_u##B, .x=x.id, .y=y.id);                                      \
    }                                                                                            \
    V##B weft_and_##B(Builder* b, V##B x, V##B y) {                                              \
        sort_commutative(&x.id, &y.id);                                                          \
        if (x.id == y.id) { return x; }                                                          \
        if (is_splat(b,y.id, 0)) { return y; }                                                   \
        if (is_splat(b,x.id, 0)) { return x; }                                                   \
        if (is_splat(b,y.id,-1)) { return x; }                                                   \
        if (is_splat(b,x.id,-1)) { return y; }                                                   \
        return inst(b, MATH,B,and_##B, .x=x.id, .y=y.id);                                        \
    }                                                                                            \
    V##B weft_or_##B(Builder* b, V##B x, V##B y) {                                               \
        sort_commutative(&x.id, &y.id);                                                          \
        if (x.id == y.id) { return x; }                                                          \
        if (is_splat(b,y.id, 0)) { return x; }                                                   \
        if (is_splat(b,x.id, 0)) { return y; }                                                   \
        if (is_splat(b,y.id,-1)) { return y; }                                                   \
        if (is_splat(b,x.id,-1)) { return x; }                                                   \
        return inst(b, MATH,B,or_##B, .x=x.id, .y=y.id);                                         \
    }                                                                                            \
    V##B weft_xor_##B(Builder* b, V##B x, V##B y) {                                              \
        sort_commutative(&x.id, &y.id);                                                          \
        if (x.id == y.id) { return weft_splat_##B(b,0); }                                        \
        if (is_splat(b,y.id, 0)) { return x; }                                                   \
        if (is_splat(b,x.id, 0)) { return y; }                                                   \
        return inst(b, MATH,B,xor_##B, .x=x.id, .y=y.id);                                        \
    }                                                                                            \
    V##B weft_sel_##B(Builder* b, V##B x, V##B y, V##B z) {                                      \
        if (is_splat(b,x.id, 0)) { return z; }                                                   \
        if (is_splat(b,x.id,-1)) { return y; }                                                   \
        if (is_splat(b,z.id, 0)) { return weft_and_##B(b,x,y); }                                 \
        if (is_splat(b,y.id, 0)) { return inst(b, MATH,B,bic_##B, .x=z.id, .y=x.id); }           \
        return inst(b, MATH,B,sel_##B, .x=x.id, .y=y.id, .z=z.id);                               \
    }

INT_STAGES( 8, int8_t, uint8_t)
INT_STAGES(16,int16_t,uint16_t)
INT_STAGES(32,int32_t,uint32_t)
INT_STAGES(64,int64_t,uint64_t)
