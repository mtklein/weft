#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static bool call(const char* fmt, ...) {
    va_list va;
    va_start(va,fmt);

    char* cmd = NULL;
    vasprintf(&cmd, fmt, va);
    int rc = system(cmd);

    if (rc) {
        fprintf(stderr, "%s #returned %d\n", cmd, rc);
    }

    free(cmd);
    va_end(va);

    return rc == 0;
}

static bool mode(const char* mode, const char* cc, const char* cflags) {
    return call("mkdir -p out/%s", mode)
        && call("%s %s -o out/%s/weft.o -c weft.c", cc, cflags, mode)
        && call("%s %s -o out/%s/test.o -c test.c", cc, cflags, mode)
        && call("%s out/%s/weft.o out/%s/test.o -o out/%s/test", cc, mode, mode, mode)
        && call("out/%s/test", mode);
}

int main(void) {
    return mode("opt", "clang"
                      , "-g -O2 -Xclang -nostdsysteminc -Weverything -Werror")
        && mode("asan", "clang -fsanitize=address,integer,undefined -fno-sanitize-recover=all"
                      , "-g -O0 -Xclang -nostdsysteminc -Weverything -Werror")
        && call("git add -u")
        ? 0 : 1;
}
