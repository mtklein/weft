#include "weft_jit.h"
#include <string.h>

#define mask(bits) ((1<<bits)-1)
#define emit(buf,inst) (memcpy(buf, &inst, sizeof inst), buf+sizeof(inst))

char* weft_emit_splat_8(char* buf, int d[], int x[], int y[], int z[], int64_t imm) {
    (void)x; (void)y; (void)z;
    struct {
        uint32_t i :  8;
        uint32_t d :  8;
        uint32_t t : 16;
    } inst = {imm & mask(8), d[0] & mask(5), 0xface};
    return emit(buf, inst);
}

#if 0
uint32_t* emit_splat_8(uint32_t* buf, int d[], int x[], int y[], int z[], int64_t imm) {
    (void)x;
    (void)y;
    (void)z;
    // mov     w0, imm & 0xff
    // dup.8b  d[0], w0
    return buf;
}
uint32_t* emit_splat_16(uint32_t* buf, int d[], int x[], int y[], int z[], int64_t imm) {
    (void)x;
    (void)y;
    (void)z;
    // mov     w0, imm & 0xffff
    // dup.8h  d[0], w0
    return buf;
}
uint32_t* emit_splat_32(uint32_t* buf, int d[], int x[], int y[], int z[], int64_t imm) {
    (void)x;
    (void)y;
    (void)z;
    // mov     w0, (imm      ) & 0xffff
    // movk    w0, (imm >> 16) & 0xffff
    // dup.4s  d[0], w0
    // orr.16b d[1], d[0],d[0]
    return buf;
}
uint32_t* emit_splat_64(uint32_t* buf, int d[], int x[], int y[], int z[], int64_t imm) {
    (void)x;
    (void)y;
    (void)z;
    // mov     x0, (imm      ) & 0xffff
    // movk    x0, (imm >> 16) & 0xffff
    // movk    x0, (imm >> 32) & 0xffff
    // movk    x0, (imm >> 48) & 0xffff
    // dup.2d  d[0], x0
    // orr.16b d[1], d[0],d[0]
    // orr.16b d[2], d[0],d[0]
    // orr.16b d[3], d[0],d[0]
    return buf;
}
#endif
