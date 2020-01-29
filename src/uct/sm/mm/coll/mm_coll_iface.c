/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <alloca.h>

#include <ucs/sys/string.h>
#include <uct/sm/base/sm_ep.h>
#include <uct/sm/base/sm_iface.h>
#include <uct/sm/mm/base/mm_ep.h>
#include <ucs/debug/memtrack.h>

#include "mm_coll_iface.h"
#include "mm_coll_ep.h"

#define UCT_MM_COLL_IFACE_GET_FIFO_ELEM(_iface, _index) \
   ucs_container_of(UCT_MM_IFACE_GET_FIFO_ELEM(&(_iface)->super, \
                                               (_iface)->super.recv_fifo_elems,\
                                               _index), \
                    uct_mm_coll_fifo_element_t, super)

static ucs_status_t uct_mm_coll_iface_get_address(uct_iface_t *tl_iface,
                                                  uct_iface_addr_t *addr)
{
    ucs_status_t status;
    uct_mm_coll_iface_t *iface = ucs_derived_of(tl_iface, uct_mm_coll_iface_t);
    uct_mm_coll_iface_addr_t *iface_addr = (void*)addr;

    status = uct_mm_iface_get_address((uct_iface_t*)&iface->super,
                                      (uct_iface_addr_t*)&iface_addr->rx);
    if (status != UCS_OK) {
        return status;
    }
    status = uct_mm_iface_get_address((uct_iface_t*)&iface->bcast,
                                      (uct_iface_addr_t*)&iface_addr->tx);
    if (status != UCS_OK) {
        return status;
    }

    iface_addr->coll_id  = iface->my_coll_id;
    return UCS_OK;
}

static ucs_status_t uct_mm_coll_iface_query(uct_iface_h tl_iface,
                                            uct_iface_attr_t *iface_attr)
{
    ucs_status_t status = uct_mm_iface_query(tl_iface, iface_attr);
    if (status != UCS_OK) {
        return status;
    }

    iface_attr->iface_addr_len = sizeof(uct_mm_coll_iface_addr_t);
    iface_attr->cap.flags      = UCT_IFACE_FLAG_BCAST    |
                                 UCT_IFACE_FLAG_INCAST   |
                                 UCT_IFACE_FLAG_AM_BCOPY |
                                 UCT_IFACE_FLAG_PENDING  |
                                 UCT_IFACE_FLAG_CB_SYNC  |
                                 UCT_IFACE_FLAG_CONNECT_TO_IFACE;

    return UCS_OK;
}

static ucs_status_t uct_mm_lcoll_iface_query(uct_iface_h tl_iface,
                                             uct_iface_attr_t *iface_attr)
{
    ucs_status_t status = uct_mm_coll_iface_query(tl_iface, iface_attr);
    if (status != UCS_OK) {
        return status;
    }

    iface_attr->cap.am.max_short = 0;
    iface_attr->cap.flags       |= UCT_IFACE_FLAG_INCAST_REDUCABLE;

    /* Update expected performance */
    // TODO: measure and update these numbers
    iface_attr->latency                 = ucs_linear_func_make(80e-9, 0); /* 80 ns */
    iface_attr->overhead                = 10e-9; /* 10 ns */

    return UCS_OK;
}

static ucs_status_t uct_mm_bcoll_iface_query(uct_iface_h tl_iface,
                                             uct_iface_attr_t *iface_attr)
{
    ucs_status_t status = uct_mm_coll_iface_query(tl_iface, iface_attr);
    if (status != UCS_OK) {
        return status;
    }

    /* Set the message length limits */
    uct_mm_coll_iface_t *iface   = ucs_derived_of(tl_iface,uct_mm_coll_iface_t);
    uint8_t procs                = iface->sm_proc_cnt - 1;
    iface_attr->cap.am.max_short = (iface->super.config.fifo_elem_size -
                                    sizeof(uct_mm_coll_fifo_element_t)) / procs;
    iface_attr->cap.am.max_bcopy = iface->super.config.seg_size / procs;
    iface_attr->cap.flags       |= UCT_IFACE_FLAG_AM_SHORT;

    /* Update expected performance */
    // TODO: measure and update these numbers
    iface_attr->latency                 = ucs_linear_func_make(80e-9, 0); /* 80 ns */
    iface_attr->bandwidth.dedicated     = iface->super.super.config.bandwidth;
    iface_attr->bandwidth.shared        = 0;
    iface_attr->overhead                = 10e-9; /* 10 ns */

    return UCS_OK;
}

