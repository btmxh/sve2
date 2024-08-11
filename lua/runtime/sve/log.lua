local ffi = require('ffi')
local inspect = require('inspect').inspect
local lfs = require('lfs')
local path = require('pl.path')

ffi.cdef [[
void log_log(int level, const char *file, int line, const char *fmt, ...);
]]

local C = ffi.C

local M = {}

--- @param level integer the log level
--- @return fun(msg: any): nil log_fn corresponding log function
local function wrapper(level)
  return function(...)
    local file = debug.getinfo(2, 'S').short_src
    local line = debug.getinfo(2, 'l').currentline
    file = path.relpath(file, lfs.currentdir())

    local args = table.pack(...)
    local msg = ""
    for i = 1, args.n do
      local value = args[i]
      if type(value) ~= "string" then
        value = inspect(value)
      end

      if i ~= 0 then
        msg = msg .. "\t" .. value
      else
        msg = msg .. value
      end
    end
    C.log_log(level, file, line, msg)
  end
end

M.trace = wrapper(0)
M.debug = wrapper(1)
M.info = wrapper(2)
M.warn = wrapper(3)
M.error = wrapper(4)

M.debug("log module initialized")

return M
