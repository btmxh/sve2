#pragma once

#include "sve2/utils/types.h"

// macro variants, type-safe
#define SVE2_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define SVE2_MAX(a, b) (((a) > (b)) ? (a) : (b))

// special support for functions
static inline i64 sve2_min_i64(i64 a, i64 b) { return SVE2_MIN(a, b); }
static inline i64 sve2_max_i64(i64 a, i64 b) { return SVE2_MAX(a, b); }
static inline i32 sve2_min_i32(i32 a, i32 b) { return SVE2_MIN(a, b); }
static inline i32 sve2_max_i32(i32 a, i32 b) { return SVE2_MAX(a, b); }
static inline f64 sve2_min_f64(f64 a, f64 b) { return SVE2_MIN(a, b); }
static inline f64 sve2_max_f64(f64 a, f64 b) { return SVE2_MAX(a, b); }
static inline f32 sve2_min_f32(f32 a, f32 b) { return SVE2_MIN(a, b); }
static inline f32 sve2_max_f32(f32 a, f32 b) { return SVE2_MAX(a, b); }
