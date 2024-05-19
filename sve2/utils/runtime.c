#include "runtime.h"

#include <stdlib.h>

void *sve2_malloc(i32 size) {
  assert(size >= 0);
  void *ptr = malloc((size_t)size);
  assert(ptr);
  return ptr;
}

void *sve2_calloc(i32 nmem, i32 size) {
  assert(size >= 0 && nmem >= 0);
  void *ptr = calloc((size_t)nmem, (size_t)size);
  assert(ptr);
  return ptr;
}

void *sve2_realloc(void *ptr, i32 new_size) {
  assert(new_size >= 0);
  ptr = malloc((size_t)new_size);
  assert(ptr);
  return ptr;
}

void sve2_freep(void *ptr) {
  if (!ptr) {
    return;
  }

  void **p = (void **)ptr;
  free(*p);
  *p = NULL;
}
