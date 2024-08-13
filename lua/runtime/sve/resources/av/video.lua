local ffi = require("ffi")
local Resource = require("sve.resources.resource")
local ctx = require("sve.context")
local si = require("sve.resources.av.si")
local p = require("sve.utils.path")
local libc = require("sve.libc")

--- @class Video : Resource
--- @field handle ffi.cdata* the underlying video_t*, stored as a char[sizeof(video_t)]
local Video = {}
Video.__index = Video
setmetatable(Video, Resource)

--- @class ffi.namespace*
ffi.cdef [[
typedef struct video_t video_t;
typedef int AVPixelFormat;

typedef enum {
  VIDEO_FORMAT_FFMPEG_STREAM,
  VIDEO_FORMAT_TEXTURE_ARRAY,
} video_format_t;

typedef struct {
  int sw_format;
  i32 texture_array_index;
  GLuint textures[4];
} video_frame_t;

bool video_open(context_t *ctx, video_t *v, const char *path,
                stream_index_t index, video_format_t format);
void video_close(video_t *v);
void video_seek(video_t *v, i64 time);
bool video_get_texture(video_t *v, i64 time, video_frame_t *tex);
video_t* video_alloc();
]]

local C = ffi.C
local pvideo_frame_t = ffi.typeof("video_frame_t[1]");
local VIDEO_FORMAT_FFMPEG_STREAM = ffi.new("video_format_t", 0)
local VIDEO_FORMAT_TEXTURE_ARRAY = ffi.new("video_format_t", 1)

--- @class (exact) VideoNewArgs
--- @field [1] string video path
--- @field name string|nil video name
--- @field relidx number|nil relative index of the video stream
--- @field absidx number|nil absolute index of the video stream
--- @field stream boolean|nil whether to stream the video or not

--- @param args VideoNewArgs
function Video.new(args)
  local path = args[1]
  local name = args.name
  local index = si.rel_video_index(0)
  local stream = args.stream ~= false

  assert(args.relidx == nil or args.absidx == nil, "relidx and absidx should not be set at the same time")
  if args.relidx ~= nil then
    index = si.rel_audio_index(args.relidx)
  end
  if args.absidx ~= nil then
    index = si.abs_index(args.absidx)
  end

  local format = VIDEO_FORMAT_FFMPEG_STREAM
  if not stream then
    format = VIDEO_FORMAT_TEXTURE_ARRAY
  end

  local pvideo = C.video_alloc()
  ffi.C.video_open(ctx.get(), pvideo, p.expand(path), index, format)

  return Resource.new({
    name = name or (path .. ":" .. si.to_string(index)),
    handle = pvideo,
  }, Video)
end

--- @param time integer time in int64 ns
function Video:seek(time)
  ffi.C.video_seek(self.handle, time)
end

--- @class VideoFrame
--- @field format integer
--- @field index integer
--- @field textures table<integer>

--- @param time integer
--- @return VideoFrame|nil
function Video:get_texture(time)
  local pframe = pvideo_frame_t()
  if not ffi.C.video_get_texture(self.handle, time, pframe) then
    return nil
  end

  local frame = pframe[0]
  local textures = {}
  for i = 0, 3 do
    if frame.textures[i] ~= 0 then
      textures[#textures + 1] = frame.textures[i]
    end
  end

  return {
    format = frame.sw_format,
    index = frame.texture_array_index,
    textures = textures,
  }
end

function Video:free()
  ffi.C.video_close(self.handle)
  libc.free(self.handle)
end

return Video
