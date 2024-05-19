#pragma once

#include <stdarg.h>

#ifdef SVE2_NO_NONSTD
#define ASPRINTF_ATTRIBUTE __attribute((format(printf, 1, 2)))
#else
#define ASPRINTF_ATTRIBUTE
#endif

// wrappers for asprintf/vasprintf
// currently using the GNU asprintf, but it is trivial to
// switch to standard-compliant snprintf
char *sve2_asprintf(const char *fmt, ...) ASPRINTF_ATTRIBUTE;
char *sve2_vasprintf(const char *fmt, va_list vl);
char *sve2_strdup(const char *str);
