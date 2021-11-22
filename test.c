#include "weft.h"
#include <assert.h>

typedef weft_Builder Builder;
typedef weft_Program Program;

static void test_nothing() {
    Program* p;
    {
        Builder* b = weft_builder();
        p = weft_compile(b);
    }
    weft_drop(p);
}

int main(void) {
    test_nothing();
    return 0;
}
