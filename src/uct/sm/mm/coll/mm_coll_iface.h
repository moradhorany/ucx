/**
 * Copyright (c) UT-Battelle, LLC. 2014-2015. ALL RIGHTS RESERVED.
 * Copyright (C) Mellanox Technologies Ltd. 2001-2015.  ALL RIGHTS RESERVED.
 * Copyright (C) ARM Ltd. 2016.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCT_MM_COLL_IFACE_H
#define UCT_MM_COLL_IFACE_H

#include <uct/sm/mm/base/mm_iface.h>

#define UCT_MM_COLL_TL_NAME "mm_coll"

typedef struct uct_mm_coll_iface_config {
    uct_mm_iface_config_t super;
} uct_mm_coll_iface_config_t;

typedef struct uct_mm_coll_iface_addr {
    uct_mm_iface_addr_t rx;
    uct_mm_iface_addr_t tx;
    uint32_t            coll_id;
} UCS_S_PACKED uct_mm_coll_iface_addr_t;

typedef struct uct_mm_coll_fifo_element {
    uct_mm_fifo_element_t super;
    ucs_spinlock_pure_t   lock;
    volatile uint32_t     pending;
} uct_mm_coll_fifo_element_t;

typedef struct uct_mm_coll_ep uct_mm_coll_ep_t;

typedef struct uct_mm_coll_peer_ep {
#define UCT_MM_COLL_NO_PEER_ID ((uint64_t)-1)
    uint64_t          peer_id;
    uct_mm_coll_ep_t *ep;
} uct_mm_coll_peer_ep_t;

typedef struct uct_mm_coll_iface {
    uct_mm_iface_t          super; /* The "recv FIFO" is used for many-to-one */
                                   /* collectives, such as reduce or gather */
    uct_mm_iface_t          bcast; /* The "bcast FIFO" is used for one-to-many */
                                   /* collectives, such as bcast or scatter */

    uint32_t                my_coll_id;
    uint32_t                sm_proc_cnt;
    uct_md_attr_t           md_attr;

    /* Array of endpoints to different peers, ordered by connection time */
    uct_mm_coll_peer_ep_t  *eps;
    // TODO: allocate (and re-allocate) an array where each EP is aligned
} uct_mm_coll_iface_t;

unsigned uct_mm_coll_iface_progress(void *arg);

static inline ucs_status_t uct_mm_coll_iface_get_ep(uct_mm_coll_iface_t *iface,
        uint64_t peer_id, uct_mm_coll_peer_ep_t **peer_ep)
{
    /*
     * Note: iface->eps is initially allocated to the maximal required size,
     * and also "null-terminated". This function is intended for "slow-path".
     */
    uct_mm_coll_peer_ep_t *iter = iface->eps;
    while (iter->ep && (iter->peer_id != peer_id)) {
        iter++;
    }

    *peer_ep = iter; /* If not found - return the next vacant slot to be used */
    return iter->ep ? UCS_OK : UCS_ERR_NO_ELEM;
}

extern uct_tl_component_t uct_mm_coll_tl;

#endif
