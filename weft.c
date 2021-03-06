#include "weft.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <tgmath.h>

// TODO: test GCC with -march=armv8.2-a+fp16

#define N 8

typedef weft_V8  V8;
typedef weft_V16 V16;
typedef weft_V32 V32;
typedef weft_V64 V64;

typedef struct PInst {
    void (*fn)(const struct PInst*, int, unsigned, void*, void*, void* const ptr[]);
    int x,y,z;
    int : (sizeof(void*) == 8 ? 32 : 0);
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
    int64_t imm;
    int x,y,z;  // All BInst/Builder value IDs are 1-indexed so 0 can mean unused, N/A, etc.
    enum { MATH, SPLAT, UNIFORM, LOAD, SIDE_EFFECT } kind : 16;
    int slots                                             : 16;
    void  (*fn  )(const PInst*, int, unsigned, void*, void*, void* const ptr[]);
    void  (*done)(const PInst*, int, unsigned, void*, void*, void* const ptr[]);
    char* (*jit )(char*, int[], int[], int[], int[], int64_t);
    void*         unused;
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

static uint32_t fnv1a(const void* vp, size_t len) {
    uint32_t hash = 0x811c9dc5;
    for (const uint8_t *p = vp, *end=p+len; p != end;) {
        hash ^= *p++;
        (void)__builtin_mul_overflow(hash, 0x01000193, &hash);
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
            n = 0;  // i.e. break;
        }
        i = (i+1) & (b->cse_cap-1);
    }
}

// Each stage writes to R ("result") and calls next() with R incremented past its writes.
// Argument x starts at v(x); ditto for y,z.
// off tracks weft_run()'s progress [0,n), for offseting varying pointers.
// When operating on full N-sized chunks, tail is 0; tail is k for the final k<N sized chunk.
#define stage(name) static void name(const PInst* inst, int off, unsigned tail, \
                                     void* restrict V, void* restrict R, void* const ptr[])
#define each    for (int i = 0; i < N; i++)
#define next(R) inst[1].fn(inst+1,off,tail,V,R,ptr); return
#define v(arg)  (void*)( (char*)V + inst->arg )

stage(done) {
    (void)inst;
    (void)off;
    (void)tail;
    (void)V;
    (void)R;
    (void)ptr;
}

static int constant_prop(Builder* b, const BInst* inst) {
    if (inst->kind == MATH
            && (            b->inst[inst->x-1].kind == SPLAT)
            && (!inst->y || b->inst[inst->y-1].kind == SPLAT)
            && (!inst->z || b->inst[inst->z-1].kind == SPLAT)) {
        PInst program[5], *p=program;

        const int* arg = &inst->x;
        int slot[3]={0}, slots = 0;
        for (int i = 0; i < 3; i++) {
            if (arg[i]) {
                *p++    = (PInst){.fn=b->inst[arg[i]-1].fn, .imm=b->inst[arg[i]-1].imm};
                slot[i] = slots;
                slots  += b->inst[arg[i]-1].slots;
            }
        }
        *p++ = (PInst){
            .fn  = inst->fn,
            .x   = slot[0] * N,
            .y   = slot[1] * N,
            .z   = slot[2] * N,
            .imm = inst->imm,
        };
        *p++ = (PInst){.fn=done};

        int64_t imm;
        char v[4*sizeof(imm)*N];
        assert((slots + inst->slots)*N <= (int)sizeof(v)); (void)0;
        program->fn(program,0,0,v,v,NULL);
        memcpy(&imm, v + slots*N, sizeof(imm));

        switch (inst->slots) {
            case  1: return weft_splat_8 (b, (int8_t )imm).id;
            case  2: return weft_splat_16(b, (int16_t)imm).id;
            case  4: return weft_splat_32(b, (int32_t)imm).id;
            default: return weft_splat_64(b,          imm).id;
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
#define inst(b,k,bits,f,...) (V##bits){inst_(b,(BInst){.kind=k, .slots=bits/8, .fn=f, __VA_ARGS__})}

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
    if (b->inst_len == 0 || !b->inst[b->inst_len-1].done) {
        inst_(b, (BInst){.kind=SIDE_EFFECT, .done=done});
    }

    struct {
        bool live, loop_dependent;
        bool unusedA, unusedB;
        int slot;
    } *meta = calloc((size_t)b->inst_len, sizeof *meta);

    int live_insts = 0;
    for (int i = b->inst_len; i --> 0;) {
        const BInst inst = b->inst[i];
        if (inst.kind >= SIDE_EFFECT) {
            meta[i].live = true;
        }
        if (meta[i].live) {
            live_insts++;
            if (inst.x) { meta[inst.x-1].live = true; }
            if (inst.y) { meta[inst.y-1].live = true; }
            if (inst.z) { meta[inst.z-1].live = true; }
        }
    }

    for (int i = 0; i < b->inst_len; i++) {
        const BInst inst = b->inst[i];
        meta[i].loop_dependent = inst.kind >= LOAD
                              || (inst.x && meta[inst.x-1].loop_dependent)
                              || (inst.y && meta[inst.y-1].loop_dependent)
                              || (inst.z && meta[inst.z-1].loop_dependent);
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
                    .fn  = (i == b->inst_len-1) ? inst.done : inst.fn,
                    .x   = inst.x ? meta[inst.x-1].slot * N : 0,
                    .y   = inst.y ? meta[inst.y-1].slot * N : 0,
                    .z   = inst.z ? meta[inst.z-1].slot * N : 0,
                    .imm = inst.imm,
                };
                meta[i].slot = p->slots;
                p->slots += inst.slots;
            }
        }
    }
    assert(insts == live_insts); (void)0;

    free(meta);
    free(b->inst);
    free(b->cse);
    free(b);
    return p;
}

