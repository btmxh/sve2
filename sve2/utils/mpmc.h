#pragma once

#include <threads.h>

#include "sve2/utils/rb.h"

typedef struct {
  mtx_t mutex;
  cnd_t cond;
  rb_t buffer;
} mpmc_t;

void mpmc_init(mpmc_t *m, i32 initial_cap, f32 grow_factor, i32 elem_sizeof);
bool mpmc_send(mpmc_t *m, const void *data, i64 deadline);
bool mpmc_recv(mpmc_t *m, void *data, i64 deadline);
i32 mpmc_len(mpmc_t *m);
void mpmc_free(mpmc_t *m);
