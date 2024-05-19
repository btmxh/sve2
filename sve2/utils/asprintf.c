#include "asprintf.h"

#include <stdio.h>
#include <stdlib.h>
#include <threads.h>

#include "sve2/utils/runtime.h"
#include "sve2/utils/types.h"

char *sve2_asprintf(const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  char *ret = sve2_vasprintf(fmt, va);
  va_end(va);
  return ret;
}

char *sve2_vasprintf(const char *fmt, va_list vl) {
  char *out;
  vasprintf(&out, fmt, vl);
  assert(out);
  return out;
}

thread_local char temp_buffer[1024] = {0};
thread_local bool temp_buffer_in_use = false;

static char *temp_alloc(i32 size) {
  assert(!temp_buffer_in_use);
  temp_buffer_in_use = true;
  return size <= sve2_sizeof(temp_buffer) ? temp_buffer : sve2_calloc(size, 1);
}

static void temp_free(char *ptr) {
  if (ptr != temp_buffer) {
    free(ptr);
  }

  temp_buffer_in_use = false;
}

char *sve2_asprintf_temp(const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  char *ret = sve2_vasprintf_temp(fmt, va);
  va_end(va);
  return ret;
}

char *sve2_vasprintf_temp(const char *fmt, va_list vl) {
  va_list vl2;
  va_copy(vl2, vl);
  i32 len = vsnprintf(NULL, 0, fmt, vl);
  char *buffer = temp_alloc(len + 1);
  vsnprintf(buffer, len + 1, fmt, vl2);
  return buffer;
}

void sve2_asprintf_temp_free(char *str) { temp_free(str); }

char *sve2_strdup(const char *str) { return sve2_asprintf("%s", str); }
