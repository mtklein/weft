#include "weft.h"
#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__aarch64__)
    #include <sys/mman.h>
#endif

#define len(arr) (int)(sizeof(arr) / sizeof(*arr))

typedef weft_Builder Builder;
typedef weft_Program Program;
typedef weft_V8      V8;
typedef weft_V16     V16;
typedef weft_V32     V32;
typedef weft_V64     V64;

static size_t store_8 (Builder* b, int ptr, V8  x) { weft_store_8 (b, ptr, x); return  8; }
static size_t store_16(Builder* b, int ptr, V16 x) { weft_store_16(b, ptr, x); return 16; }
static size_t store_32(Builder* b, int ptr, V32 x) { weft_store_32(b, ptr, x); return 32; }
static size_t store_64(Builder* b, int ptr, V64 x) { weft_store_64(b, ptr, x); return 64; }

static void test_nothing() {
    Program* p = NULL;
    {
        Builder* b = weft_builder();
        p = weft_compile(b);
    }
    free(p);
}

static void test_nearly_nothing() {
    Program* p = NULL;
    {
        Builder* b = weft_builder();
        p = weft_compile(b);
    }
    weft_run(p, 31, NULL);
    free(p);
}

static void test_just_about_nothing() {
    Program* p = NULL;
    {
        Builder* b = weft_builder();
        weft_splat_32(b, 0x42);
        p = weft_compile(b);
    }
    weft_run(p, 31, NULL);
    free(p);
}

static void test_memset8() {
    Program* p = NULL;
    {
        Builder* b = weft_builder();
        V8 x = weft_splat_8(b, 0x42);
        weft_store_8(b,0,x);
        p = weft_compile(b);
    }

    int8_t buf[31] = {0};
    weft_run(p, len(buf), (void*[]){buf});

    for (int i = 0; i < len(buf); i++) {
        assert(buf[i] == 0x42);
    }

    free(p);
}
static void test_memset16() {
    Program* p = NULL;
    {
        Builder* b = weft_builder();
        V16 x = weft_splat_16(b, 0x4243);
        weft_store_16(b,0,x);
        p = weft_compile(b);
    }

    int16_t buf[31] = {0};
    weft_run(p, len(buf), (void*[]){buf});

    for (int i = 0; i < len(buf); i++) {
        assert(buf[i] == 0x4243);
    }

    free(p);
}
static void test_memset32() {
    Program* p = NULL;
    {
        Builder* b = weft_builder();
        V32 x = weft_splat_32(b, 0x42431701);
        weft_store_32(b,0,x);
        p = weft_compile(b);
    }

    int32_t buf[31] = {0};
    weft_run(p, len(buf), (void*[]){buf});

    for (int i = 0; i < len(buf); i++) {
        assert(buf[i] == 0x42431701);
    }

    free(p);
}
static void test_memset64() {
    Program* p = NULL;
    {
        Builder* b = weft_builder();
        V64 x = weft_splat_64(b, 0x86753098675309);
        weft_store_64(b,0,x);
        p = weft_compile(b);
    }

    int64_t buf[31] = {0};
    weft_run(p, len(buf), (void*[]){buf});

    for (int i = 0; i < len(buf); i++) {
        assert(buf[i] == 0x86753098675309);
    }

    free(p);
}

static void test_no_tail() {
    Program* p = NULL;
    {
        Builder* b = weft_builder();
        V64 x = weft_splat_64(b, 0x86753098675309);
        weft_store_64(b,0,x);
        p = weft_compile(b);
    }

    int64_t buf[32] = {0};
    weft_run(p, len(buf), (void*[]){buf});

    for (int i = 0; i < len(buf); i++) {
        assert(buf[i] == 0x86753098675309);
    }

    free(p);
}

static void test(size_t (*fn)(Builder*)) {
    Builder* b = weft_builder();
    size_t bits = fn(b);
    Program* p = weft_compile(b);

    __fp16 h[] = {0,1,2,3,4,5,6,7,8};
    float  f[] = {0,1,2,3,4,5,6,7,8};
    double d[] = {0,1,2,3,4,5,6,7,8};

    void* src = h;
    if (bits == 32) { src = f; }
    if (bits == 64) { src = d; }

    double dst[len(h)] = {0};

    int64_t one  = 1;
    __fp16  oneh = (__fp16)1.0f;
    float   onef = 1.0f;
    double  oned = 1.0;

    weft_run(p, len(h), (void*[]){dst,src, &one,&oneh,&onef,&oned});
    free(p);

    if (0 != memcmp(dst,src, (bits/8)*len(h))) {
        dprintf(2, "want:");
        for (size_t i = 0; i < (bits/8)*len(h); i++) {
            dprintf(2, " %02x", ((const uint8_t*)src)[i]);
        }
        dprintf(2, "\ngot: ");
        for (size_t i = 0; i < (bits/8)*len(h); i++) {
            dprintf(2, " %02x", ((const uint8_t*)dst)[i]);
        }
        dprintf(2, "\n");
    }
    assert(0 == memcmp(dst,src,(bits/8)*len(h)));
}

static size_t memcpy8 (Builder* b) { return store_8 (b,0,weft_load_8 (b,1)); }
static size_t memcpy16(Builder* b) { return store_16(b,0,weft_load_16(b,1)); }
static size_t memcpy32(Builder* b) { return store_32(b,0,weft_load_32(b,1)); }
static size_t memcpy64(Builder* b) { return store_64(b,0,weft_load_64(b,1)); }

static size_t store_twice8(Builder* b) {
    V8 one = weft_splat_8(b, 0x1);
    store_8(b,0, weft_xor_8(b, weft_load_8(b,1), one));
    return store_8(b,0, weft_xor_8(b, weft_load_8(b,0), one));
}
static size_t store_twice16(Builder* b) {
    V16 one = weft_splat_16(b, 0x1);
    store_16(b,0, weft_xor_16(b, weft_load_16(b,1), one));
    return store_16(b,0, weft_xor_16(b, weft_load_16(b,0), one));
}
static size_t store_twice32(Builder* b) {
    V32 one = weft_splat_32(b, 0x1);
    store_32(b,0, weft_xor_32(b, weft_load_32(b,1), one));
    return store_32(b,0, weft_xor_32(b, weft_load_32(b,0), one));
}
static size_t store_twice64(Builder* b) {
    V64 one = weft_splat_64(b, 0x1);
    store_64(b,0, weft_xor_64(b, weft_load_64(b,1), one));
    return store_64(b,0, weft_xor_64(b, weft_load_64(b,0), one));
}

