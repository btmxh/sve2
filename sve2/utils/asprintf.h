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

// temporary asprintf: no other invokes to (v)asprintf_temp must be called
// before the string is freed via sve2_asprintf_temp_free
char *sve2_asprintf_temp(const char *fmt, ...) ASPRINTF_ATTRIBUTE;
char *sve2_vasprintf_temp(const char *fmt, va_list vl);
void sve2_asprintf_temp_free(char *str);

char *sve2_strdup(const char *str);
