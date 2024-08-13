local ffi = require("ffi")
local ctypes = require("sve.ctypes")

--- @class ffi.namespace*
ffi.cdef [[
enum AVMediaType {
    AVMEDIA_TYPE_UNKNOWN = -1,
    AVMEDIA_TYPE_VIDEO,
    AVMEDIA_TYPE_AUDIO,
    AVMEDIA_TYPE_DATA,
    AVMEDIA_TYPE_SUBTITLE,
    AVMEDIA_TYPE_ATTACHMENT,
    AVMEDIA_TYPE_NB
};

typedef struct {
  enum AVMediaType type;
  i32 offset;
} stream_index_t;

char *sve2_si2str_helper(i32 bufsize, char* buffer, stream_index_t index);
]]

local M = {}
local stream_index_t = ffi.typeof("stream_index_t")

local function make_index_fn(type)
  return function(offset)
    local idx = stream_index_t()
    idx.type = type
    idx.offset = offset
    return idx
  end
end

M.abs_index = make_index_fn(-1)
M.rel_video_index = make_index_fn(0)
M.rel_audio_index = make_index_fn(1)
M.rel_subs_index = make_index_fn(2)

local C = ffi.C

--- @param index ffi.cdata* stream index
function M.to_string(index)
  local bufsize = 32
  local buffer = ctypes.au8(bufsize)
  return ffi.string(C.sve2_si2str_helper(ctypes.i32(bufsize), buffer, index))
end

return M