static size_t notnot8(Builder* b) {
    V8 x = weft_load_8(b,1);
    x = weft_not_8(b,x);
    x = weft_not_8(b,x);
    return store_8(b,0,x);
}
static size_t notnot16(Builder* b) {
    V16 x = weft_load_16(b,1);
    x = weft_not_16(b,x);
    x = weft_not_16(b,x);
    return store_16(b,0,x);
}
static size_t notnot32(Builder* b) {
    V32 x = weft_load_32(b,1);
    x = weft_not_32(b,x);
    x = weft_not_32(b,x);
    return store_32(b,0,x);
}
static size_t notnot64(Builder* b) {
    V64 x = weft_load_64(b,1);
    x = weft_not_64(b,x);
    x = weft_not_64(b,x);
    return store_64(b,0,x);
}

static size_t uniform8(Builder* b) {
    V8 x   = weft_load_8   (b, 1),
       one = weft_uniform_8(b, 2);
    return store_8(b,0, weft_mul_i8(b,x,one));
}
static size_t uniform16(Builder* b) {
    V16 x   = weft_load_16   (b, 1),
        one = weft_uniform_16(b, 2);
    return store_16(b,0, weft_mul_i16(b,x,one));
}
static size_t uniform32(Builder* b) {
    V32 x   = weft_load_32   (b, 1),
        one = weft_uniform_32(b, 2);
    return store_32(b,0, weft_mul_i32(b,x,one));
}
static size_t uniform64(Builder* b) {
    V64 x   = weft_load_64   (b, 1),
        one = weft_uniform_64(b, 2);
    return store_64(b,0, weft_mul_i64(b,x,one));
}

static size_t roundtrip_f16(Builder* b) {
    return store_16(b,0, weft_cast_s16(b, weft_cast_f16(b, weft_load_16(b,1))));
}
static size_t roundtrip_f32(Builder* b) {
    return store_32(b,0, weft_cast_s32(b, weft_cast_f32(b, weft_load_32(b,1))));
}
static size_t roundtrip_f64(Builder* b) {
    return store_64(b,0, weft_cast_s64(b, weft_cast_f64(b, weft_load_64(b,1))));
}

static size_t arithmetic_f16(Builder* b) {
    V16 one = weft_uniform_16(b, 3);

    V16 x = weft_load_16(b,1);
    x = weft_add_f16(b,x,one);
    x = weft_sub_f16(b,x,one);
    x = weft_div_f16(b,x,one);

    x = weft_mul_f16(b,x,x);
    x = weft_sqrt_f16(b,x);

    return store_16(b,0,x);
}
static size_t arithmetic_f32(Builder* b) {
    V32 one = weft_uniform_32(b, 4);

    V32 x = weft_load_32(b,1);
    x = weft_add_f32(b,x,one);
    x = weft_sub_f32(b,x,one);
    x = weft_div_f32(b,x,one);

    x = weft_mul_f32(b,x,x);
    x = weft_sqrt_f32(b,x);

    return store_32(b,0,x);
}
static size_t arithmetic_f64(Builder* b) {
    V64 one = weft_uniform_64(b, 5);

    V64 x = weft_load_64(b,1);
    x = weft_add_f64(b,x,one);
    x = weft_sub_f64(b,x,one);
    x = weft_div_f64(b,x,one);

    x = weft_mul_f64(b,x,x);
    x = weft_sqrt_f64(b,x);

    return store_64(b,0,x);
}

