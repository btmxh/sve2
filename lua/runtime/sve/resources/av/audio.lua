local Resource = require("sve.resources.resource")
local ffi = require("ffi")
local ctx = require("sve.context")
local ctypes = require("sve.ctypes")
local libc = require("sve.libc")
local si = require("sve.resources.av.si")
local p = require("sve.utils.path")

ffi.cdef [[
typedef enum {
  AUDIO_FORMAT_FFMPEG_STREAM,
  AUDIO_FORMAT_RAW_PCM_SAMPLES,
} audio_format_t;
typedef struct audio_t audio_t;

audio_t* audio_alloc();
bool audio_open(context_t *ctx, audio_t *a, const char *path,
                stream_index_t index, audio_format_t format);
void audio_close(audio_t *a);
void audio_seek(audio_t *a, i64 time);
void audio_get_samples(audio_t *a, i32* num_samples, u8* samples);
]]

local C = ffi.C
local AUDIO_FORMAT_FFMPEG_STREAM = ffi.new("audio_format_t", 0)
local AUDIO_FORMAT_PCM_SAMPLES = ffi.new("audio_format_t", 1)

--- @class Audio : Resource
--- @field name string
--- @field handle ffi.cdata* underlying audio_t object
local Audio = {}
Audio.__index = Audio
setmetatable(Audio, Resource)

--- @class AudioNewArgs
--- @field [1] string audio file path
--- @field name string? audio resource name, path will be used if absent
--- @field absidx integer? absolute stream index
--- @field relidx integer? relative stream index, 0 is used if both relidx or absidx is absent
--- @field stream boolean? whether to stream audio or not, default is true

--- @param args AudioNewArgs
--- @return Audio
function Audio.new(args)
  local path = args[1]
  local name = args.name
  local index = si.rel_audio_index(0)
  local stream = args.stream ~= false

  assert(args.relidx == nil or args.absidx == nil, "relidx and absidx should not be set at the same time")
  if args.relidx ~= nil then
    index = si.rel_audio_index(args.relidx)
  end
  if args.absidx ~= nil then
    index = si.abs_index(args.absidx)
  end

  local format = AUDIO_FORMAT_FFMPEG_STREAM
  if not stream then
    format = AUDIO_FORMAT_PCM_SAMPLES
  end

  local paudio = C.audio_alloc()
  C.audio_open(ctx.get(), paudio, p.expand(path), index, format)

  return Resource.new({
    name = name or (path .. ":" .. si.to_string(index)),
    handle = paudio
  }, Audio)
end

--- @param time integer seek time as i64 nanoseconds
function Audio:seek(time)
  C.audio_seek(self.handle, time)
end

--- @param num_samples integer number of samples in [buffer]
--- @param buffer ffi.cdata* sample buffer to retrieve data
function Audio:get_samples(num_samples, buffer)
  local pnum_samples = ctypes.pi32(num_samples)
  C.audio_get_samples(self.handle, pnum_samples, buffer)
  return pnum_samples[0]
end

function Audio:free()
  C.audio_close(self.handle)
  libc.free(self.handle)
end

return Audio
