/**
 * Copyright (c) UT-Battelle, LLC. 2014-2015. ALL RIGHTS RESERVED.
 * Copyright (C) Mellanox Technologies Ltd. 2001-2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <ucs/sys/string.h>
#include <uct/sm/base/sm_ep.h>
#include <uct/sm/base/sm_iface.h>
#include <uct/sm/mm/base/mm_ep.h>
#include <ucs/debug/memtrack.h>

#include "mm_coll_iface.h"
#include "mm_coll_ep.h"

extern ucs_config_field_t uct_mm_iface_config_table[];

static ucs_config_field_t uct_mm_coll_iface_config_table[] = {
    {"", "ALLOC=md", NULL,
     ucs_offsetof(uct_mm_coll_iface_config_t, super),
     UCS_CONFIG_TYPE_TABLE(uct_mm_iface_config_table)},

    {NULL}
};

static ucs_status_t uct_mm_coll_iface_get_address(uct_iface_t *tl_iface,
                                                  uct_iface_addr_t *addr)
{
    uct_mm_coll_iface_t *iface = ucs_derived_of(tl_iface, uct_mm_coll_iface_t);
    uct_mm_coll_iface_addr_t *iface_addr = (void*)addr;

    iface_addr->rx.id    = iface->super.fifo_mm_id;
    iface_addr->rx.vaddr = (uintptr_t)iface->super.shared_mem;
    iface_addr->tx.id    = iface->bcast_fifo_mm_id;
    iface_addr->tx.vaddr = (uintptr_t)iface->bcast_shared_mem;
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

    iface_attr->cap.flags = UCT_IFACE_FLAG_BCAST    |
                            UCT_IFACE_FLAG_INCAST   |
                            UCT_IFACE_FLAG_AM_SHORT |
                            UCT_IFACE_FLAG_AM_BCOPY |
                            UCT_IFACE_FLAG_PENDING  |
                            UCT_IFACE_FLAG_CB_SYNC  |
                            UCT_IFACE_FLAG_CONNECT_TO_IFACE;

    iface_attr->iface_addr_len = sizeof(uct_mm_coll_iface_addr_t);

    return UCS_OK;
}

static UCS_CLASS_DECLARE_DELETE_FUNC(uct_mm_coll_iface_t, uct_iface_t);

static uct_iface_ops_t uct_mm_coll_iface_ops = {
    .ep_am_short              = uct_mm_coll_ep_am_short,
    .ep_am_bcopy              = uct_mm_coll_ep_am_bcopy,
    .ep_am_slock              = uct_mm_coll_ep_am_slock,
    .ep_am_block              = uct_mm_coll_ep_am_block,
    .ep_pending_add           = uct_mm_ep_pending_add,
    .ep_pending_purge         = uct_mm_ep_pending_purge,
    .ep_flush                 = uct_mm_ep_flush,
    .ep_fence                 = uct_sm_ep_fence,
    .ep_create                = UCS_CLASS_NEW_FUNC_NAME(uct_mm_coll_ep_t),
    .ep_destroy               = UCS_CLASS_DELETE_FUNC_NAME(uct_mm_coll_ep_t),
    .iface_flush              = uct_mm_iface_flush,
    .iface_fence              = uct_sm_iface_fence,
    .iface_progress_enable    = uct_base_iface_progress_enable,
    .iface_progress_disable   = uct_base_iface_progress_disable,
    .iface_progress           = (void*)uct_mm_coll_iface_progress,
    .iface_close              = UCS_CLASS_DELETE_FUNC_NAME(uct_mm_coll_iface_t),
    .iface_query              = uct_mm_coll_iface_query,
    .iface_get_device_address = uct_sm_iface_get_device_address,
    .iface_get_address        = uct_mm_coll_iface_get_address,
    .iface_is_reachable       = uct_sm_iface_is_reachable
};

static ucs_status_t uct_mm_coll_allocate_fifo_mem(uct_mm_coll_iface_t *iface,
                                                  uct_mm_iface_config_t *config,
                                                  uct_md_h md)
{
    uct_mm_fifo_ctl_t *ctl;
    size_t size_to_alloc;
    ucs_status_t status;

    /* allocate the receive FIFO */
    size_to_alloc = UCT_MM_GET_FIFO_SIZE(&iface->super);

    status = uct_mm_md_mapper_ops(md)->alloc(md, &size_to_alloc, config->hugetlb_mode,
                                             0, "mm coll fifo", &iface->bcast_shared_mem,
                                             &iface->bcast_fifo_mm_id, &iface->super.path);
    if (status != UCS_OK) {
        ucs_error("Failed to allocate memory for the receive FIFO in mm. size: %zu : %m",
                   size_to_alloc);
        return status;
    }

    ctl = uct_mm_set_fifo_ctl(iface->bcast_shared_mem);
    uct_mm_set_fifo_elems_ptr(iface->bcast_shared_mem, &iface->bcast_fifo_elements);

    /* Make sure head and tail are cache-aligned, and not on same cacheline, to
     * avoid false-sharing.
     */
    ucs_assert_always((((uintptr_t)&ctl->head) % UCS_SYS_CACHE_LINE_SIZE) == 0);
    ucs_assert_always((((uintptr_t)&ctl->tail) % UCS_SYS_CACHE_LINE_SIZE) == 0);
    ucs_assert_always(((uintptr_t)&ctl->tail - (uintptr_t)&ctl->head) >= UCS_SYS_CACHE_LINE_SIZE);

    iface->bcast_fifo_ctl = ctl;

    ucs_assert(iface->bcast_shared_mem != NULL);
    return UCS_OK;
}

