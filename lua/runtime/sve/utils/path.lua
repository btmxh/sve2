local M = {}

--- @param path string
--- @return string
function M.expand(path)
  local new_path, _ = path:gsub("~", os.getenv("HOME") or "")
  return new_path
end

return M