#if defined(__aarch64__)
    // x0:    n
    // x1-x7: ptr0-ptr6
    // x8:    i
    // x9:    tmp
    static const int Ri = 8,
                   Rtmp = 9;

    #define mask(x,bits)   ((uint32_t)x) & ((1<<bits)-1)
    #define emit(buf,inst) (memcpy(buf, &inst, sizeof inst), buf+sizeof(inst))
    static char* emit4(char* buf, uint32_t inst) { return emit(buf, inst); }

    static char* movz(char* buf, int Rd, int64_t imm, int hw) {
        struct {
            uint32_t Rd  :  5;
            uint32_t imm : 16;
            uint32_t hw  :  2;
            uint32_t opc :  8;
            uint32_t sf  :  1;
        } inst = {mask(Rd,5), mask(imm,16), mask(hw,2), 0xa5, 1};
        return emit(buf, inst);
    }
    static char* movk(char* buf, int Rd, int64_t imm, int hw) {
        struct {
            uint32_t Rd  :  5;
            uint32_t imm : 16;
            uint32_t hw  :  2;
            uint32_t opc :  8;
            uint32_t sf  :  1;
        } inst = {mask(Rd,5), mask(imm,16), mask(hw,2), 0xe5, 1};
        return emit(buf, inst);
    }
    static char* dup(char* buf, int Rd, int Rn, int imm, int Q) {
        struct {
            uint32_t Rd  : 5;
            uint32_t Rn  : 5;
            uint32_t lo  : 6;
            uint32_t imm : 5;
            uint32_t hi  : 9;
            uint32_t Q   : 1;
            uint32_t z   : 1;
        } inst = {mask(Rd,5), mask(Rn,5), 0x3, mask(imm,5), 0x70, mask(Q,1), 0};
        return emit(buf, inst);
    }
    static char* add(char* buf, int Rd, int Rn, int Rm) {
        struct {
            uint32_t Rd  : 5;
            uint32_t Rn  : 5;
            uint32_t imm : 6;
            uint32_t Rm  : 5;
            uint32_t top : 11;
        } inst = {mask(Rd,5), mask(Rn,5), 0, mask(Rm,5), 0x458};
        return emit(buf, inst);
    }
#endif


stage(splat_8 ) { int8_t  *r=R; each r[i] = (int8_t )inst->imm; next(r+N); }
stage(splat_16) { int16_t *r=R; each r[i] = (int16_t)inst->imm; next(r+N); }
stage(splat_32) { int32_t *r=R; each r[i] = (int32_t)inst->imm; next(r+N); }
stage(splat_64) { int64_t *r=R; each r[i] = (int64_t)inst->imm; next(r+N); }

#if defined(__aarch64__)
    static char* jit_splat_8(char* buf, int d[], int x[], int y[], int z[], int64_t imm) {
        (void)x; (void)y; (void)z;
        buf = movz(buf, Rtmp, imm, 0);     // mov tmp, imm
        return dup(buf, d[0], Rtmp, 1,0);  // dup.8b d[0], tmp
    }
    static char* jit_splat_16(char* buf, int d[], int x[], int y[], int z[], int64_t imm) {
        (void)x; (void)y; (void)z;
        buf = movz(buf, Rtmp, imm, 0);     // mov tmp, imm
        return dup(buf, d[0], Rtmp, 2,1);  // dup.8h d[0],tmp
    }
    static char* jit_splat_32(char* buf, int d[], int x[], int y[], int z[], int64_t imm) {
        (void)x; (void)y; (void)z;
        buf = movz(buf, Rtmp, imm>> 0, 0);  // mov  tmp, imm15:0
        buf = movk(buf, Rtmp, imm>>16, 1);  // movk tmp, imm31:16
        buf =  dup(buf, d[0], Rtmp, 4, 1);  // dup.4s d[0], tmp
        return dup(buf, d[1], Rtmp, 4, 1);  // dup.4s d[1], tmp
    }
    static char* jit_splat_64(char* buf, int d[], int x[], int y[], int z[], int64_t imm) {
        (void)x; (void)y; (void)z;
        buf = movz(buf, Rtmp, imm>> 0, 0);
        buf = movk(buf, Rtmp, imm>>16, 1);
        buf = movk(buf, Rtmp, imm>>32, 2);
        buf = movk(buf, Rtmp, imm>>48, 3);
        buf =  dup(buf, d[0], Rtmp, 8, 1);
        buf =  dup(buf, d[1], Rtmp, 8, 1);
        buf =  dup(buf, d[2], Rtmp, 8, 1);
        return dup(buf, d[3], Rtmp, 8, 1);
    }
#else
    #define jit_splat_8  NULL
    #define jit_splat_16 NULL
    #define jit_splat_32 NULL
    #define jit_splat_64 NULL
#endif