static ucs_status_t uct_mm_coll_iface_query_empty(uct_iface_h tl_iface,
                                                  uct_iface_attr_t *iface_attr)
{
    memset(iface_attr, 0, sizeof(*iface_attr));
    return UCS_OK;
}

static UCS_CLASS_INIT_FUNC(uct_mm_coll_iface_t, uct_md_h md, uct_worker_h worker,
                           const uct_iface_params_t *params,
                           const uct_iface_config_t *tl_config)
{
    int i;
    ucs_status_t status;
    uct_mm_fifo_element_t* mm_fifo_elem_p;
    uct_mm_coll_fifo_element_t* mm_coll_fifo_elem_p;

    /* No need (or way) to initialize anything if no information is given */
    if (!(params->field_mask & UCT_IFACE_PARAM_FIELD_COLL_INFO)) {
        self->super.super.super.ops.iface_query = uct_mm_coll_iface_query_empty;
        return UCS_OK;
    }

    UCT_CHECK_PARAM(params->node_info.proc_cnt < 8 * sizeof(mm_coll_fifo_elem_p->pending),
            "Number of group members exceeds the supported maximum");
    UCT_CHECK_PARAM(params->node_info.proc_idx < 8 * sizeof(mm_coll_fifo_elem_p->pending),
            "Group member ID exceeds the supported maximum");

    UCS_CLASS_CALL_SUPER_INIT(uct_mm_iface_t, md, worker, params, tl_config);

    status = uct_mm_md_query(md, &self->md_attr);
    if (status != UCS_OK) {
        return status;
    }

    /* create the receive FIFO */
    /* use specific allocator to allocate and attach memory and check the
     * requested hugetlb allocation mode */
    status = uct_mm_coll_allocate_fifo_mem(self, ucs_derived_of(tl_config,
            uct_mm_iface_config_t), md);
    if (status != UCS_OK) {
        return status;
    }

    for (i = 0; i < self->super.config.fifo_size; i++) {
        /* Initialize the recv-FIFO */
        mm_fifo_elem_p = UCT_MM_IFACE_GET_FIFO_ELEM(&self->super,
                self->super.recv_fifo_elements, i);
        mm_coll_fifo_elem_p = ucs_container_of(mm_fifo_elem_p,
                uct_mm_coll_fifo_element_t, super);
        mm_coll_fifo_elem_p->super.flags = UCT_MM_FIFO_ELEM_FLAG_OWNER;
        mm_coll_fifo_elem_p->pending = 0;

        status = ucs_spinlock_init_sm(&mm_coll_fifo_elem_p->lock);
        if (status != UCS_OK) {
            return status;
        }

        /* Initialize the send-FIFO */
        mm_fifo_elem_p = UCT_MM_IFACE_GET_FIFO_ELEM(&self->super,
                self->bcast_fifo_elements, i);
        mm_coll_fifo_elem_p = ucs_container_of(mm_fifo_elem_p,
                uct_mm_coll_fifo_element_t, super);
        mm_coll_fifo_elem_p->super.flags = UCT_MM_FIFO_ELEM_FLAG_OWNER;
        mm_coll_fifo_elem_p->pending = 0;

        status = ucs_spinlock_init_sm(&mm_coll_fifo_elem_p->lock);
        if (status != UCS_OK) {
            return status;
        }
    }

    /* Fill-in interface fields */
    size_t eps_size             = (params->node_info.proc_cnt + 1) *
                                  sizeof(uct_mm_coll_peer_ep_t);
    self->eps                   = UCS_ALLOC_CHECK(eps_size, "mm_coll_ep_slots");
    self->bcast_fifo_ctl->head  = 0;
    self->bcast_fifo_ctl->tail  = 0;
    self->bcast_index           = 0;
    self->my_coll_id            = params->node_info.proc_idx;
    self->my_mask               = UCS_BIT(params->node_info.proc_idx);
    self->peer_mask             = UCS_MASK(params->node_info.proc_cnt) & ~self->my_mask;
    memset(self->eps, 0, eps_size);

    /* Prepare the loop-back address */
    uint64_t sm_dev_addr;
    uct_mm_coll_iface_addr_t my_addr;
    status = uct_sm_iface_get_device_address((uct_iface_t*)self,
                                             (uct_device_addr_t*)&sm_dev_addr);
    ucs_assert(status == UCS_OK);
    status = uct_mm_coll_iface_get_address((uct_iface_t*)self,
                                           (uct_iface_addr_t*)&my_addr);
    ucs_assert(status == UCS_OK);
    uct_mm_coll_iface_addr_t bcast_addr = {
            .rx      = my_addr.tx,
            .tx      = my_addr.rx,
            .coll_id = my_addr.coll_id
    };

    /* Connect the loop-back endpoint */
    uct_ep_params_t bcast_params = {
            .field_mask = UCT_EP_PARAM_FIELD_IFACE    |
                          UCT_EP_PARAM_FIELD_DEV_ADDR |
                          UCT_EP_PARAM_FIELD_IFACE_ADDR,
            .iface      = (uct_iface_h)self,
            .dev_addr   = (uct_device_addr_t*)&sm_dev_addr,
            .iface_addr = (uct_iface_addr_t*)&bcast_addr
    };
    self->eps[0].peer_id = UCT_MM_COLL_MY_PEER_ID;
    status = uct_mm_coll_iface_ops.ep_create(&bcast_params,
            (uct_ep_t**)&self->eps[0].ep);
    if (status != UCS_OK) {
        return status;
    }

    /* Only for the bcast endpoint - transfer ownership on (pending == 0) */
    self->eps[0].ep->tx_peer_mask = self->peer_mask;

    /* Overwrite the operations set by the super-class */
    self->super.super.super.ops = uct_mm_coll_iface_ops;

    ucs_debug("Created an MM_COLL iface. FIFO mm id: %zu , coll info: %u/%u",
              self->super.fifo_mm_id, params->node_info.proc_idx,
              params->node_info.proc_cnt);

    return UCS_OK;
}


