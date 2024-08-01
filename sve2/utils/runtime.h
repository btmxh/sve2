#pragma once

#include <assert.h>
#include <stdnoreturn.h>
#include <log.h>

#include "sve2/utils/types.h"

#define nassert(x)                                                             \
  do {                                                                         \
    typeof(x) nassert_value = x;                                               \
    assert(nassert_value);                                                     \
  } while (0);

#define nassert_ffmpeg(x)                                                      \
  do {                                                                         \
    int nassert_value = x;                                                     \
    if (nassert_value < 0) {                                                   \
      log_error("FFmpeg error: %s", av_err2str(nassert_value));                \
    }                                                                          \
    assert(nassert_value >= 0);                                                \
  } while (0);

noreturn void panic();

// operations that (almost) never fail

// memory allocation (with signed arguments)
void *sve2_malloc(i32 size);
void *sve2_calloc(i32 nmem, i32 size);
void *sve2_realloc(void *ptr, i32 new_size);

// see av_freep
void sve2_freep(void * /* should be T** */ ptr);

#define sve2_sizeof(x) ((i32)sizeof(x))
#define sve2_offsetof(x, mem) ((i32)offsetof(x, mem))
#define sve2_arrlen(x) (sve2_sizeof(x) / sve2_sizeof(x[0]))
