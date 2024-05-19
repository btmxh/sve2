#include "rb.h"

#include <stdlib.h>
#include <string.h>

#include "sve2/utils/minmax.h"
#include "sve2/utils/runtime.h"

void rb_init(rb_t *r, i32 initial_cap, f32 grow_factor, i32 elem_sizeof) {
  assert(elem_sizeof > 0);
  r->buffer =
      sve2_malloc(initial_cap * elem_sizeof); // return NULL if initial_cap == 0
  r->first = 0;
  r->len = 0;
  r->cap = initial_cap;
  r->grow_factor = grow_factor;
  r->elem_sizeof = elem_sizeof;
}

bool rb_push(rb_t *r, const void *data) {
  if (r->len == r->cap) {
    if (!rb_can_grow(r)) {
      return false;
    }

    i32 new_cap = sve2_max_i32(r->len + 1, (i32)(r->cap * r->grow_factor));
    u8 *new_buffer = sve2_malloc(r->elem_sizeof * new_cap);
    for (i32 i = 0; i < r->len; ++i) {
      assert(r->cap > 0);
      memcpy(&new_buffer[i * r->elem_sizeof],
             &r->buffer[((i + r->first) % r->cap) * r->elem_sizeof],
             r->elem_sizeof);
    }

    free(r->buffer);
    r->buffer = new_buffer;
    r->first = 0;
    r->cap = new_cap;
  }

  assert(r->cap > 0);
  i32 index = (r->first + r->len) % r->cap;
  memcpy(&r->buffer[((r->first + r->len) % r->cap) * r->elem_sizeof], data,
         r->elem_sizeof);
  ++r->len;
  return true;
}

bool rb_pop(rb_t *r, void *data) {
  if (r->len == 0) {
    return false;
  }

  assert(r->cap > 0);
  memcpy(data, &r->buffer[r->first * r->elem_sizeof], r->elem_sizeof);
  r->first = (r->first + r->cap - 1) % r->cap;
  --r->len;
  return true;
}

void rb_free(rb_t *r) { free(r->buffer); }

i32 rb_len(const rb_t *r) { return r->len; }

i32 rb_cap(const rb_t *r) { return r->cap; }

bool rb_can_grow(const rb_t *r) { return r->grow_factor > 0; }