static size_t special_cases_i8(Builder* b) {
    V8 x = weft_load_8(b,1);

    V8 zero = weft_splat_8(b, 0),
       one  = weft_splat_8(b, 1),
       T    = weft_splat_8(b,-1);

    assert(   x.id == weft_add_i8(b,x,zero).id);
    assert(   x.id == weft_mul_i8(b,x,one ).id);
    assert(zero.id == weft_mul_i8(b,x,zero).id);
    assert(zero.id == weft_sub_i8(b,x,x   ).id);
    assert(   x.id == weft_sub_i8(b,x,zero).id);

    assert(   x.id == weft_shl_i8(b,x,zero).id);
    assert(   x.id == weft_shr_s8(b,x,zero).id);
    assert(   x.id == weft_shr_u8(b,x,zero).id);

    assert(   x.id == weft_and_8(b,x,x   ).id);
    assert(   x.id == weft_and_8(b,x,T   ).id);
    assert(zero.id == weft_and_8(b,x,zero).id);
    assert(   x.id == weft_or_8 (b,x,x   ).id);
    assert(   x.id == weft_or_8 (b,x,zero).id);
    assert(   T.id == weft_or_8 (b,x,T   ).id);
    assert(zero.id == weft_xor_8(b,x,x   ).id);
    assert(   x.id == weft_xor_8(b,x,zero).id);

    V8 y = weft_load_8(b,1);

    assert(   y.id == weft_add_i8(b,zero,y).id);
    assert(   y.id == weft_mul_i8(b,one ,y).id);
    assert(zero.id == weft_mul_i8(b,zero,y).id);

    assert(   y.id == weft_and_8(b,   T,y).id);
    assert(zero.id == weft_and_8(b,zero,y).id);
    assert(   y.id == weft_or_8 (b,zero,y).id);
    assert(   T.id == weft_or_8 (b,   T,y).id);
    assert(   y.id == weft_xor_8(b,zero,y).id);

    assert(x.id == weft_sel_8(b,   T,x,y).id);
    assert(y.id == weft_sel_8(b,zero,x,y).id);

    assert(   T.id == weft_eq_i8(b,x,x).id);
    assert(zero.id == weft_lt_s8(b,x,x).id);
    assert(zero.id == weft_lt_u8(b,x,x).id);

    V8 d = weft_sub_i8(b,x,y);
    assert(d.id != zero.id);
    weft_assert_8(b, weft_eq_i8(b,d,zero));

    return store_8(b,0, weft_add_i8(b,x,d));
}
static size_t special_cases_i16(Builder* b) {
    V16 x = weft_load_16(b,1);

    V16 zero = weft_splat_16(b, 0),
        one  = weft_splat_16(b, 1),
        T    = weft_splat_16(b,-1);

    assert(   x.id == weft_add_i16(b,x,zero).id);
    assert(   x.id == weft_mul_i16(b,x,one ).id);
    assert(zero.id == weft_mul_i16(b,x,zero).id);
    assert(zero.id == weft_sub_i16(b,x,x   ).id);
    assert(   x.id == weft_sub_i16(b,x,zero).id);

    assert(   x.id == weft_shl_i16(b,x,zero).id);
    assert(   x.id == weft_shr_s16(b,x,zero).id);
    assert(   x.id == weft_shr_u16(b,x,zero).id);

    assert(   x.id == weft_and_16(b,x,x   ).id);
    assert(   x.id == weft_and_16(b,x,T   ).id);
    assert(zero.id == weft_and_16(b,x,zero).id);
    assert(   x.id == weft_or_16 (b,x,x   ).id);
    assert(   x.id == weft_or_16 (b,x,zero).id);
    assert(   T.id == weft_or_16 (b,x,T   ).id);
    assert(zero.id == weft_xor_16(b,x,x   ).id);
    assert(   x.id == weft_xor_16(b,x,zero).id);

    V16 y = weft_load_16(b,1);

    assert(   y.id == weft_add_i16(b,zero,y).id);
    assert(   y.id == weft_mul_i16(b,one ,y).id);
    assert(zero.id == weft_mul_i16(b,zero,y).id);

    assert(   y.id == weft_and_16(b,   T,y).id);
    assert(zero.id == weft_and_16(b,zero,y).id);
    assert(   y.id == weft_or_16 (b,zero,y).id);
    assert(   T.id == weft_or_16 (b,   T,y).id);
    assert(   y.id == weft_xor_16(b,zero,y).id);

    assert(x.id == weft_sel_16(b,   T,x,y).id);
    assert(y.id == weft_sel_16(b,zero,x,y).id);

    assert(   T.id == weft_eq_i16(b,x,x).id);
    assert(zero.id == weft_lt_s16(b,x,x).id);
    assert(zero.id == weft_lt_u16(b,x,x).id);

    V16 d = weft_sub_i16(b,x,y);
    assert(d.id != zero.id);
    weft_assert_16(b, weft_eq_i16(b,d,zero));

    return store_16(b,0, weft_add_i16(b,x,d));
}
static size_t special_cases_i32(Builder* b) {
    V32 x = weft_load_32(b,1);

    V32 zero = weft_splat_32(b, 0),
        one  = weft_splat_32(b, 1),
        T    = weft_splat_32(b,-1);

    assert(   x.id == weft_add_i32(b,x,zero).id);
    assert(   x.id == weft_mul_i32(b,x,one ).id);
    assert(zero.id == weft_mul_i32(b,x,zero).id);
    assert(zero.id == weft_sub_i32(b,x,x   ).id);
    assert(   x.id == weft_sub_i32(b,x,zero).id);

    assert(   x.id == weft_shl_i32(b,x,zero).id);
    assert(   x.id == weft_shr_s32(b,x,zero).id);
    assert(   x.id == weft_shr_u32(b,x,zero).id);

    assert(   x.id == weft_and_32(b,x,x   ).id);
    assert(   x.id == weft_and_32(b,x,T   ).id);
    assert(zero.id == weft_and_32(b,x,zero).id);
    assert(   x.id == weft_or_32 (b,x,x   ).id);
    assert(   x.id == weft_or_32 (b,x,zero).id);
    assert(   T.id == weft_or_32 (b,x,T   ).id);
    assert(zero.id == weft_xor_32(b,x,x   ).id);
    assert(   x.id == weft_xor_32(b,x,zero).id);

    V32 y = weft_load_32(b,1);

    assert(   y.id == weft_add_i32(b,zero,y).id);
    assert(   y.id == weft_mul_i32(b,one ,y).id);
    assert(zero.id == weft_mul_i32(b,zero,y).id);

    assert(   y.id == weft_and_32(b,   T,y).id);
    assert(zero.id == weft_and_32(b,zero,y).id);
    assert(   y.id == weft_or_32 (b,zero,y).id);
    assert(   T.id == weft_or_32 (b,   T,y).id);
    assert(   y.id == weft_xor_32(b,zero,y).id);

    assert(x.id == weft_sel_32(b,   T,x,y).id);
    assert(y.id == weft_sel_32(b,zero,x,y).id);

    assert(   T.id == weft_eq_i32(b,x,x).id);
    assert(zero.id == weft_lt_s32(b,x,x).id);
    assert(zero.id == weft_lt_u32(b,x,x).id);

    V32 d = weft_sub_i32(b,x,y);
    assert(d.id != zero.id);
    weft_assert_32(b, weft_eq_i32(b,d,zero));

    return store_32(b,0, weft_add_i32(b,x,d));
}
static size_t special_cases_i64(Builder* b) {
    V64 x = weft_load_64(b,1);

    V64 zero = weft_splat_64(b, 0),
        one  = weft_splat_64(b, 1),
        T    = weft_splat_64(b,-1);

    assert(   x.id == weft_add_i64(b,x,zero).id);
    assert(   x.id == weft_mul_i64(b,x,one ).id);
    assert(zero.id == weft_mul_i64(b,x,zero).id);
    assert(zero.id == weft_sub_i64(b,x,x   ).id);
    assert(   x.id == weft_sub_i64(b,x,zero).id);

    assert(   x.id == weft_shl_i64(b,x,zero).id);
    assert(   x.id == weft_shr_s64(b,x,zero).id);
    assert(   x.id == weft_shr_u64(b,x,zero).id);

    assert(   x.id == weft_and_64(b,x,x   ).id);
    assert(   x.id == weft_and_64(b,x,T   ).id);
    assert(zero.id == weft_and_64(b,x,zero).id);
    assert(   x.id == weft_or_64 (b,x,x   ).id);
    assert(   x.id == weft_or_64 (b,x,zero).id);
    assert(   T.id == weft_or_64 (b,x,T   ).id);
    assert(zero.id == weft_xor_64(b,x,x   ).id);
    assert(   x.id == weft_xor_64(b,x,zero).id);

    V64 y = weft_load_64(b,1);

    assert(   y.id == weft_add_i64(b,zero,y).id);
    assert(   y.id == weft_mul_i64(b,one ,y).id);
    assert(zero.id == weft_mul_i64(b,zero,y).id);

    assert(   y.id == weft_and_64(b,   T,y).id);
    assert(zero.id == weft_and_64(b,zero,y).id);
    assert(   y.id == weft_or_64 (b,zero,y).id);
    assert(   T.id == weft_or_64 (b,   T,y).id);
    assert(   y.id == weft_xor_64(b,zero,y).id);

    assert(x.id == weft_sel_64(b,   T,x,y).id);
    assert(y.id == weft_sel_64(b,zero,x,y).id);

    assert(   T.id == weft_eq_i64(b,x,x).id);
    assert(zero.id == weft_lt_s64(b,x,x).id);
    assert(zero.id == weft_lt_u64(b,x,x).id);

    V64 d = weft_sub_i64(b,x,y);
    assert(d.id != zero.id);
    weft_assert_64(b, weft_eq_i64(b,d,zero));

    return store_64(b,0, weft_add_i64(b,x,d));
}

