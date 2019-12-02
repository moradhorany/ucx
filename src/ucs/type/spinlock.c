/**
 * Copyright (C) Mellanox Technologies Ltd. 2018.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */

#include "spinlock.h"

#include <ucs/debug/log.h>
#include <string.h>


static ucs_status_t ucs_spinlock_init_internal(ucs_spinlock_t *lock, int flags)
{
    int ret;

    ret = pthread_spin_init(&lock->lock, flags);
    if (ret != 0) {
        ucs_error("failed to initialize SM spinlock: %m");
        return UCS_ERR_IO_ERROR;
    }

#ifndef ENABLE_ASSERT
    if (flags & PTHREAD_PROCESS_PRIVATE)
#endif
    {
        lock->count = 0;
        lock->owner = UCS_SPINLOCK_NO_OWNER;
    }

    return UCS_OK;
}

ucs_status_t ucs_spinlock_init(ucs_spinlock_t *lock) {
    return ucs_spinlock_init_internal(lock, PTHREAD_PROCESS_PRIVATE);
}

ucs_status_t ucs_spinlock_pure_init(ucs_spinlock_pure_t *lock, int is_shared) {
    return ucs_spinlock_init_internal((ucs_spinlock_t*)lock,
            is_shared ? PTHREAD_PROCESS_SHARED : PTHREAD_PROCESS_PRIVATE);
}

void ucs_spinlock_destroy_internal(ucs_spinlock_t *lock, int is_pure)
{
    int ret;

    if ((!is_pure) && (lock->count != 0)) {
        ucs_warn("destroying spinlock %p with use count %d (owner: 0x%lx)",
                 lock, lock->count, lock->owner);
    }

    ret = pthread_spin_destroy(&lock->lock);
    if (ret != 0) {
        ucs_warn("failed to destroy spinlock %p: %s", lock, strerror(ret));
    }
}

void ucs_spinlock_destroy(ucs_spinlock_t *lock)
{
    ucs_spinlock_destroy_internal(lock, 0);
}

void ucs_spinlock_pure_destroy(ucs_spinlock_pure_t *lock)
{
    ucs_spinlock_destroy_internal((ucs_spinlock_t*)lock, 1);
}