static UCS_CLASS_CLEANUP_FUNC(uct_mm_coll_iface_t)
{
    /* return all the descriptors that are now 'assigned' to the FIFO,
     * to their mpool (uct_mm_iface_free_rx_descs) */
    uct_mm_fifo_element_t* fifo_elem_p;
    uct_mm_recv_desc_t *desc;
    unsigned i;

    for (i = 0; i < self->super.config.fifo_size; i++) {
        fifo_elem_p = UCT_MM_IFACE_GET_FIFO_ELEM(&self->super,
                                                 self->bcast_fifo_elements, i);
        desc = UCT_MM_IFACE_GET_DESC_START(&self->super, fifo_elem_p);
        ucs_mpool_put(desc);
    }

    ucs_free(self->eps);

    /* release the memory allocated for the FIFO */
    size_t size_to_free      = UCT_MM_GET_FIFO_SIZE(&self->super);
    uct_mm_mapper_ops_t *ops = uct_mm_md_mapper_ops(self->super.super.md);
    ucs_status_t status      = ops->free(self->bcast_shared_mem,
                                         self->bcast_fifo_mm_id,
                                         size_to_free,
                                         self->super.path);
    if (status != UCS_OK) {
        ucs_warn("Unable to release shared memory segment: %m");
    }
}

UCS_CLASS_DEFINE(uct_mm_coll_iface_t, uct_mm_iface_t);

static UCS_CLASS_DEFINE_NEW_FUNC(uct_mm_coll_iface_t, uct_iface_t, uct_md_h,
                                 uct_worker_h, const uct_iface_params_t *,
                                 const uct_iface_config_t *);
static UCS_CLASS_DEFINE_DELETE_FUNC(uct_mm_coll_iface_t, uct_iface_t);

static ucs_status_t uct_mm_coll_query_tl_resources(uct_md_h md,
                                                   uct_tl_resource_desc_t **resource_p,
                                                   unsigned *num_resources_p)
{
    uct_tl_resource_desc_t *resource;

    resource = ucs_calloc(1, sizeof(uct_tl_resource_desc_t), "resource desc");
    if (NULL == resource) {
        ucs_error("Failed to allocate memory");
        return UCS_ERR_NO_MEMORY;
    }

    ucs_snprintf_zero(resource->tl_name, sizeof(resource->tl_name), "%s",
                      UCT_MM_COLL_TL_NAME);
    ucs_snprintf_zero(resource->dev_name, sizeof(resource->dev_name), "%s",
                      md->component->name);
    resource->dev_type = UCT_DEVICE_TYPE_SHM;

    *num_resources_p = 1;
    *resource_p      = resource;
    return UCS_OK;
}

UCT_TL_COMPONENT_DEFINE(uct_mm_coll_tl,
                        uct_mm_coll_query_tl_resources,
                        uct_mm_coll_iface_t,
                        UCT_MM_COLL_TL_NAME,
                        "MM_COLL_",
                        uct_mm_coll_iface_config_table,
                        uct_mm_coll_iface_config_t);
