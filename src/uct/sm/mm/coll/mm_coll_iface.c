/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <ucs/sys/string.h>
#include <ucs/debug/memtrack.h>
#include <uct/sm/base/sm_ep.h>
#include <uct/sm/base/sm_iface.h>
#include <uct/sm/mm/base/mm_ep.h>

#include "mm_coll_iface.h"
#include "mm_coll_ep.h"


ucs_status_t uct_mm_coll_iface_query(uct_iface_h tl_iface,
                                     uct_iface_attr_t *iface_attr)
{
    ucs_status_t status = uct_mm_iface_query(tl_iface, iface_attr);
    if (status != UCS_OK) {
        return status;
    }

    iface_attr->cap.atomic32.op_flags     =
    iface_attr->cap.atomic32.fop_flags    =
    iface_attr->cap.atomic64.op_flags     =
    iface_attr->cap.atomic64.fop_flags    = 0; /* TODO: use in MPI_Accumulate */
    iface_attr->iface_addr_len            = sizeof(uct_mm_coll_iface_addr_t);
    iface_attr->cap.flags                 = UCT_IFACE_FLAG_AM_SHORT          |
                                            UCT_IFACE_FLAG_AM_BCOPY          |
                                            UCT_IFACE_FLAG_AM_ZCOPY          |
                                            UCT_IFACE_FLAG_PENDING           |
                                            UCT_IFACE_FLAG_CB_SYNC           |
                                            UCT_IFACE_FLAG_CONNECT_TO_IFACE;
    iface_attr->cap.am.coll_mode_flags    = UCS_BIT(UCT_COLL_DTYPE_MODE_PADDED) |
                                            UCS_BIT(UCT_COLL_DTYPE_MODE_PACKED);
    iface_attr->cap.coll_mode.short_flags = UCS_BIT(UCT_COLL_DTYPE_MODE_PADDED) |
                                            UCS_BIT(UCT_COLL_DTYPE_MODE_PACKED);
    iface_attr->cap.coll_mode.bcopy_flags = UCS_BIT(UCT_COLL_DTYPE_MODE_PADDED) |
                                            UCS_BIT(UCT_COLL_DTYPE_MODE_PACKED);
    iface_attr->cap.coll_mode.zcopy_flags = 0; /* TODO: implement... */

    return UCS_OK;
}

ucs_status_t uct_mm_coll_iface_get_address(uct_iface_t *tl_iface,
                                           uct_iface_addr_t *addr)
{
    uct_mm_coll_iface_addr_t *iface_addr = (void*)addr;
    uct_mm_coll_iface_t *iface           = ucs_derived_of(tl_iface,
                                                          uct_mm_coll_iface_t);
    iface_addr->coll_id                  = iface->my_coll_id;

    return uct_mm_iface_get_address(tl_iface, addr);
}

static ucs_status_t uct_mm_coll_iface_query_empty(uct_iface_h iface,
                                                  uct_iface_attr_t *iface_attr)
{
    memset(iface_attr, 0, sizeof(*iface_attr));
    return UCS_OK;
}

static void uct_mm_coll_iface_close_empty(uct_iface_h iface)
{
}

int uct_mm_coll_iface_is_unusable(uct_mm_coll_iface_t *iface,
                                  const uct_iface_params_t *params)
{
    /* No need (or way) to initialize anything if no information is given */
    if (((params->field_mask & UCT_IFACE_PARAM_FIELD_COLL_INFO) == 0) ||
        (params->node_info.proc_cnt <= 2)) {
        uct_iface_ops_t *super_ops = &iface->super.super.super.super.ops;
        memset(super_ops, 0, sizeof(uct_iface_ops_t));
        super_ops->iface_query = uct_mm_coll_iface_query_empty;
        super_ops->iface_close = uct_mm_coll_iface_close_empty;
        return 1;
    }

    return 0;
}

UCS_CLASS_INIT_FUNC(uct_mm_coll_iface_t, uct_iface_ops_t *ops, uct_md_h md,
                    uct_worker_h worker, const uct_iface_params_t *params,
                    const uct_iface_config_t *tl_config)
{
    /* check the value defining the size of the FIFO element */
    uct_mm_iface_config_t *mm_config = ucs_derived_of(tl_config,
                                                      uct_mm_iface_config_t);
    if (mm_config->fifo_elem_size < sizeof(uct_mm_coll_fifo_element_t)) {
        ucs_error("The UCX_MM_FIFO_ELEM_SIZE parameter (%u) must be larger "
                  "than, or equal to, the FIFO element header size (%ld bytes).",
                  mm_config->fifo_elem_size, sizeof(uct_mm_coll_fifo_element_t));
        return UCS_ERR_INVALID_PARAM;
    }

    ucs_assert(!uct_mm_coll_iface_is_unusable(self, params));

    UCS_CLASS_CALL_SUPER_INIT(uct_mm_base_iface_t, ops, md, worker, params, tl_config);

    self->my_coll_id  = params->node_info.proc_idx;
    self->sm_proc_cnt = params->node_info.proc_cnt;
    self->loopback_ep = NULL;

    ucs_ptr_array_init(&self->ep_ptrs, "mm_coll_eps");

    return UCS_OK;
}

UCS_CLASS_CLEANUP_FUNC(uct_mm_coll_iface_t)
{
    ucs_ptr_array_cleanup(&self->ep_ptrs);
}

UCS_CLASS_DEFINE(uct_mm_coll_iface_t, uct_mm_base_iface_t);
