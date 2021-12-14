#include "weft.h"
#undef NDEBUG
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
int dprintf(int, const char*, ...);

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

int main(void) {
    test_nothing();
    test_nearly_nothing();

    test_memset8();
    test_memset16();
    test_memset32();
    test_memset64();

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

    test(arithmetic_f16);
    test(arithmetic_f32);
    test(arithmetic_f64);

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

    return 0;
}
