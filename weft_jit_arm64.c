#include "weft_jit.h"
#include <string.h>

// x0:    n
// x1-x7: ptr0-ptr6
// x8:    i
// x9:    tmp

static const int Ri = 8,
               Rtmp = 9;

#define mask(x,bits) ((uint32_t)x) & ((1<<bits)-1)
#define emit(buf,inst) (memcpy(buf, &inst, sizeof inst), buf+sizeof(inst))
static char* emit4(char* buf, uint32_t inst) { return emit(buf, inst); }

void weft_init_regs(int reg[32]) {
    for (int i = 8; i < 16; i++) { reg[i] = -1; }  // v8-v15 are callee saved.
}

char* weft_emit_setup(char* buf) {
    if (weft_jit_debug_break) {
        buf = emit4(buf, 0xd43e0000);
    }
    struct {
        uint32_t Rd  :  5;
        uint32_t top : 27;
    } inst = {Ri, 0x550f81f};  // mov i, xzr
    return emit(buf, inst);
}

char* weft_emit_loop(char* buf, char* top) {
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

char* weft_emit_splat_8(char* buf, int d[], int x[], int y[], int z[], int64_t imm) {
    (void)x; (void)y; (void)z;
    buf = movz(buf, Rtmp, imm, 0);     // mov tmp, imm
    return dup(buf, d[0], Rtmp, 1,0);  // dup.8b d[0], tmp
}

char* weft_emit_splat_16(char* buf, int d[], int x[], int y[], int z[], int64_t imm) {
    (void)x; (void)y; (void)z;
    buf = movz(buf, Rtmp, imm, 0);     // mov tmp, imm
    return dup(buf, d[0], Rtmp, 2,1);  // dup.8h d[0],tmp
}

char* weft_emit_splat_32(char* buf, int d[], int x[], int y[], int z[], int64_t imm) {
    (void)x; (void)y; (void)z;
    buf = movz(buf, Rtmp, imm>> 0, 0);  // mov  tmp, imm15:0
    buf = movk(buf, Rtmp, imm>>16, 1);  // movk tmp, imm31:16
    buf =  dup(buf, d[0], Rtmp, 4, 1);  // dup.4s d[0], tmp
    return dup(buf, d[1], Rtmp, 4, 1);  // dup.4s d[1], tmp
}

char* weft_emit_splat_64(char* buf, int d[], int x[], int y[], int z[], int64_t imm) {
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

char* weft_emit_store_8(char* buf, int d[], int x[], int y[], int z[], int64_t imm) {
    (void)d; (void)y; (void)z;
    buf = add(buf, Rtmp, (int)imm+1, Ri);
    struct {
        uint32_t Rt  : 5;
        uint32_t Rn  : 5;
        uint32_t top : 22;
    } st1b_x0_tmp = {mask(x[0],5), Rtmp, 0x34000};
    return emit(buf, st1b_x0_tmp);
}
