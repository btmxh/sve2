#pragma once

#include <threads.h>

#include "sve2/utils/runtime.h"
#include "sve2/utils/types.h"

// thread timer, used to identify deadlines
void init_threads_timer();
i64 threads_timer_now();
#define SVE2_NS_PER_SEC ((i64)1000000000)

#define sve2_thrd_create(...) nassert(thrd_create(__VA_ARGS__) == thrd_success)
#define sve2_mtx_init(m, type) nassert(mtx_init(m, type) == thrd_success)
#define sve2_mtx_lock(m) nassert(mtx_lock(m) == thrd_success)
#define sve2_mtx_unlock(m) nassert(mtx_unlock(m) == thrd_success)
#define sve2_cnd_init(c) nassert(cnd_init(c) == thrd_success)
#define sve2_cnd_wait(c, m) nassert(cnd_wait(c, m) == thrd_success)
#define sve2_cnd_signal(c) nassert(cnd_signal(c) == thrd_success)
#define sve2_cnd_broadcast(c) nassert(cnd_broadcast(c) == thrd_success)

// timeout API (using deadline)
#define SVE_DEADLINE_NOW ((i64)-1)
#define SVE_DEADLINE_INF ((i64)INT64_MAX)

// use mtx_lock/mtx_trylock if possible (deadline is one of the special values)
bool sve2_mtx_timedlock(mtx_t *mutex, i64 deadline);
// use mtx_lock/mtx_trylock if possible (deadline is one of the special values)
bool sve2_cnd_timedwait(cnd_t *cond, mtx_t *mutex, i64 deadline);

// sleep functions
void sve2_sleep_for(i64 time);
void sve2_sleep_until(i64 deadline);
