/*
* Copyright (C) Mellanox Technologies Ltd. 2001-2014.  ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifndef UCS_SPINLOCK_H
#define UCS_SPINLOCK_H

#include <ucs/type/status.h>
#include <pthread.h>

#ifdef HAVE_CONFIG_H
#  include "config.h"
#if ENABLE_ASSERT
#include <ucs/debug/assert.h>
#endif
#endif

BEGIN_C_DECLS

/** @file spinlock.h */

#define UCS_SPINLOCK_NO_OWNER (0xfffffffful)

/**
 * Reentrant spinlock.
 */
typedef struct ucs_spinlock {
    pthread_spinlock_t lock;
    int                count;
    pthread_t          owner;
} ucs_spinlock_t;

ucs_status_t ucs_spinlock_init(ucs_spinlock_t *lock);

void ucs_spinlock_destroy(ucs_spinlock_t *lock);

static inline int ucs_spin_is_owner(ucs_spinlock_t *lock, pthread_t self)
{
    return lock->owner == self;
}

static inline void ucs_spin_lock(ucs_spinlock_t *lock)
{
    pthread_t self = pthread_self();

    if (ucs_spin_is_owner(lock, self)) {
        ++lock->count;
        return;
    }

    pthread_spin_lock(&lock->lock);
    lock->owner = self;
    ++lock->count;
}

static inline int ucs_spin_trylock(ucs_spinlock_t *lock)
{
    pthread_t self = pthread_self();

    if (ucs_spin_is_owner(lock, self)) {
        ++lock->count;
        return 1;
    }

    if (pthread_spin_trylock(&lock->lock) != 0) {
        return 0;
    }

    lock->owner = self;
    ++lock->count;
    return 1;
}

static inline void ucs_spin_unlock(ucs_spinlock_t *lock)
{
    --lock->count;
    if (lock->count == 0) {
        lock->owner = UCS_SPINLOCK_NO_OWNER;
        pthread_spin_unlock(&lock->lock);
    }
}

/**
 * Special Reentrant spinlock for shared-memory, with a minimized footprint.
 */

#if ENABLE_ASSERT
typedef ucs_spinlock_t ucs_spinlock_pure_t;
#else
typedef struct { pthread_spinlock_t lock; } ucs_spinlock_pure_t;
#endif

ucs_status_t ucs_spinlock_pure_init(ucs_spinlock_pure_t *lock, int is_shared);

void ucs_spinlock_pure_destroy(ucs_spinlock_pure_t *lock);

static inline void ucs_spin_pure_lock(ucs_spinlock_pure_t *lock)
{
#if ENABLE_ASSERT
    pthread_t self = pthread_self();
    ucs_assert(lock->owner == UCS_SPINLOCK_NO_OWNER);
    lock->owner = self;
    ucs_assert(lock->count++ == 0);
#endif
    pthread_spin_lock(&lock->lock);
}

static inline void ucs_spin_pure_unlock(ucs_spinlock_pure_t *lock)
{
#if ENABLE_ASSERT
    ucs_assert(--lock->count == 0);
    ucs_assert(lock->owner == pthread_self());
    lock->owner = UCS_SPINLOCK_NO_OWNER;
#endif
    pthread_spin_unlock(&lock->lock);
}

END_C_DECLS

#endif
