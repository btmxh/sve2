local ffi = require("ffi")

ffi.cdef [[
void free(void* ptr);
]]

local C = ffi.C

local M = {}

M.free = C.free
M.NULL = ffi.new("void*", nil)

return M
