#pragma once

// generic ring buffer data structure
#include "sve2/utils/types.h"
typedef struct {
  u8 *buffer;
  i32 first, len, cap;
  f32 grow_factor;
  i32 elem_sizeof;
} rb_t;

#define SVE2_RB_DEFAULT_GROW ((f32) 1.5)
#define SVE2_RB_NO_GROW ((f32)-1)

// set grow_factor to SVE2_RB_NO_GROW to disable growing this buffer
void rb_init(rb_t *r, i32 initial_cap, f32 grow_factor, i32 elem_sizeof);
bool rb_push(rb_t *r, const void *data);
bool rb_pop(rb_t *r, void *data);
void rb_free(rb_t *r);

i32 rb_len(const rb_t *r);
i32 rb_cap(const rb_t *r);
bool rb_can_grow(const rb_t *r);
