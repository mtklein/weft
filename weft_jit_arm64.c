#include "weft_jit.h"
#include <string.h>

// x0:    n
// x1-x7: ptr0-ptr6
// x8:    i
// x9:    tmp

//#define mask(bits) ((1<<bits)-1)
#define emit(buf,inst) (memcpy(buf, &inst, sizeof inst), buf+sizeof(inst))

static char* emit4(char* buf, uint32_t inst) { return emit(buf, inst); }

uint32_t weft_regs() {
    return 0xffff00ff;   // v0-7 and v16-31 are free to use (v8-v15 are callee saved).
}

char* weft_emit_loop(char* buf, char* top) {
    buf = emit4(buf, 0xf1000400/*subs x0,x0,1 == subs n,n,1*/);

    struct {
        uint32_t cond :  4;
        uint32_t zero :  1;
         int32_t span : 19;
        uint32_t high :  8;
    } bne_top = {0x1/*ne*/, 0, (int)(top-buf)/4, 0x54};
    buf = emit(buf, bne_top);

    return emit4(buf, 0xd65f03c0/*ret lr*/);
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