V8  weft_splat_8 (Builder* b, int8_t  bits) {
    return inst(b, SPLAT,8 ,splat_8 , .imm=bits, .jit=jit_splat_8);
}
V16 weft_splat_16(Builder* b, int16_t bits) {
    return inst(b, SPLAT,16,splat_16, .imm=bits, .jit=jit_splat_16);
}
V32 weft_splat_32(Builder* b, int32_t bits) {
    return inst(b, SPLAT,32,splat_32, .imm=bits, .jit=jit_splat_32);
}
V64 weft_splat_64(Builder* b, int64_t bits) {
    return inst(b, SPLAT,64,splat_64, .imm=bits, .jit=jit_splat_64);
}

stage(uniform_8)  { int8_t  *r=R, u=*(const int8_t* )ptr[inst->imm]; each r[i] = u; next(r+N); }
stage(uniform_16) { int16_t *r=R, u=*(const int16_t*)ptr[inst->imm]; each r[i] = u; next(r+N); }
stage(uniform_32) { int32_t *r=R, u=*(const int32_t*)ptr[inst->imm]; each r[i] = u; next(r+N); }
stage(uniform_64) { int64_t *r=R, u=*(const int64_t*)ptr[inst->imm]; each r[i] = u; next(r+N); }

V8  weft_uniform_8 (Builder* b, int ptr) { return inst(b, UNIFORM,8 ,uniform_8 , .imm=ptr); }
V16 weft_uniform_16(Builder* b, int ptr) { return inst(b, UNIFORM,16,uniform_16, .imm=ptr); }
V32 weft_uniform_32(Builder* b, int ptr) { return inst(b, UNIFORM,32,uniform_32, .imm=ptr); }
V64 weft_uniform_64(Builder* b, int ptr) { return inst(b, UNIFORM,64,uniform_64, .imm=ptr); }

stage(load_8) {
    int8_t* r = R;
    tail ? memcpy(r, (const int8_t*)ptr[inst->imm] + off, 1*tail)
         : memcpy(r, (const int8_t*)ptr[inst->imm] + off, 1*N);
    next(r+N);
}
stage(load_16) {
    int16_t* r = R;
    tail ? memcpy(r, (const int16_t*)ptr[inst->imm] + off, 2*tail)
         : memcpy(r, (const int16_t*)ptr[inst->imm] + off, 2*N);
    next(r+N);
}
stage(load_32) {
    int32_t* r = R;
    tail ? memcpy(r, (const int32_t*)ptr[inst->imm] + off, 4*tail)
         : memcpy(r, (const int32_t*)ptr[inst->imm] + off, 4*N);
    next(r+N);
}
stage(load_64) {
    int64_t* r = R;
    tail ? memcpy(r, (const int64_t*)ptr[inst->imm] + off, 8*tail)
         : memcpy(r, (const int64_t*)ptr[inst->imm] + off, 8*N);
    next(r+N);
}

V8  weft_load_8 (Builder* b, int ptr) { return inst(b, LOAD,8 ,load_8 , .imm=ptr); }
V16 weft_load_16(Builder* b, int ptr) { return inst(b, LOAD,16,load_16, .imm=ptr); }
V32 weft_load_32(Builder* b, int ptr) { return inst(b, LOAD,32,load_32, .imm=ptr); }
V64 weft_load_64(Builder* b, int ptr) { return inst(b, LOAD,64,load_64, .imm=ptr); }

stage(store_8) {
    tail ? memcpy((int8_t*)ptr[inst->imm] + off, v(x), 1*tail)
         : memcpy((int8_t*)ptr[inst->imm] + off, v(x), 1*N);
    next(R);
}
stage(store_8_done) {
    tail ? memcpy((int8_t*)ptr[inst->imm] + off, v(x), 1*tail)
         : memcpy((int8_t*)ptr[inst->imm] + off, v(x), 1*N);
    (void)R;
}
stage(store_16) {
    tail ? memcpy((int16_t*)ptr[inst->imm] + off, v(x), 2*tail)
         : memcpy((int16_t*)ptr[inst->imm] + off, v(x), 2*N);
    next(R);
}
stage(store_16_done) {
    tail ? memcpy((int16_t*)ptr[inst->imm] + off, v(x), 2*tail)
         : memcpy((int16_t*)ptr[inst->imm] + off, v(x), 2*N);
    (void)R;
}
stage(store_32) {
    tail ? memcpy((int32_t*)ptr[inst->imm] + off, v(x), 4*tail)
         : memcpy((int32_t*)ptr[inst->imm] + off, v(x), 4*N);
    next(R);
}
stage(store_32_done) {
    tail ? memcpy((int32_t*)ptr[inst->imm] + off, v(x), 4*tail)
         : memcpy((int32_t*)ptr[inst->imm] + off, v(x), 4*N);
    (void)R;
}
stage(store_64) {
    tail ? memcpy((int64_t*)ptr[inst->imm] + off, v(x), 8*tail)
         : memcpy((int64_t*)ptr[inst->imm] + off, v(x), 8*N);
    next(R);
}
stage(store_64_done) {
    tail ? memcpy((int64_t*)ptr[inst->imm] + off, v(x), 8*tail)
         : memcpy((int64_t*)ptr[inst->imm] + off, v(x), 8*N);
    (void)R;
}

typedef struct { int id; } V0;