static size_t special_cases_f16(Builder* b) {
    V16 x = weft_load_16(b,1);

    union { __fp16 f; int16_t bits; } n0 = {-0.0}, p1 = {1.0};
    assert(n0.bits);

    V16 pzero = weft_splat_16(b,       0),
        nzero = weft_splat_16(b, n0.bits),
          one = weft_splat_16(b, p1.bits);

    assert(x.id == weft_add_f16(b, x, pzero).id);
    assert(x.id == weft_add_f16(b, x, nzero).id);
    assert(x.id == weft_sub_f16(b, x, pzero).id);
    assert(x.id == weft_sub_f16(b, x, nzero).id);
    assert(x.id == weft_mul_f16(b, x,   one).id);
    assert(x.id == weft_div_f16(b, x,   one).id);

    V16 y = weft_load_16(b,1);

    assert(y.id == weft_add_f16(b, pzero, y).id);
    assert(y.id == weft_add_f16(b, nzero, y).id);
    assert(y.id == weft_mul_f16(b,   one, y).id);

    return store_16(b,0,x);
}
static size_t special_cases_f32(Builder* b) {
    V32 x = weft_load_32(b,1);

    union { float f; int32_t bits; } n0 = {-0.0}, p1 = {1.0};
    assert(n0.bits);

    V32 pzero = weft_splat_32(b,       0),
        nzero = weft_splat_32(b, n0.bits),
          one = weft_splat_32(b, p1.bits);

    assert(x.id == weft_add_f32(b, x, pzero).id);
    assert(x.id == weft_add_f32(b, x, nzero).id);
    assert(x.id == weft_sub_f32(b, x, pzero).id);
    assert(x.id == weft_sub_f32(b, x, nzero).id);
    assert(x.id == weft_mul_f32(b, x,   one).id);
    assert(x.id == weft_div_f32(b, x,   one).id);

    V32 y = weft_load_32(b,1);

    assert(y.id == weft_add_f32(b, pzero, y).id);
    assert(y.id == weft_add_f32(b, nzero, y).id);
    assert(y.id == weft_mul_f32(b,   one, y).id);

    return store_32(b,0,x);
}
static size_t special_cases_f64(Builder* b) {
    V64 x = weft_load_64(b,1);

    union { double f; int64_t bits; } n0 = {-0.0}, p1 = {1.0};
    assert(n0.bits);

    V64 pzero = weft_splat_64(b,       0),
        nzero = weft_splat_64(b, n0.bits),
          one = weft_splat_64(b, p1.bits);

    assert(x.id == weft_add_f64(b, x, pzero).id);
    assert(x.id == weft_add_f64(b, x, nzero).id);
    assert(x.id == weft_sub_f64(b, x, pzero).id);
    assert(x.id == weft_sub_f64(b, x, nzero).id);
    assert(x.id == weft_mul_f64(b, x,   one).id);
    assert(x.id == weft_div_f64(b, x,   one).id);

    V64 y = weft_load_64(b,1);

    assert(y.id == weft_add_f64(b, pzero, y).id);
    assert(y.id == weft_add_f64(b, nzero, y).id);
    assert(y.id == weft_mul_f64(b,   one, y).id);

    return store_64(b,0,x);
}

static size_t ceil_floor_f16(Builder* b) {
    V16 x = weft_load_16(b,1);

    V16 f = weft_floor_f16(b,x),
        c = weft_sub_f16(b, weft_ceil_f16(b, weft_add_i16(b, x, weft_splat_16(b, 0x1)))
                          , weft_uniform_16(b,3));
    weft_assert_16(b, weft_eq_f16(b,f,c));

    return store_16(b,0, weft_and_16(b, f,c));
}
static size_t ceil_floor_f32(Builder* b) {
    V32 x = weft_load_32(b,1);

    V32 f = weft_floor_f32(b,x),
        c = weft_sub_f32(b, weft_ceil_f32(b, weft_add_i32(b, x, weft_splat_32(b, 0x1)))
                          , weft_uniform_32(b,4));
    weft_assert_32(b, weft_eq_f32(b,f,c));

    return store_32(b,0, weft_and_32(b, f,c));
}
static size_t ceil_floor_f64(Builder* b) {
    V64 x = weft_load_64(b,1);

    V64 f = weft_floor_f64(b,x),
        c = weft_sub_f64(b, weft_ceil_f64(b, weft_add_i64(b, x, weft_splat_64(b, 0x1)))
                          , weft_uniform_64(b,5));
    weft_assert_64(b, weft_eq_f64(b,f,c));

    return store_64(b,0, weft_and_64(b, f,c));
}

static size_t cse(Builder* b) {
    V32 one = weft_splat_32(b, 0x3f800000);

    V32 x = weft_load_32(b,1);
    V32 y = weft_add_f32(b,x,one);
    V32 z = weft_add_f32(b,x,one);
    assert(y.id == z.id);

    return store_32(b,0, weft_sub_f32(b,y,one));
}

static size_t commutative_sorting(Builder* b) {
    V32 one = weft_splat_32(b, 0x3f800000);

    V32 x = weft_load_32(b,1);
    V32 y = weft_add_f32(b,x,one);
    V32 z = weft_add_f32(b,one,x);
    assert(y.id == z.id);

    return store_32(b,0, weft_sub_f32(b,y,one));
}

