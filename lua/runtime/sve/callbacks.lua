local M = {}

local render_callbacks = {}
local on_close_callbacks = {}

render_callback = function ()
  for i = 1, #render_callbacks do
    render_callbacks[i]()
  end
end

on_close_callback = function ()
  for i = #on_close_callbacks, 1, -1 do
    on_close_callbacks[i]()
  end
end

--- @param cb (fun(): nil)|nil a new render callback
function M.add_render_callback(cb)
  render_callbacks[#render_callbacks+1] = cb
end

--- @param cb (fun(): nil)|nil a new on-close callback
function M.add_on_close_callback(cb)
  on_close_callbacks[#on_close_callbacks+1] = cb
end

return M
