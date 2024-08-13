local ffi = require("ffi")

--- @class ffi.namespace*
ffi.cdef [[
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef signed char GLbyte;
typedef unsigned char GLubyte;
typedef signed short GLshort;
typedef unsigned short GLushort;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef char GLchar;
]]

return {
  GLboolean = ffi.typeof("GLboolean"),
  GLbitfield = ffi.typeof("GLbitfield"),
  GLbyte = ffi.typeof("GLbyte"),
  GLubyte = ffi.typeof("GLubyte"),
  GLshort = ffi.typeof("GLshort"),
  GLushort = ffi.typeof("GLushort"),
  GLint = ffi.typeof("GLint"),
  GLuint = ffi.typeof("GLuint"),
  GLfloat = ffi.typeof("GLfloat"),
  GLclampf = ffi.typeof("GLclampf"),
  GLdouble = ffi.typeof("GLdouble"),
  GLclampd = ffi.typeof("GLclampd"),
  GLchar = ffi.typeof("GLchar"),
  GLenum = ffi.typeof("GLenum"),
  GLsizei = ffi.typeof("GLsizei"),

  pGLuint = ffi.typeof("GLuint[1]"),
  aGLenum = ffi.typeof("GLenum[?]"),
}
