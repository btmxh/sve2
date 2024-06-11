#pragma once

#include <stdio.h>
#include <threads.h>

#include "sve2/utils/types.h"

typedef struct {
  mtx_t mutex;
  char *msg;
  i32 msg_len, msg_cap; // not null-terminated
} log_buffer;

void log_buffer_init(log_buffer *l);
void log_buffer_log(log_buffer *l, i32 level, const char *file, i32 line,
                    const char *fmt, va_list va);
