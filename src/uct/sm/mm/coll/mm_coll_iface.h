/**
 * Copyright (c) UT-Battelle, LLC. 2014-2015. ALL RIGHTS RESERVED.
 * Copyright (C) Mellanox Technologies Ltd. 2001-2015.  ALL RIGHTS RESERVED.
 * Copyright (C) ARM Ltd. 2016.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCT_MM_COLL_IFACE_H
#define UCT_MM_COLL_IFACE_H

#include <uct/sm/mm/base/mm_iface.h>

typedef struct uct_mm_coll_iface_addr {
    uct_mm_iface_addr_t rx;
    uct_mm_iface_addr_t tx;
    uint32_t            coll_id;
} UCS_S_PACKED uct_mm_coll_iface_addr_t;

typedef struct uct_mm_coll_fifo_element {
    uct_mm_fifo_element_t super;
    ucs_spinlock_t        lock;
    volatile uint32_t     pending;
} UCS_V_ALIGNED(UCS_SYS_CACHE_LINE_SIZE) uct_mm_coll_fifo_element_t;

typedef struct uct_mm_coll_ep uct_mm_coll_ep_t;

typedef struct uct_mm_coll_iface {
    uct_mm_iface_t   super;         /* the "recv FIFO" is used for many-to-one
                                       collectives, such as reduce or gather */
    uct_mm_iface_t   bcast;         /* the "bcast FIFO" is used for one-to-many
                                       collectives, such as bcast or scatter */
    uint8_t           my_coll_id;   /* my (unique) index in the group */
    uint8_t           sm_proc_cnt;  /* number of processes in the group */
    uint8_t           ep_cnt;       /* endpoint array capacity (allocated) */

    uct_mm_coll_ep_t *eps;          /* array of endpoints to different peers */
    uct_mm_coll_ep_t *eps_limit;    /* limit of used endpoints in the array */
} uct_mm_coll_iface_t;

typedef struct uct_mm_coll_iface_subclass {
    uct_mm_coll_iface_t super;
} uct_mm_lcoll_iface_t, uct_mm_bcoll_iface_t, uct_mm_ccoll_iface_t;


UCS_CLASS_DECLARE_NEW_FUNC(uct_mm_lcoll_iface_t, uct_mm_coll_iface_t, uct_md_h, uct_worker_h,
                           const uct_iface_params_t*, const uct_iface_config_t*);

UCS_CLASS_DECLARE_NEW_FUNC(uct_mm_bcoll_iface_t, uct_mm_coll_iface_t, uct_md_h, uct_worker_h,
                           const uct_iface_params_t*, const uct_iface_config_t*);

UCS_CLASS_DECLARE_NEW_FUNC(uct_mm_ccoll_iface_t, uct_mm_coll_iface_t, uct_md_h, uct_worker_h,
                           const uct_iface_params_t*, const uct_iface_config_t*);

UCS_CLASS_DECLARE(uct_mm_lcoll_iface_t, uct_md_h, uct_worker_h,
                  const uct_iface_params_t*, const uct_iface_config_t*);

UCS_CLASS_DECLARE(uct_mm_bcoll_iface_t, uct_md_h, uct_worker_h,
                  const uct_iface_params_t*, const uct_iface_config_t*);

UCS_CLASS_DECLARE(uct_mm_ccoll_iface_t, uct_md_h, uct_worker_h,
                  const uct_iface_params_t*, const uct_iface_config_t*);

UCS_CLASS_DECLARE(uct_mm_coll_ep_t, const uct_ep_params_t *params);

unsigned uct_mm_coll_iface_progress(uct_iface_h iface);

void uct_mm_coll_iface_init_ccoll_desc(uct_mm_coll_fifo_element_t* fifo_elem_p,
                                       size_t bcopy_size_per_proc,
                                       uint32_t proc_cnt);

ucs_status_t uct_mm_coll_ep_create(const uct_ep_params_t *params, uct_ep_h *ep_p);

void uct_mm_coll_ep_release_desc(uct_mm_coll_ep_t *coll_ep, void *desc);

void uct_mm_coll_ep_destroy(uct_ep_h ep);

#endif