static ucs_status_t uct_mm_ccoll_iface_query(uct_iface_h tl_iface,
                                             uct_iface_attr_t *iface_attr)
{
    ucs_status_t status = uct_mm_bcoll_iface_query(tl_iface, iface_attr);
    if (status != UCS_OK) {
        return status;
    }

    /* Leave space for the ccoll "ready-flag" */
    iface_attr->cap.am.max_short--;
    iface_attr->cap.am.max_bcopy--;

    /* Update expected performance */
    // TODO: measure and update these numbers
    iface_attr->latency                 = ucs_linear_func_make(80e-9, 0); /* 80 ns */
    iface_attr->overhead                = 10e-9; /* 10 ns */

    return UCS_OK;
}

static ucs_status_t
uct_mm_coll_ep_pending_add(uct_ep_h tl_ep, uct_pending_req_t *n, unsigned flags)
{
    uct_mm_coll_ep_t *ep = ucs_derived_of(tl_ep, uct_mm_coll_ep_t);
    return uct_mm_ep_pending_add((uct_ep_h)ep->tx, n, flags);
}

static void
uct_mm_coll_ep_pending_purge(uct_ep_h tl_ep, uct_pending_purge_callback_t cb, void *arg)
{
    uct_mm_coll_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_mm_coll_iface_t);
    uct_mm_coll_ep_t *ep = ucs_derived_of(tl_ep, uct_mm_coll_ep_t);
    uct_purge_cb_args_t args = {cb, arg};

    ucs_arbiter_group_purge(&iface->super.arbiter, &ep->tx->arb_group,
                            uct_mm_ep_abriter_purge_cb, &args);
    ucs_arbiter_group_purge(&iface->super.arbiter, &ep->rx->arb_group,
                            uct_mm_ep_abriter_purge_cb, &args);
}

static ucs_status_t
uct_mm_coll_ep_flush(uct_ep_h tl_ep, unsigned flags, uct_completion_t *comp)
{
    uct_mm_coll_ep_t *ep = ucs_derived_of(tl_ep, uct_mm_coll_ep_t);
    return uct_mm_ep_flush((uct_ep_h)ep->tx, flags, comp);
}

/**
 * This function is used in some of the cases where a descriptor is being
 * released by the upper layers, during a call to @ref uct_iface_release_desc .
 * Specifically, it is used in cases where the descriptor actually belongs to
 * a remote peer (unlike in @ref uct_mm_ep_release_desc , where the descriptor
 * is guaranteed to be allocated by the same process). These cases are a result
 * of a broadcast, originating from a remote process (via shared memory), where
 * the incoming message had to be handled asynchronously (UCS_INPROGRESS).
 *
 * The function has a complicated task: finding the (MM_COLL_)element which uses
 * the given descriptor, and in this element mark the broadcast as completed
 * (for this worker at least, not necessarily on all the destination workers).
 * Basically, since each (MM_)endpoint keeps a hash-table mapping of segment IDs
 * to segment based addresses - we use that to find the right element (and the
 * rest is easy).
 *
 * @note This looks very inefficient, on first glance. If we have X outstanding
 * bcast elements (if I'm a slow reciever - those could all be waiting on me to
 * process them!) - we may end up checking each of those X, testing if the given
 * descriptor (to be released) belongs to one of them. But in fact, most cases
 * will belong to the first couple of elements, so this is in fact not so bad.
 */
