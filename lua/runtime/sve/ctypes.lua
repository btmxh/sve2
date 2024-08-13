local ffi = require("ffi")

ffi.cdef [[
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef float f32;
typedef double f64;
]]

return {
  i8 = ffi.typeof("i8"),
  i16 = ffi.typeof("i16"),
  i32 = ffi.typeof("i32"),
  i64 = ffi.typeof("i64"),
  u8 = ffi.typeof("u8"),
  u16 = ffi.typeof("u16"),
  u32 = ffi.typeof("u32"),
  u64 = ffi.typeof("u64"),
  f32 = ffi.typeof("f32"),
  f64 = ffi.typeof("f64"),

  bool = ffi.typeof("bool"),

  pi32 = ffi.typeof("i32[1]"),
  pf32 = ffi.typeof("f32[1]"),

  au8 = ffi.typeof("u8[?]"),

  ppu8 = ffi.typeof("u8* [1]")
}
