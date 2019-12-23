/**
* Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/
#include "ud_comet.h"

#include <uct/sm/mm/base/mm_md.h>

static ucs_config_field_t uct_ud_comet_iface_config_table[] = {
  {"UD_", "", NULL,
   ucs_offsetof(uct_ud_comet_iface_config_t, super),
   UCS_CONFIG_TYPE_TABLE(uct_ud_iface_config_table)},

  {"", "", NULL,
   ucs_offsetof(uct_ud_comet_iface_config_t, mlx5_common),
   UCS_CONFIG_TYPE_TABLE(uct_ib_mlx5_iface_config_table)},

  {"", "", NULL,
   ucs_offsetof(uct_ud_comet_iface_config_t, ud_mlx5_common),
   UCS_CONFIG_TYPE_TABLE(uct_ud_mlx5_iface_common_config_table)},

  {"DEVICE_INDEX", "0", "The COMET device (FGPA) index",
   ucs_offsetof(uct_ud_comet_iface_config_t, device_index), UCS_CONFIG_TYPE_UINT},

  {NULL}
};

extern UCS_CLASS_INIT_FUNC(uct_ud_mlx5_iface_t,
                           uct_md_h md, uct_worker_h worker,
                           const uct_iface_params_t *params,
                           const uct_iface_config_t *tl_config);

static ucs_status_t
uct_ud_comet_ep_get_address(uct_ep_h ep, uct_ep_addr_t *ep_addr)
{
    uct_ud_comet_ep_t *comet_ep  = ucs_derived_of(ep, uct_ud_comet_ep_t);
    uct_ud_comet_iface_t *iface  = ucs_derived_of(ep->iface, uct_ud_comet_iface_t);
    uct_ud_comet_ep_addr_t *addr = ucs_derived_of(ep_addr, uct_ud_comet_ep_addr_t);

    memcpy(&addr->device_caps[0], &iface->device_caps, sizeof(addr->device_caps));

    unsigned type_idx;
    for (type_idx = 0; type_idx < UCT_UD_COMET_COLL_TYPE_LAST; type_idx++) {
        comet_ep->header[type_idx].table_id = addr->table_id[type_idx];
    }

    return iface->super_get_addr(ep, ep_addr);
}

static ucs_status_t
uct_ud_comet_ep_connect_to_ep(uct_ep_h ep,
                              const uct_device_addr_t *dev_addr,
                              const uct_ep_addr_t *ep_addr)
{
    uct_ud_comet_iface_t *iface  = ucs_derived_of(ep->iface, uct_ud_comet_iface_t);
    uct_ud_comet_ep_t *comet_ep  = ucs_derived_of(ep, uct_ud_comet_ep_t);
    uct_ud_comet_ep_addr_t *addr = ucs_derived_of(ep_addr, uct_ud_comet_ep_addr_t);

    unsigned type_idx;
    for (type_idx = 0; type_idx < UCT_UD_COMET_COLL_TYPE_LAST; type_idx++) {
        comet_ep->header[type_idx].table_id = addr->table_id[type_idx];
    }

    return iface->super_connect(ep, dev_addr, ep_addr);
}

static unsigned
uct_ud_comet_iface_progress(uct_iface_h tl_iface)
{
    uct_ud_comet_iface_t *iface = ucs_derived_of(tl_iface, uct_ud_comet_iface_t);

    unsigned idx;
    uint64_t prefix;
    for (idx = 0; idx < iface->table_cnt; idx++) {
        if ((prefix = iface->tables[idx].incoming_prefix) != 0) {
            void *data = iface->tables[idx].incoming_data_va;
            size_t len = iface->tables[idx].data_length;

            iface->tables[idx].tag_ctx.completed_cb(data /*tag_ctx*/,
                    0 /*stag*/, prefix /*imm*/, len, UCS_OK);

            iface->tables[idx].incoming_prefix = 0;
            return 1;
        }
    }

    return iface->super_progress(tl_iface); /* uct_ud_mlx5_iface_progress */
}

static ucs_status_t
uct_ud_comet_ep_am_zcopy(uct_ep_h tl_ep, uint8_t id, const void *header,
                         unsigned header_length, const uct_iov_t *iov,
                         size_t iovcnt, unsigned flags,
                         uct_completion_t *comp)
{
    uct_ud_comet_iface_t *iface        = ucs_derived_of(tl_ep->iface, uct_ud_comet_iface_t);
    uct_ud_comet_ep_t *comet_ep        = ucs_derived_of(tl_ep, uct_ud_comet_ep_t);
    struct comet_packet_header *packet = &comet_ep->header[0];
    uint64_t prefix                    = *(uint64_t*)header;
    uint8_t slot_id                    = iface->my_group_index;
    uint8_t table_id                   = id; /* WARNING: parameter type abuse*/

    /* Assume enough space in SGE[0] for the COMET header*/
    comet_packet_header_init(packet, table_id, slot_id, prefix);
    ucs_assert(iface->comet_ref == NULL);
    ucs_assert(prefix != 0);

    /* Call super function */
    return iface->super_uct_ud_ep_am_zcopy(tl_ep, UCT_UD_COMET_AM_ID, packet,
            sizeof(struct comet_packet_header), iov, 1, flags, comp);
}