int uct_mm_coll_iface_release_shared_desc(uct_iface_h iface,
                                          uct_recv_desc_t *self, void *desc)
{
    /* Find the endpoint - based on the ID of the sender of this descriptor */
    uct_mm_coll_iface_t *mm_coll_iface = ucs_derived_of(iface,
                                                        uct_mm_coll_iface_t);
    uintptr_t src_coll_id              = (uintptr_t)self;
    uct_mm_coll_ep_t *ep               = mm_coll_iface->eps;

    /*
     * This function may be called with the loopback endpoint - in which case
     * we should notify the upper layer (UCP) that this descriptor in fact does
     * not require releasing
     */
    if (ep->coll_id != src_coll_id) {
        return 1;
    }

    while (++ep->coll_id != src_coll_id) {
        ucs_assert(ep < mm_coll_iface->eps_limit); /* Assume it will be found */
    }

    uct_mm_coll_ep_release_desc(ep, desc);

    return 0;
}

static uct_iface_ops_t uct_mm_coll_iface_ops = {
    .ep_pending_add            = uct_mm_coll_ep_pending_add,
    .ep_pending_purge          = uct_mm_coll_ep_pending_purge,
    .ep_flush                  = uct_mm_coll_ep_flush,
    .ep_fence                  = uct_sm_ep_fence,
    .ep_create                 = uct_mm_coll_ep_create,
    .ep_destroy                = uct_mm_coll_ep_destroy,
    .iface_flush               = uct_mm_iface_flush,
    .iface_fence               = uct_sm_iface_fence,
    .iface_progress_enable     = uct_base_iface_progress_enable,
    .iface_progress_disable    = uct_base_iface_progress_disable,
    .iface_event_fd_get        = uct_mm_iface_event_fd_get,
    .iface_event_arm           = uct_mm_iface_event_fd_arm,
    .iface_get_device_address  = uct_sm_iface_get_device_address,
    .iface_get_address         = uct_mm_coll_iface_get_address,
    .iface_is_reachable        = uct_mm_iface_is_reachable,
    .iface_release_shared_desc = uct_mm_coll_iface_release_shared_desc
};

UCS_CLASS_INIT_FUNC(uct_mm_coll_iface_t, uct_md_h, uct_worker_h,
                    const uct_iface_params_t*, const uct_iface_config_t*);
UCS_CLASS_CLEANUP_FUNC(uct_mm_coll_iface_t);
UCS_CLASS_DEFINE(uct_mm_coll_iface_t, uct_mm_iface_t);
UCS_CLASS_DEFINE_NEW_FUNC(uct_mm_coll_iface_t, uct_iface_t, uct_md_h, uct_worker_h,
                          const uct_iface_params_t*, const uct_iface_config_t*);

#define UCT_MM_COLL_IFACE_SUBCLASS(_subclass_name) \
UCS_CLASS_INIT_FUNC(_subclass_name, uct_md_h, \
                    uct_worker_h, const uct_iface_params_t*, \
                    const uct_iface_config_t*); \
UCS_CLASS_CLEANUP_FUNC(_subclass_name) {} \
UCS_CLASS_DEFINE(_subclass_name, uct_mm_coll_iface_t); \
UCS_CLASS_DEFINE_NEW_FUNC(_subclass_name, uct_mm_coll_iface_t, uct_md_h, \
                          uct_worker_h, const uct_iface_params_t*, \
                          const uct_iface_config_t*); \
UCS_CLASS_DEFINE_DELETE_FUNC(_subclass_name, uct_mm_coll_iface_t); \

UCT_MM_COLL_IFACE_SUBCLASS(uct_mm_lcoll_iface_t);
UCT_MM_COLL_IFACE_SUBCLASS(uct_mm_bcoll_iface_t);
UCT_MM_COLL_IFACE_SUBCLASS(uct_mm_ccoll_iface_t);

static ucs_status_t uct_mm_coll_iface_query_empty(uct_iface_h iface,
                                                  uct_iface_attr_t *iface_attr)
{
    memset(iface_attr, 0, sizeof(*iface_attr));
    return UCS_OK;
}

