local M = {}

--- @param cb (fun(): nil)|nil a new render callback
function M.set_render_callback(cb)
  render_callback = cb
end

--- @param cb (fun(): nil)|nil a new render callback
function M.set_on_close_callback(cb)
  on_close_callback = cb
end

return M
