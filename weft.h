#pragma once

// General Weft usage and object lifetime:
//
//   1) create a weft_Builder with weft_builder();
//   2) define your program using the weft_V{8,16,32} types and the many weft_Builder functions;
//   3) pass ownership of your weft_Builder to weft_compile() to create an optimized weft_Program;
//   4) call weft_run() to execute n independent instances of your weft_Program;
//   5) free() your weft_Program.
//
// The weft_V{8,16,32} value types represent an unspecified number of independent 8-, 16-, or
// 32-bit lanes.  These types distinguish only the bit width of the values, leaving those bits'
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

// Create values with each lane set to a constant bit pattern.
weft_V8  weft_splat8 (weft_Builder*, int bits);
weft_V16 weft_splat16(weft_Builder*, int bits);
weft_V32 weft_splat32(weft_Builder*, int bits);

// Load a single scalr from the given pointer into each of a value's lanes.
weft_V8  weft_uniform8 (weft_Builder*, int ptr);
weft_V16 weft_uniform16(weft_Builder*, int ptr);
weft_V32 weft_uniform32(weft_Builder*, int ptr);

// Load a value's lanes contiguously from the given pointer.
weft_V8  weft_load8 (weft_Builder*, int ptr);
weft_V16 weft_load16(weft_Builder*, int ptr);
weft_V32 weft_load32(weft_Builder*, int ptr);

// Store a value's lanes contiguously to the given pointer.
void weft_store8 (weft_Builder*, int ptr, weft_V8 );
void weft_store16(weft_Builder*, int ptr, weft_V16);
void weft_store32(weft_Builder*, int ptr, weft_V32);

// Destructuring loads: load 2, 3, or 4 values per lane, deinterlacing them.
// For example, you can use weft_load32x3() to load 3 weft_V32's from an array of
// struct { float x,y,z; }, with val[0] holding each x, val[1] each y, val[2] each z.
struct weft_V8x2  { weft_V8  val[2]; } weft_load8x2(weft_Builder*, int ptr);
struct weft_V8x3  { weft_V8  val[3]; } weft_load8x3(weft_Builder*, int ptr);
struct weft_V8x4  { weft_V8  val[4]; } weft_load8x4(weft_Builder*, int ptr);

struct weft_V16x2 { weft_V16 val[2]; } weft_load16x2(weft_Builder*, int ptr);
struct weft_V16x3 { weft_V16 val[3]; } weft_load16x3(weft_Builder*, int ptr);
struct weft_V16x4 { weft_V16 val[4]; } weft_load16x4(weft_Builder*, int ptr);

struct weft_V32x2 { weft_V32 val[2]; } weft_load32x2(weft_Builder*, int ptr);
struct weft_V32x3 { weft_V32 val[3]; } weft_load32x3(weft_Builder*, int ptr);
struct weft_V32x4 { weft_V32 val[4]; } weft_load32x4(weft_Builder*, int ptr);

// Structuring stores: store 2, 3, or 4 values per lane, interlacing them first.
// In our example above of an array of struct { float x,y,z; } values,
//     weft_V32 x = ..., y = ..., z = ...;
//     weft_store32x3(builder,ptr,x,y,z);
// interlaces the values in x, y, and z, and writes that array of structs back to memory.
void weft_store8x2(weft_Builder, int ptr, weft_V8, weft_V8);
void weft_store8x3(weft_Builder, int ptr, weft_V8, weft_V8, weft_V8);
void weft_store8x4(weft_Builder, int ptr, weft_V8, weft_V8, weft_V8, weft_V8);

void weft_store16x2(weft_Builder, int ptr, weft_V16, weft_V16);
void weft_store16x3(weft_Builder, int ptr, weft_V16, weft_V16, weft_V16);
void weft_store16x4(weft_Builder, int ptr, weft_V16, weft_V16, weft_V16, weft_V16);

void weft_store32x2(weft_Builder, int ptr, weft_V32, weft_V32);
void weft_store32x3(weft_Builder, int ptr, weft_V32, weft_V32, weft_V32);
void weft_store32x4(weft_Builder, int ptr, weft_V32, weft_V32, weft_V32, weft_V32);