#if defined(__aarch64__)
    static char* jit_store_8(char* buf, int d[], int x[], int y[], int z[], int64_t imm) {
        (void)d; (void)y; (void)z;
        buf = add(buf, Rtmp, (int)imm+1, Ri);
        struct {
            uint32_t Rt  : 5;
            uint32_t Rn  : 5;
            uint32_t top : 22;
        } st1b_x0_tmp = {mask(x[0],5), Rtmp, 0x34000};
        return emit(buf, st1b_x0_tmp);
    }
#else
    #define jit_store_8 NULL
#endif

void weft_store_8 (Builder* b, int ptr, V8  x) {
    (void)inst(b,SIDE_EFFECT,0,store_8 , .done=store_8_done , .x=x.id, .imm=ptr, .jit=jit_store_8);
}
void weft_store_16(Builder* b, int ptr, V16 x) {
    (void)inst(b,SIDE_EFFECT,0,store_16, .done=store_16_done, .x=x.id, .imm=ptr);
}
void weft_store_32(Builder* b, int ptr, V32 x) {
    (void)inst(b,SIDE_EFFECT,0,store_32, .done=store_32_done, .x=x.id, .imm=ptr);
}
void weft_store_64(Builder* b, int ptr, V64 x) {
    (void)inst(b,SIDE_EFFECT,0,store_64, .done=store_64_done, .x=x.id, .imm=ptr);
}

stage(assert_8)  { int8_t  *x=v(x); (void)x; each assert(x[i]); next(R); }
stage(assert_16) { int16_t *x=v(x); (void)x; each assert(x[i]); next(R); }
stage(assert_32) { int32_t *x=v(x); (void)x; each assert(x[i]); next(R); }
stage(assert_64) { int64_t *x=v(x); (void)x; each assert(x[i]); next(R); }

void weft_assert_8 (Builder* b, V8  x){inst_(b,(BInst){.fn=assert_8 , .kind=SIDE_EFFECT, .x=x.id});}
void weft_assert_16(Builder* b, V16 x){inst_(b,(BInst){.fn=assert_16, .kind=SIDE_EFFECT, .x=x.id});}
void weft_assert_32(Builder* b, V32 x){inst_(b,(BInst){.fn=assert_32, .kind=SIDE_EFFECT, .x=x.id});}
void weft_assert_64(Builder* b, V64 x){inst_(b,(BInst){.fn=assert_64, .kind=SIDE_EFFECT, .x=x.id});}

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