static void uct_mm_coll_iface_close_empty(uct_iface_h iface)
{
}

static inline void uct_mm_coll_iface_init_ccoll_buffer(uint8_t* buffer,
        size_t size_per_proc, uint32_t proc_cnt)
{
    int i;
    for (i = 1; i < proc_cnt; i++) {
        *(buffer + (i * size_per_proc) - 1) = 0;
    }
}

void uct_mm_coll_iface_init_ccoll_desc(uct_mm_coll_fifo_element_t* fifo_elem_p,
                                       size_t bcopy_size_per_proc,
                                       uint32_t proc_cnt)
{
    uct_mm_coll_iface_init_ccoll_buffer(fifo_elem_p->super.desc_data,
            bcopy_size_per_proc, proc_cnt);
}

static ucs_status_t uct_mm_coll_iface_init_element(size_t short_size_per_proc,
                                                   size_t bcopy_size_per_proc,
                                                   uint32_t proc_cnt,
                                                   uct_mm_coll_fifo_element_t*
                                                   fifo_elem_p)
{
    ucs_assert(fifo_elem_p->super.flags & UCT_MM_FIFO_ELEM_FLAG_OWNER);
    fifo_elem_p->pending = 0;

    /* initialization for "ccoll" mode */
    if (proc_cnt) {
        uct_mm_coll_iface_init_ccoll_desc(fifo_elem_p,
                                          bcopy_size_per_proc,
                                          proc_cnt);

        uct_mm_coll_iface_init_ccoll_buffer((uint8_t*)(fifo_elem_p + 1),
                                            short_size_per_proc,
                                            proc_cnt);
    }

    return ucs_spinlock_init(&fifo_elem_p->lock, UCS_SPINLOCK_FLAG_SHARED);
}

