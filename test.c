#include "weft.h"
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#define len(arr) (int)(sizeof(arr) / sizeof(*arr))

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
        V8 x = weft_splat8(b, 0x42);
        weft_store8(b,0,x);
        p = weft_compile(b);
    }

    char buf[31] = {0};
    weft_run(p, len(buf), (void*[]){buf});

    for (int i = 0; i < len(buf); i++) {
        assert(buf[i] == 0x42);
    }

    free(p);
}

int main(void) {
    test_nothing();
    test_nearly_nothing();
    test_memset8();
    return 0;
}
