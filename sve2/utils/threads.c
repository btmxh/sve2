#include "threads.h"

#include <threads.h>

#include "sve2/utils/runtime.h"

static i64 timer = 0;

i64 get_ts() {
  struct timespec ts;
  nassert(timespec_get(&ts, TIME_UTC) == TIME_UTC);
  return (i64)ts.tv_sec * SVE2_NS_PER_SEC + ts.tv_nsec;
}

// thread timer, used to identify deadlines
void init_threads_timer() {
  if (!timer) {
    timer = get_ts();
  }
}

i64 threads_timer_now() { return get_ts() - timer; }

// timeout API (using deadline)
#define SVE_DEADLINE_NOW ((i64)-1)
#define SVE_DEADLINE_INF ((i64)INT64_MAX)

struct timespec ts_from_ns(i64 ns) {
  return (struct timespec){.tv_sec = ns / SVE2_NS_PER_SEC,
                           .tv_nsec = ns % SVE2_NS_PER_SEC};
}

// use mtx_lock/mtx_trylock if possible (deadline is special values)
bool sve2_mtx_timedlock(mtx_t *mutex, i64 deadline) {
  int err;
  switch (deadline) {
  case SVE_DEADLINE_NOW:
    err = mtx_trylock(mutex);
    break;
  case SVE_DEADLINE_INF:
    err = mtx_lock(mutex);
    break;
  default: {
    struct timespec time_point = ts_from_ns(deadline);
    err = mtx_timedlock(mutex, &time_point);
  }
  }

  nassert(err != thrd_error);
  return err == thrd_success;
}

bool sve2_cnd_timedwait(cnd_t *cond, mtx_t *mutex, i64 deadline) {
  int err;
  switch (deadline) {
  case SVE_DEADLINE_NOW:
    return false;
  case SVE_DEADLINE_INF:
    err = cnd_wait(cond, mutex);
    break;
  default: {
    struct timespec time_point = ts_from_ns(deadline);
    err = cnd_timedwait(cond, mutex, &time_point);
  }
  }

  nassert(err != thrd_error);
  return err == thrd_success;
}

// sleep functions
void sve2_sleep_for(i64 time) {
  if (time <= 0) {
    return;
  }

  struct timespec remaining = ts_from_ns(time);
  int err;
  do {
    err = thrd_sleep(&remaining, &remaining);
  } while (err == -1);
}

void sve2_sleep_until(i64 deadline) {
  sve2_sleep_for(deadline - threads_timer_now());
}
