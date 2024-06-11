#include "logging.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <libavutil/log.h>

#include "sve2/log/log_buffer.h"
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

log_buffer ffmpeg_buffer;

static void init_ffmpeg_log_buffer() { log_buffer_init(&ffmpeg_buffer); }

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

  log_buffer_log(&ffmpeg_buffer, level, __FILE__, __LINE__, fmt, vl);
}

void init_logging() {
  sve2_mtx_init(&log_mtx, mtx_plain);
  log_set_lock(lock_fn, NULL);
  log_set_level(LOG_TRACE);
  init_ffmpeg_log_buffer();
  av_log_set_level(AV_LOG_TRACE);
  av_log_set_callback(av_log_callback);
}

void done_logging() {
  log_buffer_free(&ffmpeg_buffer);
}
