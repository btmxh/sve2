#include "log_buffer.h"

#include <stdarg.h>
#include <string.h>

#include <log.h>

#include "sve2/utils/minmax.h"
#include "sve2/utils/threads.h"

void log_buffer_init(log_buffer *l) {
  sve2_mtx_init(&l->mutex, mtx_plain);
  l->msg = NULL;
  l->msg_len = l->msg_cap = 0;
}

static void log_buffer_grow(log_buffer *l, i32 new_cap) {
  if (l->msg_cap >= new_cap) {
    return;
  }

  new_cap = sve2_max_i32(new_cap, l->msg_cap * 2);
  l->msg = sve2_realloc(l->msg, new_cap);
}

static bool log_buffer_flush(log_buffer *l, int level, const char *file,
                             i32 line) {
  char *endl = memchr(l->msg, '\n', l->msg_len);
  if (!endl) {
    return false;
  }

  i32 line_len = (i32)(endl - l->msg);
  log_log(level, file, line, "%.*s", (int)line_len, l->msg);
  l->msg_len -= line_len + 1;
  memmove(l->msg, &endl[1], l->msg_len);
  return true;
}

void log_buffer_log(log_buffer *l, i32 level, const char *file, i32 line,
                    const char *fmt, va_list va) {
  va_list va2;
  va_copy(va2, va);
  i32 len = vsnprintf(NULL, 0, fmt, va2);
  // spare one character for the null-terminator
  // we are not going to use it though
  sve2_mtx_lock(&l->mutex);
  log_buffer_grow(l, l->msg_len + len + 1);
  vsnprintf(&l->msg[l->msg_len], len + 1, fmt, va);
  l->msg_len += len;

  while (log_buffer_flush(l, level, file, line)) {
  }

  sve2_mtx_unlock(&l->mutex);
}
