#include "runtime.h"

#include <stdlib.h>

noreturn void panic() {
  exit(EXIT_FAILURE);
}

void *sve2_malloc(i32 size) {
  assert(size >= 0);
  if (size == 0) {
    return NULL;
  }
  void *ptr = malloc((size_t)size);
  assert(ptr);
  return ptr;
}

void *sve2_calloc(i32 nmem, i32 size) {
  assert(size >= 0 && nmem >= 0);
  if (size == 0 || nmem == 0) {
    return NULL;
  }
  void *ptr = calloc((size_t)nmem, (size_t)size);
  assert(ptr);
  return ptr;
}

void *sve2_realloc(void *ptr, i32 new_size) {
  assert(new_size >= 0);
  if (new_size == 0) {
    free(ptr);
    return NULL;
  }
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