static ucs_status_t
uct_ud_comet_iface_tag_recv_zcopy(uct_iface_h iface, uct_tag_t tag,
                                  uct_tag_t tag_mask, const uct_iov_t *iov,
                                  size_t iovcnt, uct_tag_context_t *ctx)
{
    uct_ud_comet_iface_t *comet = ucs_derived_of(iface, uct_ud_comet_iface_t);
    uint64_t comet_prefix       = (uint64_t)tag;
    void *base_address          = iov[0].buffer;
    uint32_t table_index        = iovcnt;     /* WARNING: parameter type abuse*/
    uct_ud_comet_table_t *table = &comet->tables[table_index];
    uct_ud_comet_table_t *table_pa = (uct_ud_comet_table_t *)comet_buffer_phys_address_get(comet->tables);

    table_pa = &table_pa[table_index];

    /* Sanity checks */
    ucs_assert(comet_prefix != 0);
    ucs_assert(tag_mask == (uct_tag_t)-1);
    ucs_assert(ctx->completed_cb != NULL);
    ucs_assert(comet->comet_ref != NULL);

    table->tag_ctx = *ctx;
    table->data_length = iov[0].length;
    if (table->incoming_data_va != base_address) {
        table->incoming_data_va = base_address;
        table->incoming_data_pa = comet_buffer_phys_address_get(base_address);
    }

    /* Set the request in the COMET table */
    int ret = comet_recv(comet->comet_ref, table_index,
                         (uintptr_t)&table_pa->incoming_prefix,
                         (uintptr_t)table->incoming_data_pa);
    return ret ? UCS_ERR_IO_ERROR : UCS_OK;
}

static UCS_CLASS_CLEANUP_FUNC(uct_ud_comet_iface_t)
{
    if (self->comet_ref) {
        comet_close(self->comet_ref);
    }
}

static UCS_CLASS_DECLARE_DELETE_FUNC(uct_ud_comet_iface_t, uct_iface_t);

static ucs_status_t uct_ud_comet_iface_query(uct_iface_h tl_iface,
                                             uct_iface_attr_t *iface_attr)
{
    uct_ud_comet_iface_t *comet = ucs_derived_of(tl_iface, uct_ud_comet_iface_t);
    ucs_status_t status = comet->super_iface_query(tl_iface, iface_attr);
    if (status != UCS_OK) {
        return status;
    }

    iface_attr->cap.flags  |= UCT_IFACE_FLAG_BCAST | UCT_IFACE_FLAG_INCAST;
    iface_attr->ep_addr_len = sizeof(uct_ud_comet_ep_addr_t);

    return UCS_OK;
}

static void
ud_comet_ops_mlx5_reuse_initialize(uct_ud_comet_iface_t *comet_iface)
{
    uct_iface_ops_t *ops  = &comet_iface->super.super.super.super.super.ops;

    /* Store old function pointers */
    comet_iface->super_progress = ops->iface_progress;
    comet_iface->super_get_addr = ops->ep_get_address;
    comet_iface->super_connect  = ops->ep_connect_to_ep;
    comet_iface->super_uct_ud_ep_am_zcopy = ops->ep_am_zcopy;
    comet_iface->super_iface_query    = ops->iface_query;

    /* Replace with new function pointers */
    ops->iface_query            = uct_ud_comet_iface_query;
    ops->iface_close            = UCS_CLASS_DELETE_FUNC_NAME(uct_ud_comet_iface_t);
    ops->iface_progress         = uct_ud_comet_iface_progress;
    ops->ep_get_address         = uct_ud_comet_ep_get_address;
    ops->ep_connect_to_ep       = uct_ud_comet_ep_connect_to_ep;
    ops->iface_tag_recv_zcopy   = uct_ud_comet_iface_tag_recv_zcopy;
    ops->ep_am_zcopy            = uct_ud_comet_ep_am_zcopy;
}

