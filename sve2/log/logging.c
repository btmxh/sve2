#include "logging.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <libavutil/log.h>

#include "sve2/utils/asprintf.h"
#include "sve2/utils/runtime.h"
#include "sve2/utils/threads.h"

void raw_log(const char *fmt, ...) {
  va_list v;
  va_start(v, fmt);
  fputs("RAW: ", stderr);
  vfprintf(stderr, fmt, v);
  fputc('\n', stderr);
  va_end(v);
}

noreturn void raw_log_panic(const char *fmt, ...) {
  va_list v;
  va_start(v, fmt);
  fprintf(stderr, fmt, v);
  va_end(v);
  panic();
}

static mtx_t log_mtx;

static void lock_fn(bool lock, void *udata) {
  (void)udata;
  if (lock) {
    sve2_mtx_lock(&log_mtx);
  } else {
    sve2_mtx_unlock(&log_mtx);
  }
}

char small_buffer[256];

char *alloc_log_msg(i32 length) {
  return length < sve2_sizeof(small_buffer) ? small_buffer
                                            : sve2_malloc(length);
}

void free_log_msg(char *msg) {
  if (msg != small_buffer) {
    free(msg);
  }
}

void trim_tail_log_msg(char *msg, i32 len) {
  for (i32 i = len - 1; i >= 0; --i) {
    if (!isspace(msg[i])) {
      return;
    }

    msg[i] = '\0';
  }
}

static void av_log_callback(void *avcl, int level, const char *fmt,
                            va_list vl) {
  if (level > av_log_get_level()) {
    return;
  }
  (void)avcl;
  switch (level) {
  case AV_LOG_PANIC:
  case AV_LOG_FATAL:
    level = LOG_FATAL;
    break;
  case AV_LOG_ERROR:
    level = LOG_ERROR;
    break;
  case AV_LOG_WARNING:
    level = LOG_WARN;
    break;
  case AV_LOG_INFO:
    level = LOG_INFO;
    break;
  case AV_LOG_DEBUG:
    level = LOG_DEBUG;
    break;
  case AV_LOG_VERBOSE:
  case AV_LOG_TRACE:
    level = LOG_TRACE;
    break;
  }

  char *msg = sve2_vasprintf_temp(fmt, vl);
  trim_tail_log_msg(msg, strlen(msg));
  log_log(level, __FILE__, __LINE__, "%s", msg);
  sve2_asprintf_temp_free(msg);
}

void init_logging() {
  sve2_mtx_init(&log_mtx, mtx_plain);
  log_set_lock(lock_fn, NULL);
  log_set_level(LOG_TRACE);
  av_log_set_level(AV_LOG_INFO);
  av_log_set_callback(av_log_callback);
}
