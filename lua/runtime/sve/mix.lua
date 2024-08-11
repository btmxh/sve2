local ffi = require("ffi")

--- @class Mixer
local Mixer = {}
Mixer.__index = Mixer

--- @return Mixer
function Mixer.new()
  return setmetatable({}, Mixer)
end

--- @param value number
local function protect(value)
  if value ~= value then
    sve.warn("NaN detected in audio buffer, silencing")
    return 0
  end

  if math.abs(value) > 2.0 then
    sve.warn("Sample out of range, silencing")
    return 0
  end

  return value
end

--- @class Track
--- @field [1] Audio source audio
--- @field gain number|nil

--- @param nb_samples integer
--- @param buffer ffi.cdata*
--- @param tracks table<integer, Track>
--- @return integer
function Mixer.mix(nb_samples, buffer, tracks)
  ffi.fill(buffer, nb_samples * ffi.sizeof("f32[2]"), 0)
  local f32_buffer = ffi.cast("f32*", buffer)
  local temp_buffer = ffi.new("f32[?]", nb_samples * 2)
  for _, track in pairs(tracks) do
    local source_nb_samples = track[1]:get_samples(nb_samples, ffi.cast("u8*", temp_buffer))
    for i = 0, source_nb_samples * 2 - 1 do
      f32_buffer[i] = f32_buffer[i] + temp_buffer[i] * (track.gain or 1.0)
    end
  end

  for i = 0, nb_samples * 2 - 1 do
    f32_buffer[i] = protect(f32_buffer[i])
  end

  return nb_samples
end

--- @return number
function db(number)
  return math.pow(10, number / 20)
end

return Mixer