static size_t uniform_cse(Builder* b) {
    V32 x = weft_uniform_32(b,2),
        y = weft_uniform_32(b,2);
    assert(x.id == y.id);
    return store_32(b,0, weft_load_32(b,1));
}

static size_t no_load_cse(Builder* b) {
    V32 x = weft_load_32(b,1),
        y = weft_load_32(b,1);
    assert(x.id != y.id);
    return store_32(b,0, weft_and_32(b,x,y));
}

static size_t constant_prop8(Builder* b) {
    V8 one  = weft_splat_8(b, 1),
       big  = weft_add_i8(b, one, weft_splat_8(b, 63)),
       same = weft_shr_s8(b, big, weft_splat_8(b, 6));
    assert(same.id == one.id);
    return store_8(b,0, weft_mul_i8(b, weft_load_8(b,1), one));
}
static size_t constant_prop16(Builder* b) {
    V16 one  = weft_splat_16(b, 1),
        big  = weft_add_i16(b, one, weft_splat_16(b, 63)),
        same = weft_shr_s16(b, big, weft_splat_16(b, 6));
    assert(same.id == one.id);
    return store_16(b,0, weft_mul_i16(b, weft_load_16(b,1), one));
}
static size_t constant_prop32(Builder* b) {
    V32 one  = weft_splat_32(b, 1),
        big  = weft_add_i32(b, one, weft_splat_32(b, 63)),
        same = weft_shr_s32(b, big, weft_splat_32(b, 6));
    assert(same.id == one.id);
    return store_32(b,0, weft_mul_i32(b, weft_load_32(b,1), one));
}
static size_t constant_prop64(Builder* b) {
    V64 one  = weft_splat_64(b, 1),
        big  = weft_add_i64(b, one, weft_splat_64(b, 63)),
        same = weft_shr_s64(b, big, weft_splat_64(b, 6));
    assert(same.id == one.id);
    return store_64(b,0, weft_mul_i64(b, weft_load_64(b,1), one));
}

static size_t sel_8(Builder* b) {
    V8 one = weft_splat_8(b, 0x1);

    V8 x = weft_load_8(b,1);
    V8 odd = weft_and_8(b, x, one);

    x = weft_sel_8(b, odd, x
                         , weft_shl_i8(b, weft_shr_u8(b,x,one), one));
    return store_8(b,0, x);
}
static size_t sel_16(Builder* b) {
    V16 one = weft_splat_16(b, 0x1);

    V16 x = weft_load_16(b,1);
    V16 odd = weft_and_16(b, x, one);

    x = weft_sel_16(b, odd, x
                          , weft_shl_i16(b, weft_shr_u16(b,x,one), one));
    return store_16(b,0, x);
}
static size_t sel_32(Builder* b) {
    V32 one = weft_splat_32(b, 0x1);

    V32 x = weft_load_32(b,1);
    V32 odd = weft_and_32(b, x, one);

    x = weft_sel_32(b, odd, x
                          , weft_shl_i32(b, weft_shr_u32(b,x,one), one));
    return store_32(b,0, x);
}
static size_t sel_64(Builder* b) {
    V64 one = weft_splat_64(b, 0x1);

    V64 x = weft_load_64(b,1);
    V64 odd = weft_and_64(b, x, one);

    x = weft_sel_64(b, odd, x
                          , weft_shl_i64(b, weft_shr_u64(b,x,one), one));
    return store_64(b,0, x);
}

static size_t shiftv_8(Builder* b) {
    V8 x    = weft_load_8(b,1),
       zero = weft_splat_8(b,0),
       one  = weft_splat_8(b,1);

    V8 shift = weft_sel_8(b, weft_and_8(b, weft_lt_s8(b,zero,x)
                                         , weft_lt_s8(b,x, weft_splat_8(b, 1<<6)))
                           , one, zero);
    x = weft_shl_i8(b,x,shift);
    x = weft_shr_s8(b,x,shift);
    x = weft_shl_i8(b,x,shift);
    x = weft_shr_u8(b,x,shift);

    return store_8(b,0,x);
}
static size_t shiftv_16(Builder* b) {
    V16 x    = weft_load_16(b,1),
       zero = weft_splat_16(b,0),
       one  = weft_splat_16(b,1);

    V16 shift = weft_sel_16(b, weft_and_16(b, weft_lt_s16(b,zero,x)
                                            , weft_lt_s16(b,x, weft_splat_16(b, 1<<14)))
                             , one, zero);
    x = weft_shl_i16(b,x,shift);
    x = weft_shr_s16(b,x,shift);
    x = weft_shl_i16(b,x,shift);
    x = weft_shr_u16(b,x,shift);

    return store_16(b,0,x);
}
static size_t shiftv_32(Builder* b) {
    V32 x    = weft_load_32(b,1),
       zero = weft_splat_32(b,0),
       one  = weft_splat_32(b,1);

    V32 shift = weft_sel_32(b, weft_and_32(b, weft_lt_s32(b,zero,x)
                                            , weft_lt_s32(b,x, weft_splat_32(b, 1<<30)))
                             , one, zero);
    x = weft_shl_i32(b,x,shift);
    x = weft_shr_s32(b,x,shift);
    x = weft_shl_i32(b,x,shift);
    x = weft_shr_u32(b,x,shift);

    return store_32(b,0,x);
}
static size_t shiftv_64(Builder* b) {
    V64 x    = weft_load_64(b,1),
       zero = weft_splat_64(b,0),
       one  = weft_splat_64(b,1);

    V64 shift = weft_sel_64(b, weft_and_64(b, weft_lt_s64(b,zero,x)
                                            , weft_lt_s64(b,x, weft_splat_64(b, 1ll<<62)))
                             , one, zero);
    x = weft_shl_i64(b,x,shift);
    x = weft_shr_s64(b,x,shift);
    x = weft_shl_i64(b,x,shift);
    x = weft_shr_u64(b,x,shift);

    return store_64(b,0,x);
}

