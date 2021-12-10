#pragma once
#include <stdint.h>

// General Weft usage and object lifetime:
//
//   1) create a weft_Builder with weft_builder();
//   2) define your program using the weft_V{8,16,32,64} types and the many weft_Builder functions;
//   3) pass ownership of your weft_Builder to weft_compile() to create an optimized weft_Program;
//   4) call weft_run() to execute n independent instances of your weft_Program;
//   5) free() your weft_Program.
//
// The weft_V{8,16,32,64} value types represent an unspecified number of independent 8-, 16-, 32-
// or 64-bit lanes.  These types distinguish only the bit width of the values, leaving those bits'
// interpretation to individual weft_Builder functions.  This means bit-punning between values
// of the same bit width (e.g. 32-bit int <-> float) is free and implicit.
//
// All pointers are late-bound: weft_Builder functions take pointers by index into the array of
// pointers you pass later to weft_run().
//
// weft_run() does not mutate the weft_Program or ptr[]; it is safe to call from different threads.

typedef struct weft_Builder weft_Builder;
typedef struct weft_Program weft_Program;

weft_Builder* weft_builder(void);
weft_Program* weft_compile(weft_Builder*);
void          weft_run    (const weft_Program*, int n, void* const ptr[]);

typedef struct { int id; } weft_V8;
typedef struct { int id; } weft_V16;
typedef struct { int id; } weft_V32;
typedef struct { int id; } weft_V64;

// Create values with each lane set to a constant bit pattern.
weft_V8  weft_splat_8 (weft_Builder*, int8_t  bits);
weft_V16 weft_splat_16(weft_Builder*, int16_t bits);
weft_V32 weft_splat_32(weft_Builder*, int32_t bits);
weft_V64 weft_splat_64(weft_Builder*, int64_t bits);

// Load a single scalar from the given pointer into each of a value's lanes.
weft_V8  weft_uniform_8 (weft_Builder*, int ptr);
weft_V16 weft_uniform_16(weft_Builder*, int ptr);
weft_V32 weft_uniform_32(weft_Builder*, int ptr);
weft_V64 weft_uniform_64(weft_Builder*, int ptr);

// Load a value's lanes contiguously from the given pointer.
weft_V8  weft_load_8 (weft_Builder*, int ptr);
weft_V16 weft_load_16(weft_Builder*, int ptr);
weft_V32 weft_load_32(weft_Builder*, int ptr);
weft_V64 weft_load_64(weft_Builder*, int ptr);

// Store a value's lanes contiguously to the given pointer.
void weft_store_8 (weft_Builder*, int ptr, weft_V8 );
void weft_store_16(weft_Builder*, int ptr, weft_V16);
void weft_store_32(weft_Builder*, int ptr, weft_V32);
void weft_store_64(weft_Builder*, int ptr, weft_V64);

// Arithmetic.
weft_V8 weft_add_i8(weft_Builder*, weft_V8, weft_V8);
weft_V8 weft_sub_i8(weft_Builder*, weft_V8, weft_V8);
weft_V8 weft_mul_i8(weft_Builder*, weft_V8, weft_V8);
weft_V8 weft_shl_i8(weft_Builder*, weft_V8, weft_V8);
weft_V8 weft_shr_s8(weft_Builder*, weft_V8, weft_V8);
weft_V8 weft_shr_u8(weft_Builder*, weft_V8, weft_V8);

weft_V16 weft_add_i16(weft_Builder*, weft_V16, weft_V16);
weft_V16 weft_sub_i16(weft_Builder*, weft_V16, weft_V16);
weft_V16 weft_mul_i16(weft_Builder*, weft_V16, weft_V16);
weft_V16 weft_shl_i16(weft_Builder*, weft_V16, weft_V16);
weft_V16 weft_shr_s16(weft_Builder*, weft_V16, weft_V16);
weft_V16 weft_shr_u16(weft_Builder*, weft_V16, weft_V16);

weft_V32 weft_add_i32(weft_Builder*, weft_V32, weft_V32);
weft_V32 weft_sub_i32(weft_Builder*, weft_V32, weft_V32);
weft_V32 weft_mul_i32(weft_Builder*, weft_V32, weft_V32);
weft_V32 weft_shl_i32(weft_Builder*, weft_V32, weft_V32);
weft_V32 weft_shr_s32(weft_Builder*, weft_V32, weft_V32);
weft_V32 weft_shr_u32(weft_Builder*, weft_V32, weft_V32);

