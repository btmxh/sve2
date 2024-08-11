--- Generic resource manager
--- @class ResourceManager
---
--- @field tracked_resources table<string, Resource> table of tracked resources
local ResourceManager = {}
ResourceManager.__index = ResourceManager

function ResourceManager.new()
  return setmetatable({
    tracked_resources = {
    }
  }, ResourceManager)
end

--- This function is used to retrieve the resource name of a generic
--- resource without triggering a false warning from lua-ls
--- (see [ResourceManager:register()]).
---
--- If directly accessing [res.name] does not cause any issues, then do
--- that instead of this function.
---
--- @param res Resource
--- @return string name the name of this resource
local function getname(res)
  return res.name
end

--- Register a resource for tracking
---@generic T: Resource
---@param res T The tracked resource
---@return T res
function ResourceManager:register(res)
  local name = getname(res)
  self:unregister(name)
  self.tracked_resources[name] = res
  return res
end

function ResourceManager:unregister(name)
  local res = self.tracked_resources[name]
  if res ~= nil then
    res:free()
  end

  self.tracked_resources[name] = nil
end

function ResourceManager:clear()
  for _, res in pairs(self.tracked_resources) do
    res:free()
  end

  self.tracked_resources = {}
end

return ResourceManager