static size_t or_8(Builder* b) {
    V8 x = weft_load_8(b,1),
       y = weft_load_8(b,1);
    return store_8(b,0, weft_or_8(b,x,y));
}
static size_t or_16(Builder* b) {
    V16 x = weft_load_16(b,1),
        y = weft_load_16(b,1);
    return store_16(b,0, weft_or_16(b,x,y));
}
static size_t or_32(Builder* b) {
    V32 x = weft_load_32(b,1),
        y = weft_load_32(b,1);
    return store_32(b,0, weft_or_32(b,x,y));
}
static size_t or_64(Builder* b) {
    V64 x = weft_load_64(b,1),
        y = weft_load_64(b,1);
    return store_64(b,0, weft_or_64(b,x,y));
}

static size_t bic_8(Builder* b) {
    V8 x = weft_load_8(b,1),
       y = weft_load_8(b,1);
    return store_8(b,0, weft_sel_8(b, weft_lt_s8(b,x,y), weft_splat_8(b,0), x));
}
static size_t bic_16(Builder* b) {
    V16 x = weft_load_16(b,1),
        y = weft_load_16(b,1);
    return store_16(b,0, weft_sel_16(b, weft_lt_s16(b,x,y), weft_splat_16(b,0), x));
}
static size_t bic_32(Builder* b) {
    V32 x = weft_load_32(b,1),
        y = weft_load_32(b,1);
    return store_32(b,0, weft_sel_32(b, weft_lt_s32(b,x,y), weft_splat_32(b,0), x));
}
static size_t bic_64(Builder* b) {
    V64 x = weft_load_64(b,1),
        y = weft_load_64(b,1);
    return store_64(b,0, weft_sel_64(b, weft_lt_s64(b,x,y), weft_splat_64(b,0), x));
}

static size_t comparisons_i8(Builder* b) {
    V8 x = weft_load_8(b,1),
       y = weft_load_8(b,1),
       z = weft_add_i8(b,x,weft_splat_8(b,1));

    x = weft_and_8(b,x, weft_eq_i8(b,x,x));
    x = weft_and_8(b,x, weft_eq_i8(b,x,y));
    x = weft_xor_8(b,x, weft_eq_i8(b,x,z));

    x = weft_xor_8(b,x, weft_lt_s8(b,x,x));
    x = weft_xor_8(b,x, weft_lt_s8(b,x,y));
    x = weft_and_8(b,x, weft_lt_s8(b,x,z));
    x = weft_xor_8(b,x, weft_lt_s8(b,z,x));

    x = weft_xor_8(b,x, weft_lt_u8(b,x,x));
    x = weft_xor_8(b,x, weft_lt_u8(b,x,y));
    x = weft_and_8(b,x, weft_lt_u8(b,x,z));
    x = weft_xor_8(b,x, weft_lt_u8(b,z,x));

    x = weft_and_8(b,x, weft_le_s8(b,x,x));
    x = weft_and_8(b,x, weft_le_s8(b,x,y));
    x = weft_and_8(b,x, weft_le_s8(b,x,z));
    x = weft_xor_8(b,x, weft_le_s8(b,z,x));

    x = weft_and_8(b,x, weft_le_u8(b,x,x));
    x = weft_and_8(b,x, weft_le_u8(b,x,y));
    x = weft_and_8(b,x, weft_le_u8(b,x,z));
    x = weft_xor_8(b,x, weft_le_u8(b,z,x));

    return store_8(b,0,x);
}
static size_t comparisons_i16(Builder* b) {
    V16 x = weft_load_16(b,1),
        y = weft_load_16(b,1),
        z = weft_add_i16(b,x,weft_splat_16(b,1));

    x = weft_and_16(b,x, weft_eq_i16(b,x,x));
    x = weft_and_16(b,x, weft_eq_i16(b,x,y));
    x = weft_xor_16(b,x, weft_eq_i16(b,x,z));

    x = weft_xor_16(b,x, weft_lt_s16(b,x,x));
    x = weft_xor_16(b,x, weft_lt_s16(b,x,y));
    x = weft_and_16(b,x, weft_lt_s16(b,x,z));
    x = weft_xor_16(b,x, weft_lt_s16(b,z,x));

    x = weft_xor_16(b,x, weft_lt_u16(b,x,x));
    x = weft_xor_16(b,x, weft_lt_u16(b,x,y));
    x = weft_and_16(b,x, weft_lt_u16(b,x,z));
    x = weft_xor_16(b,x, weft_lt_u16(b,z,x));

    x = weft_and_16(b,x, weft_le_s16(b,x,x));
    x = weft_and_16(b,x, weft_le_s16(b,x,y));
    x = weft_and_16(b,x, weft_le_s16(b,x,z));
    x = weft_xor_16(b,x, weft_le_s16(b,z,x));

    x = weft_and_16(b,x, weft_le_u16(b,x,x));
    x = weft_and_16(b,x, weft_le_u16(b,x,y));
    x = weft_and_16(b,x, weft_le_u16(b,x,z));
    x = weft_xor_16(b,x, weft_le_u16(b,z,x));

    return store_16(b,0,x);
}
static size_t comparisons_i32(Builder* b) {
    V32 x = weft_load_32(b,1),
        y = weft_load_32(b,1),
        z = weft_add_i32(b,x,weft_splat_32(b,1));

    x = weft_and_32(b,x, weft_eq_i32(b,x,x));
    x = weft_and_32(b,x, weft_eq_i32(b,x,y));
    x = weft_xor_32(b,x, weft_eq_i32(b,x,z));

    x = weft_xor_32(b,x, weft_lt_s32(b,x,x));
    x = weft_xor_32(b,x, weft_lt_s32(b,x,y));
    x = weft_and_32(b,x, weft_lt_s32(b,x,z));
    x = weft_xor_32(b,x, weft_lt_s32(b,z,x));

    x = weft_xor_32(b,x, weft_lt_u32(b,x,x));
    x = weft_xor_32(b,x, weft_lt_u32(b,x,y));
    x = weft_and_32(b,x, weft_lt_u32(b,x,z));
    x = weft_xor_32(b,x, weft_lt_u32(b,z,x));

    x = weft_and_32(b,x, weft_le_s32(b,x,x));
    x = weft_and_32(b,x, weft_le_s32(b,x,y));
    x = weft_and_32(b,x, weft_le_s32(b,x,z));
    x = weft_xor_32(b,x, weft_le_s32(b,z,x));

    x = weft_and_32(b,x, weft_le_u32(b,x,x));
    x = weft_and_32(b,x, weft_le_u32(b,x,y));
    x = weft_and_32(b,x, weft_le_u32(b,x,z));
    x = weft_xor_32(b,x, weft_le_u32(b,z,x));

    return store_32(b,0,x);
}
static size_t comparisons_i64(Builder* b) {
    V64 x = weft_load_64(b,1),
        y = weft_load_64(b,1),
        z = weft_add_i64(b,x,weft_splat_64(b,1));

    x = weft_and_64(b,x, weft_eq_i64(b,x,x));
    x = weft_and_64(b,x, weft_eq_i64(b,x,y));
    x = weft_xor_64(b,x, weft_eq_i64(b,x,z));

    x = weft_xor_64(b,x, weft_lt_s64(b,x,x));
    x = weft_xor_64(b,x, weft_lt_s64(b,x,y));
    x = weft_and_64(b,x, weft_lt_s64(b,x,z));
    x = weft_xor_64(b,x, weft_lt_s64(b,z,x));

    x = weft_xor_64(b,x, weft_lt_u64(b,x,x));
    x = weft_xor_64(b,x, weft_lt_u64(b,x,y));
    x = weft_and_64(b,x, weft_lt_u64(b,x,z));
    x = weft_xor_64(b,x, weft_lt_u64(b,z,x));

    x = weft_and_64(b,x, weft_le_s64(b,x,x));
    x = weft_and_64(b,x, weft_le_s64(b,x,y));
    x = weft_and_64(b,x, weft_le_s64(b,x,z));
    x = weft_xor_64(b,x, weft_le_s64(b,z,x));

    x = weft_and_64(b,x, weft_le_u64(b,x,x));
    x = weft_and_64(b,x, weft_le_u64(b,x,y));
    x = weft_and_64(b,x, weft_le_u64(b,x,z));
    x = weft_xor_64(b,x, weft_le_u64(b,z,x));

    return store_64(b,0,x);
}

