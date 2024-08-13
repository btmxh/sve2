local gltypes = require("sve.gl.types")

--- @alias GLenum ffi.cdata*
--- @alias GLbitfield ffi.cdata*
return {
  GL_COLOR_BUFFER_BIT = gltypes.GLbitfield(0x4000),
  GL_DEPTH_BUFFER_BIT = gltypes.GLbitfield(0x100),
  GL_STENCIL_BUFFER_BIT = gltypes.GLbitfield(0x2000),

  GL_LINES = gltypes.GLenum(0x0001),
  GL_LINE_LOOP = gltypes.GLenum(0x0002),
  GL_LINE_STRIP = gltypes.GLenum(0x0003),
  GL_TRIANGLES = gltypes.GLenum(0x0004),
  GL_TRIANGLE_STRIP = gltypes.GLenum(0x0005),

  GL_TEXTURE_2D = gltypes.GLenum(0x0DE1),
  GL_TEXTURE_2D_ARRAY = gltypes.GLenum(0x8C1A),
  GL_TEXTURE0 = gltypes.GLenum(0x84C0),

  GL_BLEND = gltypes.GLenum(0x0BE2),
  GL_SRC_ALPHA = gltypes.GLenum(0x0302),
  GL_ONE_MINUS_SRC_ALPHA = gltypes.GLenum(0x0303),
}