#define FLOAT_STAGES(B,S,F,M,N0,P1) \
    stage( cast_f##B){S *r=R; F *x=v(x);          each r[i]=(S)   x[i]              ; next(r+N);} \
    stage( cast_s##B){F *r=R; S *x=v(x);          each r[i]=(F)(M)x[i]              ; next(r+N);} \
    stage( ceil_f##B){F *r=R,   *x=v(x);          each r[i]=(F)ceil ((M)x[i])       ; next(r+N);} \
    stage(floor_f##B){F *r=R,   *x=v(x);          each r[i]=(F)floor((M)x[i])       ; next(r+N);} \
    stage( sqrt_f##B){F *r=R,   *x=v(x);          each r[i]=(F)sqrt ((M)x[i])       ; next(r+N);} \
    stage(  add_f##B){F *r=R,   *x=v(x), *y=v(y); each r[i]=(F)((M)x[i] + (M)y[i])  ; next(r+N);} \
    stage(  sub_f##B){F *r=R,   *x=v(x), *y=v(y); each r[i]=(F)((M)x[i] - (M)y[i])  ; next(r+N);} \
    stage(  mul_f##B){F *r=R,   *x=v(x), *y=v(y); each r[i]=(F)((M)x[i] * (M)y[i])  ; next(r+N);} \
    stage(  div_f##B){F *r=R,   *x=v(x), *y=v(y); each r[i]=(F)((M)x[i] / (M)y[i])  ; next(r+N);} \
    stage(   eq_f##B){S *r=R; F *x=v(x), *y=v(y); each r[i]=(M)x[i] == (M)y[i] ?-1:0; next(r+N);} \
    stage(   lt_f##B){S *r=R; F *x=v(x), *y=v(y); each r[i]=(M)x[i] <  (M)y[i] ?-1:0; next(r+N);} \
    stage(   le_f##B){S *r=R; F *x=v(x), *y=v(y); each r[i]=(M)x[i] <= (M)y[i] ?-1:0; next(r+N);} \
                                                                                                  \
    V##B weft_cast_f##B (Builder* b, V##B x) { return inst(b,MATH,B, cast_f##B, .x=x.id); }       \
    V##B weft_cast_s##B (Builder* b, V##B x) { return inst(b,MATH,B, cast_s##B, .x=x.id); }       \
    V##B weft_ceil_f##B (Builder* b, V##B x) { return inst(b,MATH,B, ceil_f##B, .x=x.id); }       \
    V##B weft_floor_f##B(Builder* b, V##B x) { return inst(b,MATH,B,floor_f##B, .x=x.id); }       \
    V##B weft_sqrt_f##B (Builder* b, V##B x) { return inst(b,MATH,B, sqrt_f##B, .x=x.id); }       \
    V##B weft_add_f##B(Builder* b, V##B x, V##B y) {                                              \
        sort_commutative(&x.id, &y.id);                                                           \
        if (is_splat(b,y.id,     0)) { return x; }                                                \
        if (is_splat(b,y.id, (S)N0)) { return x; }                                                \
        if (is_splat(b,x.id,     0)) { return y; }                                                \
        if (is_splat(b,x.id, (S)N0)) { return y; }                                                \
        return inst(b, MATH,B, add_f##B, .x=x.id, .y=y.id);                                       \
    }                                                                                             \
    V##B weft_sub_f##B(Builder* b, V##B x, V##B y) {                                              \
        if (is_splat(b,y.id,     0)) { return x; }                                                \
        if (is_splat(b,y.id, (S)N0)) { return x; }                                                \
        return inst(b, MATH,B, sub_f##B, .x=x.id, .y=y.id);                                       \
    }                                                                                             \
    V##B weft_mul_f##B(Builder* b, V##B x, V##B y) {                                              \
        sort_commutative(&x.id, &y.id);                                                           \
        /* NB: x*0 isn't 0 when x is NaN. */                                                      \
        if (is_splat(b,y.id, P1)) { return x; }                                                   \
        if (is_splat(b,x.id, P1)) { return y; }                                                   \
        return inst(b, MATH,B, mul_f##B, .x=x.id, .y=y.id);                                       \
    }                                                                                             \
    V##B weft_div_f##B(Builder* b, V##B x, V##B y) {                                              \
        if (is_splat(b,y.id, P1)) { return x; }                                                   \
        return inst(b, MATH,B, div_f##B, .x=x.id, .y=y.id);                                       \
    }                                                                                             \
    V##B weft_eq_f##B(Builder* b, V##B x, V##B y){sort_commutative(&x.id, &y.id);                 \
                                                  return inst(b,MATH,B,eq_f##B,.x=x.id,.y=y.id);} \
    V##B weft_lt_f##B(Builder* b, V##B x, V##B y){return inst(b,MATH,B,lt_f##B,.x=x.id,.y=y.id);} \
    V##B weft_le_f##B(Builder* b, V##B x, V##B y){return inst(b,MATH,B,le_f##B,.x=x.id,.y=y.id);} \

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
    FLOAT_STAGES(16,int16_t,__fp16, float, 0x8000            , 0x3c00            )
    FLOAT_STAGES(32,int32_t, float, float, 0x80000000        , 0x3f800000        )
    FLOAT_STAGES(64,int64_t,double,double, 0x8000000000000000, 0x3ff0000000000000)
#pragma GCC diagnostic pop

#define INT_STAGES(B,S,U) \
    stage(not_  ##B) {S *r=R, *x=v(x);          each r[i] =     ~x[i]            ; next(r+N);}  \
    stage(shli_i##B) {S *r=R, *x=v(x);          each r[i] =  (S)(x[i]<<inst->imm); next(r+N);}  \
    stage(shri_s##B) {S *r=R, *x=v(x);          each r[i] =      x[i]>>inst->imm ; next(r+N);}  \
    stage(shri_u##B) {U *r=R, *x=v(x);          each r[i] =      x[i]>>inst->imm ; next(r+N);}  \
    stage(shlv_i##B) {S *r=R, *x=v(x), *y=v(y); each r[i] =  (S)(x[i] << y[i])   ; next(r+N);}  \
    stage(shrv_s##B) {S *r=R, *x=v(x), *y=v(y); each r[i] =      x[i] >> y[i]    ; next(r+N);}  \
    stage(shrv_u##B) {U *r=R, *x=v(x), *y=v(y); each r[i] =      x[i] >> y[i]    ; next(r+N);}  \
    stage(add_i ##B) {U *r=R, *x=v(x), *y=v(y); each r[i] =      x[i] +  y[i]    ; next(r+N);}  \
    stage(sub_i ##B) {U *r=R, *x=v(x), *y=v(y); each r[i] =      x[i] -  y[i]    ; next(r+N);}  \
    stage(mul_i ##B) {U *r=R, *x=v(x), *y=v(y); each r[i] =      x[i] *  y[i]    ; next(r+N);}  \
    stage(and_  ##B) {U *r=R, *x=v(x), *y=v(y); each r[i] =      x[i] &  y[i]    ; next(r+N);}  \
    stage(bic_  ##B) {U *r=R, *x=v(x), *y=v(y); each r[i] =      x[i] & ~y[i]    ; next(r+N);}  \
    stage( or_  ##B) {U *r=R, *x=v(x), *y=v(y); each r[i] =      x[i] |  y[i]    ; next(r+N);}  \
    stage(xor_  ##B) {U *r=R, *x=v(x), *y=v(y); each r[i] =      x[i] ^  y[i]    ; next(r+N);}  \
    stage(eq_i  ##B) {S *r=R, *x=v(x), *y=v(y); each r[i] = x[i]==y[i] ?   -1 : 0; next(r+N);}  \
    stage(lt_s  ##B) {S *r=R, *x=v(x), *y=v(y); each r[i] = x[i]< y[i] ?   -1 : 0; next(r+N);}  \
    stage(lt_u  ##B) {U *r=R, *x=v(x), *y=v(y); each r[i] = x[i]< y[i] ?(U)-1 : 0; next(r+N);}  \
    stage(le_s  ##B) {S *r=R, *x=v(x), *y=v(y); each r[i] = x[i]<=y[i] ?   -1 : 0; next(r+N);}  \
    stage(le_u  ##B) {U *r=R, *x=v(x), *y=v(y); each r[i] = x[i]<=y[i] ?(U)-1 : 0; next(r+N);}  \
    stage(sel_  ##B) {                                                                          \
        U *r=R, *x=v(x), *y=v(y), *z=v(z);                                                      \
        each r[i] = ( x[i] & y[i])                                                              \
                  | (~x[i] & z[i]);                                                             \
        next(r+N);                                                                              \
    }                                                                                           \
                                                                                                \
    V##B weft_not_ ##B(Builder* b, V##B x) { return inst(b, MATH,B,not_##B, .x=x.id); }         \
    V##B weft_add_i##B(Builder* b, V##B x, V##B y) {                                            \
        sort_commutative(&x.id, &y.id);                                                         \
        if (is_splat(b,y.id, 0)) { return x; }                                                  \
        if (is_splat(b,x.id, 0)) { return y; }                                                  \
        return inst(b, MATH,B,add_i##B, .x=x.id, .y=y.id);                                      \
    }                                                                                           \
    V##B weft_sub_i##B(Builder* b, V##B x, V##B y) {                                            \
        if (x.id == y.id) { return weft_splat_##B(b,0); }                                       \
        if (is_splat(b,y.id, 0)) { return x; }                                                  \
        return inst(b, MATH,B,sub_i##B, .x=x.id, .y=y.id);                                      \
    }                                                                                           \
    V##B weft_mul_i##B(Builder* b, V##B x, V##B y) {                                            \
        sort_commutative(&x.id, &y.id);                                                         \
        if (is_splat(b,y.id, 0)) { return y; }                                                  \
        if (is_splat(b,x.id, 0)) { return x; }                                                  \
        if (is_splat(b,y.id, 1)) { return x; }                                                  \
        if (is_splat(b,x.id, 1)) { return y; }                                                  \
        return inst(b, MATH,B,mul_i##B, .x=x.id, .y=y.id);                                      \
    }                                                                                           \
    V##B weft_shl_i##B(Builder* b, V##B x, V##B y) {                                            \
        if (is_splat(b,y.id,0)) { return x; }                                                   \
        for (int64_t imm; any_splat(b,y.id,&imm);) {                                            \
            return inst(b, MATH,B,shli_i##B, .x=x.id, .imm=imm);                                \
        }                                                                                       \
        return inst(b, MATH,B,shlv_i##B, .x=x.id, .y=y.id);                                     \
    }                                                                                           \
    V##B weft_shr_s##B(Builder* b, V##B x, V##B y) {                                            \
        if (is_splat(b,y.id,0)) { return x; }                                                   \
        for (int64_t imm; any_splat(b,y.id,&imm);) {                                            \
            return inst(b, MATH,B,shri_s##B, .x=x.id, .imm=imm);                                \
        }                                                                                       \
        return inst(b, MATH,B,shrv_s##B, .x=x.id, .y=y.id);                                     \
    }                                                                                           \
    V##B weft_shr_u##B(Builder* b, V##B x, V##B y) {                                            \
        if (is_splat(b,y.id,0)) { return x; }                                                   \
        for (int64_t imm; any_splat(b,y.id,&imm);) {                                            \
            return inst(b, MATH,B,shri_u##B, .x=x.id, .imm=imm);                                \
        }                                                                                       \
        return inst(b, MATH,B,shrv_u##B, .x=x.id, .y=y.id);                                     \
    }                                                                                           \
    V##B weft_and_##B(Builder* b, V##B x, V##B y) {                                             \
        sort_commutative(&x.id, &y.id);                                                         \
        if (x.id == y.id) { return x; }                                                         \
        if (is_splat(b,y.id, 0)) { return y; }                                                  \
        if (is_splat(b,x.id, 0)) { return x; }                                                  \
        if (is_splat(b,y.id,-1)) { return x; }                                                  \
        if (is_splat(b,x.id,-1)) { return y; }                                                  \
        return inst(b, MATH,B,and_##B, .x=x.id, .y=y.id);                                       \
    }                                                                                           \
    V##B weft_or_##B(Builder* b, V##B x, V##B y) {                                              \
        sort_commutative(&x.id, &y.id);                                                         \
        if (x.id == y.id) { return x; }                                                         \
        if (is_splat(b,y.id, 0)) { return x; }                                                  \
        if (is_splat(b,x.id, 0)) { return y; }                                                  \
        if (is_splat(b,y.id,-1)) { return y; }                                                  \
        if (is_splat(b,x.id,-1)) { return x; }                                                  \
        return inst(b, MATH,B,or_##B, .x=x.id, .y=y.id);                                        \
    }                                                                                           \
    V##B weft_xor_##B(Builder* b, V##B x, V##B y) {                                             \
        sort_commutative(&x.id, &y.id);                                                         \
        if (x.id == y.id) { return weft_splat_##B(b,0); }                                       \
        if (is_splat(b,y.id, 0)) { return x; }                                                  \
        if (is_splat(b,x.id, 0)) { return y; }                                                  \
        return inst(b, MATH,B,xor_##B, .x=x.id, .y=y.id);                                       \
    }                                                                                           \
    V##B weft_sel_##B(Builder* b, V##B x, V##B y, V##B z) {                                     \
        if (is_splat(b,x.id, 0)) { return z; }                                                  \
        if (is_splat(b,x.id,-1)) { return y; }                                                  \
        if (is_splat(b,z.id, 0)) { return weft_and_##B(b,x,y); }                                \
        if (is_splat(b,y.id, 0)) { return inst(b, MATH,B,bic_##B, .x=z.id, .y=x.id); }          \
        return inst(b, MATH,B,sel_##B, .x=x.id, .y=y.id, .z=z.id);                              \
    }                                                                                           \
    V##B weft_eq_i##B(Builder* b, V##B x, V##B y) {                                             \
        sort_commutative(&x.id, &y.id);                                                         \
        if (x.id == y.id) { return weft_splat_##B(b,-1); }                                      \
        return inst(b, MATH,B,eq_i##B, .x=x.id, .y=y.id);                                       \
    }                                                                                           \
    V##B weft_lt_s##B(Builder* b, V##B x, V##B y) {                                             \
        if (x.id == y.id) { return weft_splat_##B(b, 0); }                                      \
        return inst(b, MATH,B,lt_s##B, .x=x.id, .y=y.id);                                       \
    }                                                                                           \
    V##B weft_lt_u##B(Builder* b, V##B x, V##B y) {                                             \
        if (x.id == y.id) { return weft_splat_##B(b, 0); }                                      \
        return inst(b, MATH,B,lt_u##B, .x=x.id, .y=y.id);                                       \
    }                                                                                           \
    V##B weft_le_s##B(Builder* b, V##B x, V##B y) {                                             \
        if (x.id == y.id) { return weft_splat_##B(b,-1); }                                      \
        return inst(b, MATH,B,le_s##B, .x=x.id, .y=y.id);                                       \
    }                                                                                           \
    V##B weft_le_u##B(Builder* b, V##B x, V##B y) {                                             \
        if (x.id == y.id) { return weft_splat_##B(b,-1); }                                      \
        return inst(b, MATH,B,le_u##B, .x=x.id, .y=y.id);                                       \
    }                                                                                           \

INT_STAGES( 8, int8_t, uint8_t)
INT_STAGES(16,int16_t,uint16_t)
INT_STAGES(32,int32_t,uint32_t)
INT_STAGES(64,int64_t,uint64_t)

stage(narrow_i16) { int8_t  *r=R; int16_t *x=v(x); each r[i] = (int8_t )x[i]; next(r+N); }
stage(narrow_i32) { int16_t *r=R; int32_t *x=v(x); each r[i] = (int16_t)x[i]; next(r+N); }
stage(narrow_i64) { int32_t *r=R; int64_t *x=v(x); each r[i] = (int32_t)x[i]; next(r+N); }
stage(narrow_f32) { __fp16  *r=R; float   *x=v(x); each r[i] = (__fp16 )x[i]; next(r+N); }
stage(narrow_f64) { float   *r=R; double  *x=v(x); each r[i] = (float  )x[i]; next(r+N); }

stage(widen_s8)  {  int16_t *r=R;  int8_t  *x=v(x); each r[i] = ( int16_t)x[i]; next(r+N); }
stage(widen_s16) {  int32_t *r=R;  int16_t *x=v(x); each r[i] = ( int32_t)x[i]; next(r+N); }
stage(widen_s32) {  int64_t *r=R;  int32_t *x=v(x); each r[i] = ( int64_t)x[i]; next(r+N); }
stage(widen_u8)  { uint16_t *r=R; uint8_t  *x=v(x); each r[i] = (uint16_t)x[i]; next(r+N); }
stage(widen_u16) { uint32_t *r=R; uint16_t *x=v(x); each r[i] = (uint32_t)x[i]; next(r+N); }
stage(widen_u32) { uint64_t *r=R; uint32_t *x=v(x); each r[i] = (uint64_t)x[i]; next(r+N); }
stage(widen_f16) { float    *r=R; __fp16   *x=v(x); each r[i] = (float   )x[i]; next(r+N); }
stage(widen_f32) { double   *r=R; float    *x=v(x); each r[i] = (double  )x[i]; next(r+N); }

V8  weft_narrow_i16(Builder* b, V16 x) { return inst(b, MATH, 8,narrow_i16, .x=x.id); }
V16 weft_narrow_i32(Builder* b, V32 x) { return inst(b, MATH,16,narrow_i32, .x=x.id); }
V32 weft_narrow_i64(Builder* b, V64 x) { return inst(b, MATH,32,narrow_i64, .x=x.id); }
V16 weft_narrow_f32(Builder* b, V32 x) { return inst(b, MATH,16,narrow_f32, .x=x.id); }
V32 weft_narrow_f64(Builder* b, V64 x) { return inst(b, MATH,32,narrow_f64, .x=x.id); }

V16 weft_widen_s8 (Builder* b, V8  x) { return inst(b, MATH,16,widen_s8 , .x=x.id); }
V32 weft_widen_s16(Builder* b, V16 x) { return inst(b, MATH,32,widen_s16, .x=x.id); }
V64 weft_widen_s32(Builder* b, V32 x) { return inst(b, MATH,64,widen_s32, .x=x.id); }
V16 weft_widen_u8 (Builder* b, V8  x) { return inst(b, MATH,16,widen_u8 , .x=x.id); }
V32 weft_widen_u16(Builder* b, V16 x) { return inst(b, MATH,32,widen_u16, .x=x.id); }
V64 weft_widen_u32(Builder* b, V32 x) { return inst(b, MATH,64,widen_u32, .x=x.id); }
V32 weft_widen_f16(Builder* b, V16 x) { return inst(b, MATH,32,widen_f16, .x=x.id); }
V64 weft_widen_f32(Builder* b, V32 x) { return inst(b, MATH,64,widen_f32, .x=x.id); }

static bool assign_reg(int reg[32], int frag, int* r) {
    for (int i = 0; i < 32; i++) {
        if (reg[i] == 0) {
            reg[i] = frag;
            *r = i;
            return true;
        }
    }
    return false;
}

static int must_find_frag(const int reg[32], int frag) {
    for (int i = 0; i < 32; i++) {
        if (reg[i] == frag) {
            return i;
        }
    }
    assert(false);
    return -1;
}

extern bool weft_jit_debug_break;
bool weft_jit_debug_break = false;

#if defined(__aarch64__)
    static char* jit_setup(char* buf, int reg[32]) {
        for (int i = 8; i < 16; i++) { reg[i] = -1; }  // v8-v15 are callee saved.

        if (weft_jit_debug_break) {
            buf = emit4(buf, 0xd43e0000);
        }
        struct {
            uint32_t Rd  :  5;
            uint32_t top : 27;
        } inst = {Ri, 0x550f81f};  // mov i, xzr
        return emit(buf, inst);
    }
    static char* jit_loop(char* buf, char* top) {
        buf = emit4(buf, 0x91000508);  // add  i,i,1
        buf = emit4(buf, 0xf1000400);  // subs n,n,1

        struct {
            uint32_t cond :  4;
            uint32_t zero :  1;
             int32_t span : 19;
            uint32_t high :  8;
        } bne_top = {0x1/*ne*/, 0, (int)(top-buf)/4, 0x54};
        buf = emit(buf, bne_top);

        return emit4(buf, 0xd65f03c0); // ret lr
    }
#else
    static char* jit_setup(char* buf, int reg[32]) { (void)reg;   return buf; }
    static char* jit_loop (char* buf, char* label) { (void)label; return buf; }
#endif

size_t weft_jit(const Builder* b, void* vbuf) {
    char scratch[64];
    size_t len = 0;

    int reg[32] = {0};
    {
        char* buf = vbuf ? vbuf : scratch;
        char* next = jit_setup(buf, reg);
        len += (size_t)(next - buf);
        vbuf = vbuf ? next : vbuf;
    }

    void* const top = vbuf;
    for (int i = 0; i < b->inst_len; i++) {
        const BInst inst = b->inst[i];
        if (!inst.jit) {
            return 0;
        }

        // 1-based value IDs throughout, leaving 0 as an empty register, -1 as a reserved register.
        const int id = i+1;

        // We split each value into fragments that fit in a register:
        //    V8:  1 fragment  in 1 register (lower half)
        //    V16: 1 fragment  in 1 register
        //    V32: 2 fragments in 2 registers
        //    V64: 4 fragments in 4 registers

        // Find registers to hold inst's value.  TODO: spill to stack instead of failing.
        int d[4],x[4],y[4],z[4];
        if (inst.slots >= 1 && !assign_reg(reg, 4*id+0, d+0)) { return 0; }
        if (inst.slots >= 4 && !assign_reg(reg, 4*id+1, d+1)) { return 0; }
        if (inst.slots >= 8 && !assign_reg(reg, 4*id+2, d+2)) { return 0; }
        if (inst.slots >= 8 && !assign_reg(reg, 4*id+3, d+3)) { return 0; }

        if (inst.x) {
            if (b->inst[inst.x-1].slots >= 1) { x[0] = must_find_frag(reg, 4*inst.x+0); }
            if (b->inst[inst.x-1].slots >= 4) { x[1] = must_find_frag(reg, 4*inst.x+1); }
            if (b->inst[inst.x-1].slots >= 8) { x[2] = must_find_frag(reg, 4*inst.x+2);
                                                x[3] = must_find_frag(reg, 4*inst.x+3); }
        }
        if (inst.y) {
            if (b->inst[inst.y-1].slots >= 1) { y[0] = must_find_frag(reg, 4*inst.y+0); }
            if (b->inst[inst.y-1].slots >= 4) { y[1] = must_find_frag(reg, 4*inst.y+1); }
            if (b->inst[inst.y-1].slots >= 8) { y[2] = must_find_frag(reg, 4*inst.y+2);
                                                y[3] = must_find_frag(reg, 4*inst.y+3); }
        }
        if (inst.z) {
            if (b->inst[inst.z-1].slots >= 1) { z[0] = must_find_frag(reg, 4*inst.z+0); }
            if (b->inst[inst.z-1].slots >= 4) { z[1] = must_find_frag(reg, 4*inst.z+1); }
            if (b->inst[inst.z-1].slots >= 8) { z[2] = must_find_frag(reg, 4*inst.z+2);
                                                z[3] = must_find_frag(reg, 4*inst.z+3); }
        }

        char* buf = vbuf ? vbuf : scratch;
        char* next = inst.jit(buf, d,x,y,z, inst.imm);
        len += (size_t)(next - buf);
        vbuf = vbuf ? next : vbuf;

        // TODO: free up registers holding fragments of dead values.
    }

    char* buf = vbuf ? vbuf : scratch;
    char* next = jit_loop(buf, top);
    len += (size_t)(next - buf);
    vbuf = vbuf ? next : vbuf;

    return len;
}