static size_t comparisons_f16(Builder* b) {
    V16 x = weft_load_16(b,1),
        y = weft_load_16(b,1),
        z = weft_add_i16(b, x, weft_splat_16(b,1));

    x = weft_and_16(b,x, weft_eq_f16(b,x,y));
    x = weft_xor_16(b,x, weft_eq_f16(b,x,z));

    x = weft_xor_16(b,x, weft_lt_f16(b,x,y));
    x = weft_and_16(b,x, weft_lt_f16(b,x,z));
    x = weft_xor_16(b,x, weft_lt_f16(b,z,x));

    x = weft_and_16(b,x, weft_le_f16(b,x,y));
    x = weft_and_16(b,x, weft_le_f16(b,x,z));
    x = weft_xor_16(b,x, weft_le_f16(b,z,x));

    return store_16(b,0,x);
}
static size_t comparisons_f32(Builder* b) {
    V32 x = weft_load_32(b,1),
        y = weft_load_32(b,1),
        z = weft_add_i32(b, x, weft_splat_32(b,1));

    x = weft_and_32(b,x, weft_eq_f32(b,x,y));
    x = weft_xor_32(b,x, weft_eq_f32(b,x,z));

    x = weft_xor_32(b,x, weft_lt_f32(b,x,y));
    x = weft_and_32(b,x, weft_lt_f32(b,x,z));
    x = weft_xor_32(b,x, weft_lt_f32(b,z,x));

    x = weft_and_32(b,x, weft_le_f32(b,x,y));
    x = weft_and_32(b,x, weft_le_f32(b,x,z));
    x = weft_xor_32(b,x, weft_le_f32(b,z,x));

    return store_32(b,0,x);
}
static size_t comparisons_f64(Builder* b) {
    V64 x = weft_load_64(b,1),
        y = weft_load_64(b,1),
        z = weft_add_i64(b, x, weft_splat_64(b,1));

    x = weft_and_64(b,x, weft_eq_f64(b,x,y));
    x = weft_xor_64(b,x, weft_eq_f64(b,x,z));

    x = weft_xor_64(b,x, weft_lt_f64(b,x,y));
    x = weft_and_64(b,x, weft_lt_f64(b,x,z));
    x = weft_xor_64(b,x, weft_lt_f64(b,z,x));

    x = weft_and_64(b,x, weft_le_f64(b,x,y));
    x = weft_and_64(b,x, weft_le_f64(b,x,z));
    x = weft_xor_64(b,x, weft_le_f64(b,z,x));

    return store_64(b,0,x);
}

static size_t narrow_widen_i8(Builder* b) {
    V8 x = weft_load_8(b,1),
       s = weft_narrow_i16(b, weft_widen_s8(b,x)),
       u = weft_narrow_i16(b, weft_widen_u8(b,x));
    return store_8(b,0, weft_and_8(b, s,u));
}
static size_t narrow_widen_i16(Builder* b) {
    V16 x = weft_load_16(b,1),
        s = weft_narrow_i32(b, weft_widen_s16(b,x)),
        u = weft_narrow_i32(b, weft_widen_u16(b,x));
    return store_16(b,0, weft_and_16(b, s,u));
}
static size_t narrow_widen_i32(Builder* b) {
    V32 x = weft_load_32(b,1),
        s = weft_narrow_i64(b, weft_widen_s32(b,x)),
        u = weft_narrow_i64(b, weft_widen_u32(b,x));
    return store_32(b,0, weft_and_32(b, s,u));
}
static size_t narrow_widen_f16(Builder* b) {
    V16 x = weft_load_16(b,1);
    return store_16(b,0, weft_narrow_f32(b, weft_widen_f16(b,x)));
}
static size_t narrow_widen_f32(Builder* b) {
    V32 x = weft_load_32(b,1);
    return store_32(b,0, weft_narrow_f64(b, weft_widen_f32(b,x)));
}

static size_t ternary_constant_prop(Builder* b) {
    V32 x = weft_load_32(b,1);

    // It's weird to pass sel a condition that's neither 0 nor -1,
    // but it's the only way I can find to constant_prop() a ternary.
    V32 y = weft_and_32(b,x, weft_sel_32(b, weft_splat_32(b,      0x0000ffff)
                                          , weft_splat_32(b,      0x0000ffff)
                                          , weft_splat_32(b, (int)0xffff0000)));
    assert(x.id == y.id);

    return store_32(b,0, x);
}
static size_t ternary_not_constant_prop(Builder* b) {
    V32 x = weft_load_32(b,1);

    // Just as above, but the z argument vetos constant prop.
    V32 z = weft_mul_i32(b, weft_splat_32(b, (int)0xffff0000)
                          , weft_uniform_32(b, 2));
    V32 y = weft_and_32(b,x, weft_sel_32(b, weft_splat_32(b, 0x0000ffff)
                                          , weft_splat_32(b, 0x0000ffff)
                                          , z));
    assert(x.id != y.id);

    return store_32(b,0, y);
}

