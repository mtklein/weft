#include "weft.h"
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#define len(arr) (int)(sizeof(arr) / sizeof(*arr))
#define iota {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30}

#define assert_eq(x,y) assert((x) <= (y) && (y) <= (x))

typedef weft_Builder Builder;
typedef weft_Program Program;
typedef weft_V8      V8;
typedef weft_V16     V16;
typedef weft_V32     V32;

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
        assert_eq(buf[i], 0x42);
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
        assert_eq(buf[i], 0x4243);
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
        assert_eq(buf[i], 0x42431701);
    }

    free(p);
}

static void test_memset_uniform8() {
    Program* p = NULL;
    {
        Builder* b = weft_builder();
        V8 x = weft_uniform_8(b, 1);
        weft_store_8(b,0,x);
        p = weft_compile(b);
    }

    int8_t uni = 0x42;
    int8_t buf[31] = {0};
    weft_run(p, len(buf), (void*[]){buf, &uni});

    for (int i = 0; i < len(buf); i++) {
        assert_eq(buf[i], 0x42);
    }

    free(p);
}
static void test_memset_uniform16() {
    Program* p = NULL;
    {
        Builder* b = weft_builder();
        V16 x = weft_uniform_16(b, 1);
        weft_store_16(b,0,x);
        p = weft_compile(b);
    }

    int16_t uni = 0x4243;
    int16_t buf[31] = {0};
    weft_run(p, len(buf), (void*[]){buf,&uni});

    for (int i = 0; i < len(buf); i++) {
        assert_eq(buf[i], 0x4243);
    }

    free(p);
}
static void test_memset_uniform32() {
    Program* p = NULL;
    {
        Builder* b = weft_builder();
        V32 x = weft_uniform_32(b, 1);
        weft_store_32(b,0,x);
        p = weft_compile(b);
    }

    int32_t uni = 0x42431701;
    int32_t buf[31] = {0};
    weft_run(p, len(buf), (void*[]){buf, &uni});

    for (int i = 0; i < len(buf); i++) {
        assert_eq(buf[i], 0x42431701);
    }

    free(p);
}

static void test_memcpy8() {
    Program* p = NULL;
    {
        Builder* b = weft_builder();
        V8 x = weft_load_8(b,1);
        weft_store_8(b,0,x);
        p = weft_compile(b);
    }

    int8_t dst[31] = {0};
    int8_t src[len(dst)];
    for (int i = 0; i < len(dst); i++) {
        src[i] = (int8_t)i;
    }
    weft_run(p, len(dst), (void*[]){dst,src});

    for (int i = 0; i < len(dst); i++) {
        assert_eq(dst[i], src[i]);
    }

    free(p);
}
static void test_memcpy16() {
    Program* p = NULL;
    {
        Builder* b = weft_builder();
        V16 x = weft_load_16(b,1);
        weft_store_16(b,0,x);
        p = weft_compile(b);
    }

    int16_t dst[31] = {0};
    int16_t src[31] = iota;
    weft_run(p, len(dst), (void*[]){dst,src});

    for (int i = 0; i < len(dst); i++) {
        assert_eq(dst[i], src[i]);
    }

    free(p);
}
static void test_memcpy32() {
    Program* p = NULL;
    {
        Builder* b = weft_builder();
        V32 x = weft_load_32(b,1);
        weft_store_32(b,0,x);
        p = weft_compile(b);
    }

    int32_t dst[31] = {0};
    int32_t src[31] = iota;
    weft_run(p, len(dst), (void*[]){dst,src});

    for (int i = 0; i < len(dst); i++) {
        assert_eq(dst[i], src[i]);
    }

    free(p);
}

static void test_arithmetic() {
    Program* p = NULL;
    {
        Builder* b = weft_builder();
        V32 one = weft_splat_32(b, 0x3f800000);

        V32 x = weft_load_32(b,1);
        x = weft_add_f32(b,x,one);
        x = weft_sub_f32(b,x,one);
        x = weft_div_f32(b,x,one);

        x = weft_mul_f32(b,x,x);
        x = weft_sqrt_f32(b,x);

        weft_store_32(b,0,x);
        p = weft_compile(b);
    }

    float dst[31] = {0},
          src[31] = iota;
    weft_run(p, len(dst), (void*[]){dst,src});

    for (int i = 0; i < len(dst); i++) {
        assert_eq(dst[i], src[i]);
    }

    free(p);
}

static void test_cse() {
    Program* p = NULL;
    {
        Builder* b = weft_builder();
        V32 one = weft_splat_32(b, 0x3f800000);

        V32 x = weft_load_32(b,1);
        V32 y = weft_add_f32(b,x,one);
        V32 z = weft_add_f32(b,x,one);
        assert_eq(y.id, z.id);

        weft_store_32(b,0,y);
        p = weft_compile(b);
    }

    float dst[31] = {0},
          src[31] = iota;
    weft_run(p, len(dst), (void*[]){dst,src});

    for (int i = 0; i < len(dst); i++) {
        assert_eq(dst[i], src[i]+1);
    }

    free(p);
}

static void test_commutative_sorting() {
    Program* p = NULL;
    {
        Builder* b = weft_builder();
        V32 one = weft_splat_32(b, 0x3f800000);

        V32 x = weft_load_32(b,1);
        V32 y = weft_add_f32(b,x,one);
        V32 z = weft_add_f32(b,one,x);
        assert_eq(y.id, z.id);

        weft_store_32(b,0,y);
        p = weft_compile(b);
    }

    float dst[31] = {0},
          src[31] = iota;
    weft_run(p, len(dst), (void*[]){dst,src});

    for (int i = 0; i < len(dst); i++) {
        assert_eq(dst[i], src[i]+1);
    }

    free(p);
}


int main(void) {
    test_nothing();
    test_nearly_nothing();
    test_memset8();
    test_memset16();
    test_memset32();
    test_memset_uniform8();
    test_memset_uniform16();
    test_memset_uniform32();
    test_memcpy8();
    test_memcpy16();
    test_memcpy32();
    test_arithmetic();
    test_cse();
    test_commutative_sorting();
    return 0;
}
