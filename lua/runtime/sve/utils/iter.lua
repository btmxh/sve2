local M = {}

--- @generic T
--- @param iter fun():T,...
--- @return table<T>
function M.iter_to_arr(iter)
  local arr = {}
  for value in iter do
    arr[#arr+1] = value
  end

  return arr
end

return M
