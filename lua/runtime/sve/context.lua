local ffi = require("ffi")
local ctypes = require("sve.ctypes")
local gltypes = require("sve.resources.gl.types")

ffi.cdef [[
typedef struct context_t context_t;

void context_set_should_close(context_t* c, bool should_close);
bool context_get_should_close(context_t* c);
void context_get_framebuffer_info(context_t* c, i32* w, i32* h, f32* xscale, f32* yscale);
GLuint context_default_framebuffer(context_t* c);
void context_set_audio_timer(context_t *c, i64 time);
i64 context_get_audio_timer(context_t *c);
bool context_map_audio(context_t* c, u8** staging_buffer, i32* nb_samples);
void context_unmap_audio(context_t* c, i32 nb_samples);
]]

local C = ffi.C
local M = {}

--- @return ffi.ctype* ctx the current sve context
function M.get()
  return context
end

--- @param flag boolean should close flag
function M.set_should_close(flag)
  C.context_set_should_close(M.get(), ctypes.bool(flag))
end

function M.close()
  M.set_should_close(true)
end

--- @return boolean flag the value of the should close flag
function M.get_should_close()
  return C.context_get_should_close(M.get())
end

--- @class FramebufferInfo
--- @field width number
--- @field height number
--- @field xscale number
--- @field yscale number

--- @return FramebufferInfo info
function M.get_framebuffer_info()
  local pw = ctypes.pi32()
  local ph = ctypes.pi32()
  local pxscale = ctypes.pf32()
  local pyscale = ctypes.pf32()
  C.context_get_framebuffer_info(M.get(), pw, ph, pxscale, pyscale)

  return {
    width = pw[0],
    height = ph[0],
    xscale = pxscale[0],
    yscale = pyscale[0],
  }
end

--- @return number fbo the default framebuffer of the sve context
function M.default_framebuffer()
  return C.context_default_framebuffer(M.get())
end

--- @param time ffi.cdata* time in ns (64-bit integer)
function M.set_audio_timer(time)
  return C.context_set_audio_timer(M.get(), time)
end

--- @return ffi.cdata* time the current audio timer value
function M.get_audio_timer()
  return C.context_get_audio_timer(M.get())
end

--- @class (exact) AudioMapContext
--- @field buffer ffi.cdata* sample buffer for audio data
--- @field nb_samples integer number of samples that [buffer] can hold

--- @return AudioMapContext|nil ctx an optional audio mapping context
function M.map_audio()
  local pbuffer = ctypes.ppu8()
  local pnb_samples = ctypes.pi32()
  if C.context_map_audio(M.get(), pbuffer, pnb_samples) then
    return {
      buffer = pbuffer[0],
      nb_samples = pnb_samples[0],
    }
  end

  return nil
end

--- @param nb_samples number number of samples mapped
function M.unmap_audio(nb_samples)
  C.context_unmap_audio(M.get(), ctypes.i32(nb_samples))
end

--- @param fn fun(ctx: AudioMapContext): integer
function M.submit_audio(fn)
  local ctx = M.map_audio()
  if ctx == nil then
    return nil
  end
  M.unmap_audio(fn(ctx))
end

return M
