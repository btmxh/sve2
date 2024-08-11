--- Generic resource type
---
--- Resources are Lua objects that contains system resources outside the scope
--- of the Lua garbage collector (e.g. FFmpeg media, OpenGL objects, etc.)
---
--- All resources must be manually managed in Lua code. To free a resource,
--- call its [Resource:free()] method.
---
--- @class Resource
---
--- @field name string A unique name to identify the resource
---
local Resource = {}
Resource.__index = Resource

---Create a new resource
---@generic T: Resource
---@param obj table Table containing resource information
---@param type `T` The resource type metatable
---@return T r The created resource
function Resource.new(obj, type)
  return setmetatable(obj, type)
end

-- base implementation of [Resource:free()] is no-op
function Resource:free()
end

return Resource
