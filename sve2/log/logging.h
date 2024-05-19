#pragma once

#include <log.h>

#ifndef SVE_NO_NONSTD
#define RAW_LOG_ATTRIBUTE __attribute__((format(printf, 1, 2)))
#else
#define RAW_LOG_ATTRIBUTE
#endif

// raw logging: used if the common logging interface could not be used
// e.g. when init_logging() has not finished
void raw_log(const char *fmt, ...) RAW_LOG_ATTRIBUTE;
void raw_log_panic(const char *fmt, ...) RAW_LOG_ATTRIBUTE;

// init the common logging interface provided by log.c
void init_logging(); // panic on failure