weft_V64 weft_add_i64(weft_Builder*, weft_V64, weft_V64);
weft_V64 weft_sub_i64(weft_Builder*, weft_V64, weft_V64);
weft_V64 weft_mul_i64(weft_Builder*, weft_V64, weft_V64);
weft_V64 weft_shl_i64(weft_Builder*, weft_V64, weft_V64);
weft_V64 weft_shr_s64(weft_Builder*, weft_V64, weft_V64);
weft_V64 weft_shr_u64(weft_Builder*, weft_V64, weft_V64);

weft_V8 weft_and_8(weft_Builder*, weft_V8, weft_V8);
weft_V8 weft_or_8 (weft_Builder*, weft_V8, weft_V8);
weft_V8 weft_xor_8(weft_Builder*, weft_V8, weft_V8);
weft_V8 weft_sel_8(weft_Builder*, weft_V8, weft_V8, weft_V8);

weft_V16 weft_and_16(weft_Builder*, weft_V16, weft_V16);
weft_V16 weft_or_16 (weft_Builder*, weft_V16, weft_V16);
weft_V16 weft_xor_16(weft_Builder*, weft_V16, weft_V16);
weft_V16 weft_sel_16(weft_Builder*, weft_V16, weft_V16, weft_V16);

weft_V32 weft_and_32(weft_Builder*, weft_V32, weft_V32);
weft_V32 weft_or_32 (weft_Builder*, weft_V32, weft_V32);
weft_V32 weft_xor_32(weft_Builder*, weft_V32, weft_V32);
weft_V32 weft_sel_32(weft_Builder*, weft_V32, weft_V32, weft_V32);

weft_V64 weft_and_64(weft_Builder*, weft_V64, weft_V64);
weft_V64 weft_or_64 (weft_Builder*, weft_V64, weft_V64);
weft_V64 weft_xor_64(weft_Builder*, weft_V64, weft_V64);
weft_V64 weft_sel_64(weft_Builder*, weft_V64, weft_V64, weft_V64);

weft_V16 weft_add_f16(weft_Builder*, weft_V16, weft_V16);
weft_V16 weft_sub_f16(weft_Builder*, weft_V16, weft_V16);
weft_V16 weft_mul_f16(weft_Builder*, weft_V16, weft_V16);
weft_V16 weft_div_f16(weft_Builder*, weft_V16, weft_V16);

weft_V32 weft_add_f32(weft_Builder*, weft_V32, weft_V32);
weft_V32 weft_sub_f32(weft_Builder*, weft_V32, weft_V32);
weft_V32 weft_mul_f32(weft_Builder*, weft_V32, weft_V32);
weft_V32 weft_div_f32(weft_Builder*, weft_V32, weft_V32);

weft_V64 weft_add_f64(weft_Builder*, weft_V64, weft_V64);
weft_V64 weft_sub_f64(weft_Builder*, weft_V64, weft_V64);
weft_V64 weft_mul_f64(weft_Builder*, weft_V64, weft_V64);
weft_V64 weft_div_f64(weft_Builder*, weft_V64, weft_V64);

weft_V16 weft_ceil_f16 (weft_Builder*, weft_V16);
weft_V16 weft_floor_f16(weft_Builder*, weft_V16);
weft_V16 weft_sqrt_f16 (weft_Builder*, weft_V16);

weft_V32 weft_ceil_f32 (weft_Builder*, weft_V32);
weft_V32 weft_floor_f32(weft_Builder*, weft_V32);
weft_V32 weft_sqrt_f32 (weft_Builder*, weft_V32);

weft_V64 weft_ceil_f64 (weft_Builder*, weft_V64);
weft_V64 weft_floor_f64(weft_Builder*, weft_V64);
weft_V64 weft_sqrt_f64 (weft_Builder*, weft_V64);

weft_V8  weft_narrow_i16(weft_Builder*, weft_V16);
weft_V16 weft_narrow_i32(weft_Builder*, weft_V32);
weft_V32 weft_narrow_i64(weft_Builder*, weft_V64);

weft_V16 weft_widen_s8 (weft_Builder*, weft_V8);
weft_V32 weft_widen_s16(weft_Builder*, weft_V16);
weft_V64 weft_widen_s32(weft_Builder*, weft_V32);

weft_V16 weft_widen_u8 (weft_Builder*, weft_V8);
weft_V32 weft_widen_u16(weft_Builder*, weft_V16);
weft_V64 weft_widen_u32(weft_Builder*, weft_V32);

weft_V16 weft_cast_f16(weft_Builder*, weft_V16);
weft_V32 weft_cast_f32(weft_Builder*, weft_V32);
weft_V64 weft_cast_f64(weft_Builder*, weft_V64);

weft_V16 weft_cast_s16(weft_Builder*, weft_V16);
weft_V32 weft_cast_s32(weft_Builder*, weft_V32);
weft_V64 weft_cast_s64(weft_Builder*, weft_V64);