UCS_CLASS_INIT_FUNC(uct_mm_coll_iface_t,
                    uct_md_h md, uct_worker_h worker,
                    const uct_iface_params_t *params,
                    const uct_iface_config_t *tl_config)
{
    int i;
    ucs_status_t status;

    /* check the value defining the size of the FIFO element */
    uct_mm_iface_config_t *mm_config = ucs_derived_of(tl_config,
                                                      uct_mm_iface_config_t);
    if (mm_config->fifo_elem_size < sizeof(uct_mm_coll_fifo_element_t)) {
        ucs_error("The UCX_MM_FIFO_ELEM_SIZE parameter (%u) must be larger "
                  "than, or equal to, the FIFO element header size (%ld bytes).",
                  mm_config->fifo_elem_size, sizeof(uct_mm_coll_fifo_element_t));
        return UCS_ERR_INVALID_PARAM;
    }

    /* Calculate the per-process sizes, for bcoll/ccoll modes */
    uint32_t procs = params->node_info.proc_cnt;
    size_t bcopy_stride = mm_config->seg_size / (procs - 1);
    size_t short_stride = mm_config->fifo_elem_size -
                          sizeof(uct_mm_coll_fifo_element_t);

    /* No need (or way) to initialize anything if no information is given */
    uct_iface_ops_t *super_ops = &self->super.super.super.super.ops;
    if (!(params->field_mask & UCT_IFACE_PARAM_FIELD_COLL_INFO) || (procs <= 2)) {
        super_ops->iface_query = uct_mm_coll_iface_query_empty;
        super_ops->iface_close = uct_mm_coll_iface_close_empty;
        return UCS_OK;
    }

    /* initialize my broadcast FIFO (for TX) */
    void *temp_self = self;
    self = (void*)&self->bcast;
    UCS_CLASS_CALL_SUPER_INIT(uct_mm_iface_t, md, worker, params, tl_config);
    /* overwrite the interface functions (set by UCS_CLASS_CALL_SUPER_INIT()) */
    self->super.super.super.super.ops = uct_mm_coll_iface_ops; /*!= super_ops */

    for (i = 0; i < self->super.config.fifo_size; i++) {
        status = uct_mm_coll_iface_init_element(short_stride,
                bcopy_stride, procs, UCT_MM_COLL_IFACE_GET_FIFO_ELEM(self, i));
        if (status != UCS_OK) {
            goto destory_bcast;
        }
    }

    /* restore self pointer - to switch from the bcast object */
    self = temp_self;

    /* initialize my incoming FIFO (for RX) */
    mm_config->fifo_elem_size = sizeof(uct_mm_coll_fifo_element_t) +
                                (procs - 1) * short_stride;
    status = uct_mm_iface_t_init(&self->super, _myclass->superclass,
            _init_count, md, worker, params, tl_config);
    mm_config->fifo_elem_size = sizeof(uct_mm_coll_fifo_element_t) + short_stride;
    if (status != UCS_OK) {
        goto destory_bcast;
    }
    /* overwrite the interface functions (set by UCS_CLASS_CALL_SUPER_INIT()) */
    *super_ops = uct_mm_coll_iface_ops;

    for (i = 0; i < self->super.config.fifo_size; i++) {
        status = uct_mm_coll_iface_init_element(short_stride,
                bcopy_stride, procs, UCT_MM_COLL_IFACE_GET_FIFO_ELEM(self, i));
        if (status != UCS_OK) {
            goto destory_elements;
        }
    }

    self->my_coll_id   = params->node_info.proc_idx;
    self->sm_proc_cnt  = procs;
    self->ep_cnt       = 8;

    if (ucs_posix_memalign((void**)&self->eps, UCS_SYS_CACHE_LINE_SIZE,
                           8 * sizeof(uct_mm_coll_ep_t), "mm_coll_eps")) {
        status = UCS_ERR_NO_MEMORY;
        goto destory_elements;
    }

    self->eps_limit = self->eps + 1;

    /* create the loopback endpoint (otherwise first progress is corrupt) */
    uct_mm_coll_iface_addr_t loopback_address;
    void *dev_addr = alloca(uct_sm_iface_get_device_addr_len());
    uct_ep_params_t loopback_params = {
            .field_mask = UCT_EP_PARAM_FIELD_IFACE    |
                          UCT_EP_PARAM_FIELD_DEV_ADDR |
                          UCT_EP_PARAM_FIELD_IFACE_ADDR,
            .iface      = &self->super.super.super.super,
            .dev_addr   = (uct_device_addr_t*)dev_addr,
            .iface_addr = (uct_iface_addr_t*)&loopback_address
    };

    status = uct_sm_iface_get_device_address(loopback_params.iface, dev_addr);
    if (status != UCS_OK) {
        goto destroy_eps;
    }

    status = uct_mm_coll_iface_get_address(loopback_params.iface,
            (uct_iface_addr_t*)&loopback_address);
    if (status != UCS_OK) {
        goto destroy_eps;
    }

    status = UCS_CLASS_INIT(uct_mm_coll_ep_t, self->eps, &loopback_params);
    if (status != UCS_OK) {
        goto destroy_eps;
    }

    return UCS_OK;

destroy_eps:
    ucs_free(self->eps);

destory_elements:
    while (i--) {
        uct_mm_coll_fifo_element_t* fifo_elem_p =
                UCT_MM_COLL_IFACE_GET_FIFO_ELEM(self, i);
        ucs_spinlock_destroy(&fifo_elem_p->lock);
    }

    UCS_CLASS_CLEANUP(uct_mm_iface_t, self);

    /* prepare to destroy bcast */
    i = self->super.config.fifo_size;
    self = (void*)&self->bcast;

destory_bcast:
    while (i--) {
        uct_mm_coll_fifo_element_t* fifo_elem_p =
                UCT_MM_COLL_IFACE_GET_FIFO_ELEM(self, i);
        ucs_spinlock_destroy(&fifo_elem_p->lock);
    }

    UCS_CLASS_CLEANUP(uct_mm_iface_t, self);

    return status;
}

