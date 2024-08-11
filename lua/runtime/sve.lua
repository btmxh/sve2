local sve = {}

local log = require('sve.log')
sve.trace = log.trace
sve.debug = log.debug
sve.info = log.info
sve.warn = log.warn
sve.error = log.error

sve.inspect = require('inspect').inspect

sve.rm = require('sve.resources.manager').new()

local rinit = require('sve.resources.rinit')(sve.rm)
sve.new_vao = rinit.new_vao
sve.new_shader_vf = rinit.new_shader_vf
sve.new_shader_c = rinit.new_shader_c
sve.new_audio = rinit.new_audio
sve.new_video = rinit.new_video
sve.free = rinit.free

sve.ctx = require('sve.context')

local callbacks = require("sve.callbacks")
sve.set_render_callback = callbacks.set_render_callback
sve.set_on_close_callback = callbacks.set_on_close_callback

return sve
