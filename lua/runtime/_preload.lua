d = require("sve.duration").parse_duration
sve = require("sve")

sve.add_on_close_callback(function ()
  sve.rm:clear()
end)
