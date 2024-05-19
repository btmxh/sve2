#pragma once

#include <assert.h>

#include "sve2/utils/types.h"

#define nassert(x)                                                             \
  do {                                                                         \
    typeof(x) nassert_value = x;                                               \
    assert(nassert_value);                                                     \
  } while (0);

void panic();

// operations that (almost) never fail

// memory allocation (with signed arguments)
void *sve2_malloc(i32 size);
void *sve2_calloc(i32 nmem, i32 size);
void *sve2_realloc(void *ptr, i32 new_size);

// see av_freep
void sve2_freep(void* /* should be T** */ ptr);
