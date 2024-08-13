--- Resource initialization module
---
--- This provides functions to initialize resource objects that register to
--- a specific [ResourceManager]

local VAO = require("sve.resources.gl.vao")
local Shader = require("sve.resources.gl.shader")

local Audio = require("sve.resources.av.audio")
local Video = require("sve.resources.av.video")

--- @param manager ResourceManager the resource manager for this module
return function(manager)
  local M = {}

  --- @param name string the VAO name
  --- @return VAO
  function M.new_vao(name)
    return manager:register(VAO.new(name))
  end

  --- @param name string the program name
  --- @param vertex string path to vertex shader
  --- @param fragment string path to fragment shader
  --- @return Shader
  function M.new_shader_vf(name, vertex, fragment)
    return manager:register(Shader.new_vf(name, vertex, fragment))
  end

  --- @param name string the program name
  --- @param compute string path to compute shader
  --- @return Shader
  function M.new_shader_c(name, compute)
    return manager:register(Shader.new_c(name, compute))
  end

  --- @param args AudioNewArgs
  --- @return Audio
  function M.new_audio(args)
    return manager:register(Audio.new(args))
  end

  --- @param args VideoNewArgs
  --- @return Video
  function M.new_video(args)
    return manager:register(Video.new(args))
  end

  --- Free a resource and remove it from the resource manager
  --- @param res Resource
  function M.free(res)
    manager:unregister(res.name)
  end

  return M
end