static size_t ternary_loop_dependent(Builder* b) {
    V32 one  = weft_uniform_32(b,2),
        oneF = weft_uniform_32(b,4),
        x    = weft_load_32(b,1);
    return store_32(b,0, weft_sel_32(b, weft_lt_s32(b,oneF,one), oneF, x));
}
static size_t ternary_not_loop_dependent(Builder* b) {
    V32 one  = weft_uniform_32(b,2),
        oneF = weft_uniform_32(b,4),
        x    = weft_load_32(b,1);
    return store_32(b,0, weft_mul_i32(b, x, weft_sel_32(b, weft_lt_s32(b,one,oneF), one, oneF)));
}

static size_t hash_collision(Builder* b) {
    V32 x = weft_splat_32(b,0),
        y = weft_splat_32(b,0x654dd44a);
    assert(x.id != y.id);
    return store_32(b,0, weft_load_32(b,1));
}

static void (*jit(const Builder* b, size_t* len))(int, void*,void*,void*,void*,void*,void*,void*) {
    *len = weft_jit(b,NULL);
#if defined(MAP_ANONYMOUS)
    assert(*len);
    void* buf = mmap(NULL,*len, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1,0);
    assert((uintptr_t)buf != ~(uintptr_t)0);

    assert(*len == weft_jit(b,buf));

    assert(0 == mprotect(buf,*len, PROT_READ|PROT_EXEC));
    return (void(*)(int, void*,void*,void*,void*,void*,void*,void*))buf;
#else
    assert(!*len);
    return NULL;
#endif
}

static void drop(void (*fn)(int, void*,void*,void*,void*,void*,void*,void*), size_t len) {
#if defined(MAP_ANONYMOUS)
    munmap((void*)fn,len);
#else
    assert(!fn && !len);
#endif
}

static void test_jit_loop() {
    Builder* b = weft_builder();

    size_t len = 0;
    void (*fn)(int, void*,void*,void*,void*,void*,void*,void*) = jit(b, &len);
    if (fn) {
        fn(42, NULL,NULL,NULL,NULL,NULL,NULL,NULL);
    }
    drop(fn,len);
    free(weft_compile(b));
}

static void test_jit_splat() {
    Builder* b = weft_builder();
    weft_splat_8 (b, 0x42);
    weft_splat_16(b, 0x4243);
    weft_splat_32(b, 0x42434445);
    weft_splat_64(b, 0x4243444546474849);

    size_t len = 0;
    void (*fn)(int, void*,void*,void*,void*,void*,void*,void*) = jit(b, &len);
    if (fn) {
        fn(42, NULL,NULL,NULL,NULL,NULL,NULL,NULL);
    }
    drop(fn,len);
    free(weft_compile(b));
}

static void test_jit_memset() {
    Builder* b = weft_builder();
    weft_store_8(b,0, weft_splat_8(b, 0x42));

    size_t len = 0;
    void (*fn)(int, void*,void*,void*,void*,void*,void*,void*) = jit(b, &len);
    if (fn) {
        uint8_t dst[42] = {0};
        fn(len(dst), dst,NULL,NULL,NULL,NULL,NULL,NULL);
        for (int i = 0; i < len(dst); i++) {
            assert(dst[i] == 0x42);
        }
    }
    drop(fn,len);
    free(weft_compile(b));
}

extern bool weft_jit_debug_break;

int main(int argc, char** argv) {
    weft_jit_debug_break = argc > 1;
    (void)argv;

    test_nothing();
    test_nearly_nothing();
    test_just_about_nothing();

    test_memset8();
    test_memset16();
    test_memset32();
    test_memset64();

    test_no_tail();

    test(memcpy8);
    test(memcpy16);
    test(memcpy32);
    test(memcpy64);

    test(store_twice8);
    test(store_twice16);
    test(store_twice32);
    test(store_twice64);

    test(notnot8);
    test(notnot16);
    test(notnot32);
    test(notnot64);

    test(uniform8);
    test(uniform16);
    test(uniform32);
    test(uniform64);

    test(roundtrip_f16);
    test(roundtrip_f32);
    test(roundtrip_f64);

    test(arithmetic_f16);
    test(arithmetic_f32);
    test(arithmetic_f64);

    test(special_cases_i8);
    test(special_cases_i16);
    test(special_cases_i32);
    test(special_cases_i64);

    test(special_cases_f16);
    test(special_cases_f32);
    test(special_cases_f64);

    test(ceil_floor_f16);
    test(ceil_floor_f32);
    test(ceil_floor_f64);

    test(cse);
    test(commutative_sorting);
    test(uniform_cse);
    test(no_load_cse);

    test(constant_prop8);
    test(constant_prop16);
    test(constant_prop32);
    test(constant_prop64);

    test(sel_8);
    test(sel_16);
    test(sel_32);
    test(sel_64);

    test(shiftv_8);
    test(shiftv_16);
    test(shiftv_32);
    test(shiftv_64);

    test(or_8);
    test(or_16);
    test(or_32);
    test(or_64);

    test(bic_8);
    test(bic_16);
    test(bic_32);
    test(bic_64);

    test(comparisons_i8);
    test(comparisons_i16);
    test(comparisons_i32);
    test(comparisons_i64);

    test(comparisons_f16);
    test(comparisons_f32);
    test(comparisons_f64);

    test(narrow_widen_i8);
    test(narrow_widen_i16);
    test(narrow_widen_i32);
    test(narrow_widen_f16);
    test(narrow_widen_f32);

    test(ternary_constant_prop);
    test(ternary_not_constant_prop);
    test(ternary_loop_dependent);
    test(ternary_not_loop_dependent);
    test(hash_collision);

    test_jit_loop();
    test_jit_splat();
    test_jit_memset();

    return 0;
}
