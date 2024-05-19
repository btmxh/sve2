#include "mpmc.h"

#include "sve2/utils/rb.h"
#include "sve2/utils/threads.h"

void mpmc_init(mpmc_t *m, i32 initial_cap, f32 grow_factor, i32 elem_sizeof) {
  sve2_mtx_init(&m->mutex, mtx_timed);
  sve2_cnd_init(&m->cond);
  rb_init(&m->buffer, initial_cap, grow_factor, elem_sizeof);
}

bool mpmc_send(mpmc_t *m, const void *data, i64 deadline) {
  if (!sve2_mtx_timedlock(&m->mutex, deadline)) {
    return false;
  }

  bool timeout = false;
  while (!rb_push(&m->buffer, data)) {
    if (!sve2_cnd_timedwait(&m->cond, &m->mutex, deadline)) {
      timeout = true;
      break;
    }
  }

  if (!timeout) {
    sve2_cnd_signal(&m->cond);
  }
  sve2_mtx_unlock(&m->mutex);
  return !timeout;
}

bool mpmc_recv(mpmc_t *m, void *data, i64 deadline) {
  if (!sve2_mtx_timedlock(&m->mutex, deadline)) {
    return false;
  }

  bool timeout = false;
  while (!rb_pop(&m->buffer, data)) {
    if (!sve2_cnd_timedwait(&m->cond, &m->mutex, deadline)) {
      timeout = true;
      break;
    }
  }

  if (!timeout) {
    sve2_cnd_signal(&m->cond);
  }
  sve2_mtx_unlock(&m->mutex);
  return !timeout;
}

i32 mpmc_len(mpmc_t *m) {
  sve2_mtx_lock(&m->mutex);
  i32 len = rb_len(&m->buffer);
  sve2_mtx_unlock(&m->mutex);
  return len;
}

void mpmc_free(mpmc_t *m) {
  mtx_destroy(&m->mutex);
  cnd_destroy(&m->cond);
  rb_free(&m->buffer);
}
