#include <stdlib.h>

#include <dotenv.h>
#include <luajit-2.1/lauxlib.h>
#include <luajit-2.1/lua.h>
#include <luajit-2.1/lualib.h>
#include <unistd.h>

#include "sve2/context/context.h"
#include "sve2/gl/shader.h"
#include "sve2/utils/cmd_queue.h"
#include "sve2/utils/runtime.h"

void add_lua_library_path(lua_State *L, const char *path) {
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "path");
  char *abs_path = realpath(path, NULL);
  const char *old_path = lua_tostring(L, -1);
  char *new_path;
  asprintf(&new_path, "%s;%s", old_path, path);

  lua_pop(L, 1);
  lua_pushstring(L, new_path);
  lua_setfield(L, -2, "path");
  lua_pop(L, 1);

  free(abs_path);
  free(new_path);
}

int main(int argc, char *argv[]) {
  env_load(".env", true);
  AVChannelLayout ch_layout = AV_CHANNEL_LAYOUT_STEREO;

  context_t *c;
  char *output_path = getenv("OUTPUT_PATH");
  nassert(c = context_init(&(context_init_t){
              .mode = output_path ? CONTEXT_MODE_RENDER : CONTEXT_MODE_PREVIEW,
              .width = 1920,
              .height = 1080,
              .fps = 60,
              .output_path = output_path,
              .sample_rate = 48000,
              .sample_fmt = AV_SAMPLE_FMT_FLT,
              .num_buffered_audio_frames = 4,
              .ch_layout = &ch_layout}));

  lua_State *lua = lua_open();
  luaL_openlibs(lua);
  lua_pushlightuserdata(lua, c);
  lua_setglobal(lua, "context");
  add_lua_library_path(lua, "./lua/runtime/?.lua");

  if (luaL_dofile(lua, "lua/runtime/_preload.lua")) {
    log_error("unable to execute _preload.lua: %s", lua_tostring(lua, -1));
    return 1;
  }

  for (i32 i = 1; i < argc; ++i) {
    if (luaL_dofile(lua, argv[i])) {
      log_error("unable to execute preload script '%s': %s", argv[i],
                lua_tostring(lua, -1));
    }
  }

  cmd_queue_t cmd_queue;
  cmd_queue_init(&cmd_queue);

  while (!context_get_should_close(c)) {
    context_begin_frame(c);

    char *cmd;
    while (cmd_queue_get(&cmd_queue, &cmd) > 0 && cmd) {
      log_info("processing lua command: %s", cmd);
      if (luaL_dostring(lua, cmd)) {
        log_error("error: %s", lua_tostring(lua, -1));
        lua_pop(lua, 1);
      }
      cmd_queue_unget(&cmd_queue, cmd);
    }

    lua_getglobal(lua, "render_callback");
    if (!lua_isnoneornil(lua, -1)) {
      if (lua_pcall(lua, 0, 0, 0) != 0) {
        log_error("render error: %s", lua_tostring(lua, -1));
      }
    } else {
      lua_pop(lua, -1);
    }
    context_end_frame(c);
  }

  cmd_queue_free(&cmd_queue);

  lua_getglobal(lua, "on_close_callback");
  if (!lua_isnoneornil(lua, -1)) {
    if (lua_pcall(lua, 0, 0, 0) != 0) {
      log_error("on close callback error: %s", lua_tostring(lua, -1));
    }
  } else {
    lua_pop(lua, -1);
  }

  lua_close(lua);
  context_free(c);

  return 0;
}
