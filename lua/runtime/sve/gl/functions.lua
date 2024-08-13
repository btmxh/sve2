local ffi = require("ffi")
local glt = require("sve.gl.types")
local glc = require("sve.gl.constants")

ffi.cdef [[
// clear
void (*glad_glClearColor)(GLclampf r, GLclampf g, GLclampf b, GLclampf a);
void (*glad_glClear)(GLbitfield mask);
// draw call
void (*glad_glDrawArrays)(GLenum mode, GLint first, GLsizei count);
// features
void (*glad_glEnable)(GLenum cap);
// blending
void (*glad_glBlendFunc)(GLenum sfactor, GLenum dfactor);
// texture
void (*glad_glActiveTexture)(GLenum target);
void (*glad_glBindTexture)(GLenum target, GLuint texture);
// vao
void (*glad_glCreateVertexArrays)(GLsizei n, GLuint* array);
void (*glad_glDeleteVertexArrays)(GLsizei n, GLuint* array);
void (*glad_glBindVertexArrays)(GLuint array);
// program
void (*glad_glUniform1f)(GLint location, GLfloat value);
GLint (*glad_glGetUniformLocation)(GLuint program, const char* name);
]]

local C = ffi.C
local M = {}

--- @param r number
--- @param g number
--- @param b number
--- @param a number
function M.clear_color(r, g, b, a)
  C.glad_glClearColor(glt.GLclampf(r), glt.GLclampf(g), glt.GLclampf(b), glt.GLclampf(a))
end

--- @alias ClearMaskBit "color" | "depth" | "stencil"
--- @param mask ClearMaskBit | table<ClearMaskBit>
function M.clear(mask)
  if type(mask) == "string" then
    mask = { mask }
  end

  local int_mask = glt.GLenum(0)
  for _, bit in pairs(mask) do
    int_mask = int_mask or ({
      color = glc.GL_COLOR_BUFFER_BIT,
      depth = glc.GL_DEPTH_BUFFER_BIT,
      stencil = glc.GL_STENCIL_BUFFER_BIT
    })[bit]
  end

  C.glad_glClear(int_mask)
end

--- @alias PrimitiveType "lines"|"line_loop"|"line_strip"|"triangles"|"triangle_strip"
--- @param mode PrimitiveType
--- @param first number
--- @param count number
function M.draw_arrays(mode, first, count)
  C.glad_glDrawArrays(({
    lines = glc.GL_LINES,
    line_loop = glc.GL_LINE_LOOP,
    line_strip = glc.GL_LINE_STRIP,
    triangles = glc.GL_TRIANGLES,
    triangle_strip = glc.GL_TRIANGLE_STRIP,
  })[mode], glt.GLint(first), glt.GLsizei(count))
end

--- @param cap GLenum
function M.enable(cap)
  C.glad_glEnable(cap)
end

--- @param sfactor GLenum
--- @param dfactor GLenum
function M.blend_func(sfactor, dfactor)
  C.glad_glBlendFunc(sfactor, dfactor)
end

--- @param slot number
function M.active_texture(slot)
  C.glad_glActiveTexture(glc.GL_TEXTURE0 + slot)
end

--- @param target GLenum
--- @param tex ffi.cdata*
function M.bind_texture(target, tex)
  C.glad_glBindTexture(target, tex)
end

--- @return table<ffi.cdata*> vaos
function M.create_vertex_array()
  local pvao = glt.GLuint()
  C.glad_glCreateVertexArrays(glt.GLsizei(1), pvao)
  return pvao[0]
end

--- @param vao ffi.cdata*
function M.delete_vertex_array(vao)
  local pvao = glt.Gluint(vao)
  C.glad_glDeleteVertexArrays(glt.GLsizei(1), pvao)
end

--- @param vao ffi.cdata*
function M.bind_vertex_array(vao)
  C.glad_glBindVertexArrays(vao)
end

--- @param location integer
--- @param value number
function M.uniform1f(location, value)
  C.glad_glUniform1f(glt.GLint(location), glt.GLfloat(value))
end

--- @param program integer
--- @param name string
--- @return integer
function M.get_uniform_location(program, name)
  return C.glad_glGetUniformLocation(glt.GLuint(program), name)
end

return M