UCS_CLASS_INIT_FUNC(uct_mm_lcoll_iface_t, uct_md_h md, uct_worker_h worker,
                    const uct_iface_params_t *params,
                    const uct_iface_config_t *tl_config)
{
    uct_mm_coll_iface_ops.iface_progress = uct_mm_lcoll_iface_progress;
    uct_mm_coll_iface_ops.iface_query    = uct_mm_lcoll_iface_query;
    uct_mm_coll_iface_ops.ep_am_bcopy    = uct_mm_lcoll_ep_am_bcopy;
    uct_mm_coll_iface_ops.iface_close    = (uct_iface_close_func_t)
            UCS_CLASS_DELETE_FUNC_NAME(uct_mm_lcoll_iface_t);

    /* for "locked" mode - no need to allocate any space for "inline" data */
    uct_mm_iface_config_t *mm_config =
            ucs_derived_of(tl_config, uct_mm_iface_config_t);
    mm_config->fifo_elem_size = sizeof(uct_mm_coll_fifo_element_t);

    UCS_CLASS_CALL_SUPER_INIT(uct_mm_coll_iface_t, md, worker, params, tl_config);

    return UCS_OK;
}

UCS_CLASS_INIT_FUNC(uct_mm_bcoll_iface_t, uct_md_h md, uct_worker_h worker,
                    const uct_iface_params_t *params,
                    const uct_iface_config_t *tl_config)
{
    uct_mm_coll_iface_ops.iface_progress = uct_mm_bcoll_iface_progress;
    uct_mm_coll_iface_ops.iface_query    = uct_mm_bcoll_iface_query;
    uct_mm_coll_iface_ops.ep_am_short    = uct_mm_bcoll_ep_am_short;
    uct_mm_coll_iface_ops.ep_am_bcopy    = uct_mm_bcoll_ep_am_bcopy;
    uct_mm_coll_iface_ops.iface_close    = (uct_iface_close_func_t)
            UCS_CLASS_DELETE_FUNC_NAME(uct_mm_bcoll_iface_t);

    UCS_CLASS_CALL_SUPER_INIT(uct_mm_coll_iface_t, md, worker, params, tl_config);

    return UCS_OK;
}

UCS_CLASS_INIT_FUNC(uct_mm_ccoll_iface_t, uct_md_h md, uct_worker_h worker,
                    const uct_iface_params_t *params,
                    const uct_iface_config_t *tl_config)
{
    uct_mm_coll_iface_ops.iface_progress = uct_mm_ccoll_iface_progress;
    uct_mm_coll_iface_ops.iface_query    = uct_mm_ccoll_iface_query;
    uct_mm_coll_iface_ops.ep_am_short    = uct_mm_ccoll_ep_am_short;
    uct_mm_coll_iface_ops.ep_am_bcopy    = uct_mm_ccoll_ep_am_bcopy;
    uct_mm_coll_iface_ops.iface_close    = (uct_iface_close_func_t)
            UCS_CLASS_DELETE_FUNC_NAME(uct_mm_ccoll_iface_t);

    UCS_CLASS_CALL_SUPER_INIT(uct_mm_coll_iface_t, md, worker, params, tl_config);

    return UCS_OK;
}

UCS_CLASS_CLEANUP_FUNC(uct_mm_coll_iface_t)
{
    void *temp_self = self;
    self = (void*)&self->bcast;

    int i;
    uct_mm_coll_fifo_element_t* fifo_elem_p;
    for (i = 0; i < self->super.config.fifo_size; i++) {
        fifo_elem_p = UCT_MM_COLL_IFACE_GET_FIFO_ELEM(self, i);
        ucs_spinlock_destroy(&fifo_elem_p->lock);
    }

    UCS_CLASS_CLEANUP(uct_mm_iface_t, self); /* Note: this is NOT super */
    self = temp_self;

    for (i = 0; i < self->super.config.fifo_size; i++) {
        fifo_elem_p = UCT_MM_COLL_IFACE_GET_FIFO_ELEM(self, i);
        ucs_spinlock_destroy(&fifo_elem_p->lock);
    }

    ucs_free(self->eps);
}
