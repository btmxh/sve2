local ffi = require("ffi")
local types = require("sve.resources.gl.types")
local Resource = require("sve.resources.resource")
local ctypes = require("sve.ctypes")
local gltypes = require("sve.resources.gl.types")
local ctx = require("sve.context")

--- @class ffi.namespace*
ffi.cdef [[
typedef struct shader_manager_t shader_manager_t;
typedef struct shader_t shader_t;

shader_t* shader_new(context_t* c, GLenum shader_types[], const char* shader_paths[], i32 num_shaders);
void shader_free(shader_t* s);
i32 shader_use(shader_t* s);
GLuint shader_get_program(shader_t* s);
]]

local C = ffi.C
local GL_VERTEX_SHADER = types.GLenum(0x8B31)
local GL_FRAGMENT_SHADER = types.GLenum(0x8B30)
local GL_COMPUTE_SHADER = types.GLenum(0x91B9)
local ap_const_char = ffi.typeof("const char*[?]")

---@class Shader : Resource
---
---@field handle ffi.ctype*
local Shader = {}
Shader.__index = Shader
setmetatable(Shader, Resource)

---Create a new vertex-fragment shader program
---@param name string the shader name
---@param vertex string path to the vertex shader (relative to `shaders/`)
---@param fragment string path to the fragment shader (relative to `shaders/`)
---@return Shader
function Shader.new_vf(name, vertex, fragment)
  return Resource.new({
    name = name,
    handle = C.shader_new(
      ctx.get(),
      gltypes.aGLenum(2, GL_VERTEX_SHADER, GL_FRAGMENT_SHADER),
      ap_const_char(2, vertex, fragment),
      ctypes.i32(2)
    ),
  }, Shader)
end

---Create a new compute shader program
---@param name string the shader name
---@param compute string path to the compute shader (relative to `shaders/`)
---@return Shader
function Shader.new_c(name, compute)
  return Resource.new({
    name = name,
    handle = C.shader_new(
      ctx.get(),
      gltypes.aGLenum(1, GL_COMPUTE_SHADER),
      ap_const_char(1, compute),
      ctypes.i32(1)
    ),
  }, Shader)
end

function Shader:use()
  return C.shader_use(self.handle)
end

function Shader:free()
  return C.shader_free(self.handle)
end

--- @return integer
function Shader:get_program()
  return C.shader_get_program(self.handle)
end

--- @generic T
--- @param fn fun(version: integer, program: integer): T | nil
--- @return T | nil
function Shader:run(fn)
  local version = self:use()
  if version >= 0 then
    return fn(version, self:get_program())
  end

  return nil
end

return Shader
