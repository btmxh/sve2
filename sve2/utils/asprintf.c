#include "asprintf.h"

#include <stdio.h>

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
  return out;
}

char *sve2_strdup(const char *str) { return sve2_asprintf("%s", str); }
