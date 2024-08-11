local iter = require("sve.utils.iter")

local M = {}

--- @param duration string
--- @return integer ns duration in int64 nanoseconds, raise error if fails
function M.parse_duration(duration)
  local d = M.try_parse_duration(duration)
  if d == nil then
    error("invalid duration: " .. duration)
  end

  return d
end

--- @param duration string
--- @return integer? ns duration in int64 nanoseconds, or nil if fails
function M.try_parse_duration(duration)
  local tokens = iter.iter_to_arr(duration:gmatch("[^:]+"))

  if #tokens <= 1 or #tokens >= 4 then
    return nil
  end

  local seconds = tonumber(tokens[#tokens])
  local minutes = tonumber(tokens[#tokens - 1])
  --- @type number?
  local hours = 0
  if #tokens == 3 then
    hours = tonumber(tokens[1])
  end

  if hours == nil or minutes == nil or seconds == nil then
    return nil
  end

  local total_seconds = hours * 3600 + minutes * 60 + math.floor(seconds)
  local ns = (seconds - math.floor(seconds)) * 1e9

  local total_ns = 1000000000LL * total_seconds + ns
  return total_ns
end

return M
