local ffi = require('ffi')
local Resource = require('sve.resources.resource')
local types = require('sve.resources.gl.types')

---
---@class VAO : Resource
---
---@field handle integer OpenGL handle to the VAO
local VAO = {}
VAO.__index = VAO
setmetatable(VAO, { __index = Resource })

--- @class ffi.namespace*
ffi.cdef [[
void (*glad_glCreateVertexArrays)(GLsizei n, GLuint* arrays);
void (*glad_glDeleteVertexArrays)(GLsizei n, GLuint* arrays);
void (*glad_glBindVertexArray)(GLuint array);
]]

local C = ffi.C

---Create a new OpenGL vertex array
---@param name string The resource name
---@return VAO
function VAO.new(name)
  local p_handle = types.pGLuint()
  C.glad_glCreateVertexArrays(types.GLsizei(1), p_handle)
  local handle = p_handle[0]
  assert(handle ~= 0)

  return Resource.new({
    name = name,
    handle = handle,
  }, VAO)
end

function VAO:bind()
  C.glad_glBindVertexArray(self.handle)
end

function VAO:free()
  local p_handle = types.pGLuint(self.handle)
  print(self.handle)
  C.glad_glDeleteVertexArrays(types.GLsizei(1), p_handle)
end

return VAO