static UCS_CLASS_INIT_FUNC(uct_ud_comet_iface_t,
                           uct_md_h md, uct_worker_h worker,
                           const uct_iface_params_t *params,
                           const uct_iface_config_t *tl_config)
{
    /* Initialize interface ops */
    const uct_ud_comet_iface_config_t *config =
            ucs_derived_of(tl_config, uct_ud_comet_iface_config_t);
    UCS_CLASS_CALL_SUPER_INIT(uct_ud_mlx5_iface_t, md,
                              worker, params, &config->super.super.super);
    ud_comet_ops_mlx5_reuse_initialize(self);
    self->device_caps = NULL;
    self->comet_ref = NULL;

    /* Get the attributes of this MD for connection establishment later */
    ucs_status_t status = uct_mm_md_query(md, &self->md_attr);
    if (status != UCS_OK) {
        return status;
    }

    /* Query the COMET device capabilities */
    uint32_t num_comet_devices;
    int ret = comet_query_capabilities(&self->device_caps, &num_comet_devices);
    if (ret != 0) {
        ucs_error("Failed to query COMET capabilities");
        return UCS_ERR_NO_RESOURCE;
    }

    /* If no devices were found - this is a COMET client */
    if (num_comet_devices == 0) {
        return UCS_OK;
    }

    if (config->device_index >= num_comet_devices) {
        ucs_error("Failed to choose the requested COMET device");
        return UCS_ERR_NO_DEVICE;
    }

    /* Open device driver */
    ret = comet_init(config->device_index, &self->comet_ref);
    if ((ret != 0) || (self->comet_ref == NULL)) {
        ucs_debug("Failed to initialize COMET");
        return UCS_ERR_NO_MEMORY;
    }

    /* Generate per-table information based on the COMET device capabilities */
    // TODO: support more than one COMET device?
    self->table_cnt = self->device_caps[0].num_tables;
    self->tables = comet_alloc(self->comet_ref,
            self->table_cnt * sizeof(uct_ud_comet_table_t));
    if (self->tables == NULL) {
        ucs_error("Failed to allocate COMET memory");
        return UCS_ERR_NO_MEMORY;
    }

    unsigned idx;
    for (idx = 0; idx < self->table_cnt; idx++) {
        self->tables[idx].collective_type  = UCT_UD_COMET_COLL_TYPE_REDUCE; // TODO: detect
        self->tables[idx].incoming_data_va = NULL;
        self->tables[idx].incoming_prefix  = 0;
    }

    self->group_proc_cnt = params->node_info.proc_cnt;
    self->my_group_index = params->node_info.proc_idx;
    return UCS_OK;
}

UCS_CLASS_DEFINE(uct_ud_comet_iface_t, uct_ud_iface_t);
static UCS_CLASS_DEFINE_NEW_FUNC(uct_ud_comet_iface_t, uct_iface_t, uct_md_h,
                                 uct_worker_h, const uct_iface_params_t*,
                                 const uct_iface_config_t*);
static UCS_CLASS_DEFINE_DELETE_FUNC(uct_ud_comet_iface_t, uct_iface_t);

static ucs_status_t
uct_ud_comet_query_resources(uct_md_h md,
                            uct_tl_resource_desc_t **resources_p,
                            unsigned *num_resources_p)
{
    /* Query (MLX5, accelerated) UD resources */
    ucs_status_t status = uct_ib_device_query_tl_resources(&ucs_derived_of(md, uct_ib_md_t)->dev,
            UCT_UD_COMET_TL_NAME, UCT_IB_DEVICE_FLAG_MLX5_PRM, resources_p, num_resources_p );

    if (status != UCS_OK) {
        return status;
    }

    /* TODO: take COMET overhead into account in the resource latency estimation... */
    if (*num_resources_p == 0) {
        ucs_debug("MLX5 UD devices not found for COMET");
        return UCS_ERR_NO_DEVICE;
    }

    uint32_t tmp_num_resources;

    const struct comet_capabilities *comet_devices;
    int ret = comet_query_capabilities(&comet_devices, &tmp_num_resources );
    if (ret != 0) {
        ucs_error("Failed to query COMET capabilities");
        *num_resources_p = 0;
        return UCS_ERR_NO_DEVICE;
    }

	*num_resources_p = (unsigned) tmp_num_resources;

    /* No COMET device? ==> CLIENT mode */
    printf("num_resources_p = %p\n", num_resources_p);

    if (*num_resources_p == 0) {
        ucs_debug("COMET device not found, initializing UD_COMET as client");
    }

    return UCS_OK;
}

UCT_TL_COMPONENT_DEFINE(uct_ud_comet_tl,
                        uct_ud_comet_query_resources,
                        uct_ud_comet_iface_t,
                        UCT_UD_COMET_TL_NAME,
                        "UD_COMET_",
                        uct_ud_comet_iface_config_table,
                        uct_ud_comet_iface_config_t);
UCT_MD_REGISTER_TL(&uct_ib_mdc, &uct_ud_comet_tl);
