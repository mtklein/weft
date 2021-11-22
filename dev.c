#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#define CC     "clang "
#define CFLAGS "-O2 -Xclang -nostdsysteminc -Weverything -Werror -c -o "

static bool call(const char* cmd) {
    printf("%s\n", cmd);
    return system(cmd) == 0;
}

int main(void) {
    mkdir("out", 0755);
    return call(CC CFLAGS "out/weft.o weft.c")
        && call(CC CFLAGS "out/test.o test.c")
        && call(CC "out/test.o out/weft.o -o out/test")
        && call("out/test")
        && call("git add -u")
        ? 0 : 1;
}
