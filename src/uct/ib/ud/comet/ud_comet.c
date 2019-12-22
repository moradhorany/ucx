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

/* Indicates that UD_COMET has been initialized successfully */
static int g_ud_comet_initialized = 0;

static UCS_CLASS_DEFINE_DELETE_FUNC(uct_ud_comet_iface_t, uct_iface_t);

extern UCS_CLASS_INIT_FUNC(uct_ud_mlx5_iface_t,
                           uct_md_h md, uct_worker_h worker,
                           const uct_iface_params_t *params,
                           const uct_iface_config_t *tl_config);

static ucs_status_t
uct_ud_comet_ep_get_address(uct_ep_h ep, uct_ep_addr_t *ep_addr)
{
     uct_ud_comet_iface_t *iface  = ucs_derived_of(ep->iface, uct_ud_comet_iface_t);
    uct_ud_comet_ep_t *comet_ep  = ucs_derived_of(ep, uct_ud_comet_ep_t);
    uct_ud_comet_ep_addr_t *addr = ucs_derived_of(ep_addr, uct_ud_comet_ep_addr_t);

    memcpy(&addr->comet_device_capabilities, &iface->comet_device_capabilities, sizeof(iface->comet_device_capabilities));

    unsigned type_idx;
    for (type_idx = 0; type_idx < UCT_UD_COMET_COLL_TYPE_LAST; type_idx++) {
        comet_ep->header[type_idx].table_id = addr->table_id[type_idx];
    }

    // Shuki: Should we call super here?
    return UCS_OK;
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
    uct_ud_comet_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_ud_comet_iface_t);
    uct_ud_comet_ep_t *comet_ep  = ucs_derived_of(tl_ep, uct_ud_comet_ep_t);
    struct comet_packet_header *packet_header = &comet_ep->header[0];
    uint64_t tag = 0; // Shuki: TODO - Unknown.
    register uint64_t comet_prefix = (uint64_t)tag;
    uint8_t slot_id = iface->my_group_index;
    uint32_t table_index        = iovcnt;     /* WARNING: parameter type abuse*/

    /* Sanity checks */
    ucs_assert(comet_prefix != 0);
    //ucs_assert(tag_mask == (uct_tag_t)-1);

    /* Assume enough space in SGE[0] for the COMET header*/
    comet_packet_header_init(packet_header,
       (uint8_t)table_index, slot_id, comet_prefix);

    /* Call super function */
    iface->super_uct_ud_ep_am_zcopy(tl_ep, id, header,
            header_length, iov, iovcnt, flags,
            comp);

    return UCS_OK;
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
    /* Close COMET lib reference */
    ucs_assert(self->comet_ref != NULL);
    comet_close(self->comet_ref);
}

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

    /* Get the attributes of this MD for connection establishment later */
    ucs_status_t status = uct_mm_md_query(md, &self->md_attr);
    if (status != UCS_OK) {
        return status;
    }

    /* Store relevant parameters */
    self->comet_device_index = config->device_index;
    self->sm_proc_cnt = params->node_info.proc_cnt;

    /* Open device driver */
    int ret = comet_init(self->comet_device_index, &self->comet_ref);
    if ((ret != 0) || (self->comet_ref == NULL)) {
        ucs_debug("Failed to initialize COMET");
        return UCS_ERR_NO_MEMORY;
    }

    /* Query the COMET device capabilities */
    unsigned num_comet_devices;
    const struct comet_capabilities *comet_caps;
    ret = comet_query_capabilities(&comet_caps, &num_comet_devices);
    if (ret != 0) {
        ucs_error("Failed to query COMET capabilities");
        return UCS_ERR_NO_RESOURCE;
    }
    memcpy(&self->comet_device_capabilities, comet_caps, sizeof(self->comet_device_capabilities));

    /* Generate per-table information based on the COMET device capabilities */
    self->tables = comet_alloc(self->comet_ref,
    		num_comet_devices * sizeof(uct_ud_comet_table_t));
    unsigned idx;
    for (idx = 0; idx < num_comet_devices; idx++) {
        self->tables[idx].collective_type  = UCT_UD_COMET_COLL_TYPE_REDUCE; // TODO: detect
        self->tables[idx].incoming_data_va = NULL;
        self->tables[idx].incoming_prefix  = 0;
    }

    /* UD_COMET initialized */
    g_ud_comet_initialized = 1;

    return UCS_OK;
}

UCS_CLASS_DEFINE(uct_ud_comet_iface_t, uct_ud_mlx5_iface_t);

static ucs_status_t
uct_ud_comet_query_resources(uct_md_h md,
                            uct_tl_resource_desc_t **resources_p,
                            unsigned *num_resources_p)
{
    ucs_status_t status = UCS_OK;
    int ret;
    const struct comet_capabilities *comet_devices;
    uint32_t num_comet_devices;
    uct_tl_resource_desc_t *tl_devices = NULL;
    uct_tl_comet_device_resource_t *comet_dev_resource;

    /* TODO take transport overhead into account */
    status = uct_ib_device_query_tl_resources(&ucs_derived_of(md, uct_ib_md_t)->dev,
                        "ud_mlx5", UCT_IB_DEVICE_FLAG_MLX5_PRM,
                        resources_p, num_resources_p);
    if (status != UCS_OK) {
        return status;
    }

    ret = comet_query_capabilities(&comet_devices, &num_comet_devices);
    if (ret != 0) {
        ucs_debug("Error querying COMET capabilities ret=%d", ret);
        return UCS_ERR_NO_RESOURCE;
    }

    /*
	   Cannot initialize a COMET device without at least
	   - 1 x mlnx NIC
	   - 1 x COMET FPGA.
     */
    if (*num_resources_p == 0) {
        ucs_debug("%s - Not enough MLX5 devices: num_comet_devices=%u, num_tl_devices=%u\n",
                __func__, num_comet_devices, *num_resources_p);
        status = UCS_ERR_NO_RESOURCE;
        goto exit_error_free_resources;
    }

    /* No COMET device? ==> CLIENT mode */
    if (num_comet_devices == 0) {
        ucs_debug("%s - COMET device not found, initializing UD_COMET as client\n",
                __func__);
        status = UCS_OK;
    	goto comet_init_successful_return;
    }

    tl_devices = *resources_p;
    if (tl_devices == NULL) {
        status = UCS_ERR_NO_MEMORY;
        goto exit_error_free_resources;
    }

    /* Always use MLNX device index 0 for COMET */
    comet_dev_resource = (uct_tl_comet_device_resource_t *)&tl_devices[0];

    /* Initialize pointer to capabilities (comet_devices is always static) */
    comet_dev_resource->comet_capabilities_p = &comet_devices[0];
    comet_dev_resource->type = UCT_DEVICE_TYPE_ACC;
    strcpy(&comet_dev_resource->tl_name[0], UCT_UD_COMET_TL_NAME);
    strcpy(&comet_dev_resource->dev_name[0], "mlx5_1:1");

    /* Return only 1 interface */
    *num_resources_p = 1;

comet_init_successful_return:
    return status;

exit_error_free_resources:
    if (*resources_p != NULL) {
        ucs_free(*resources_p);
        *resources_p = NULL;
    }

    *num_resources_p = 0;

    return status;
}

int
ud_comet_is_initialized(void)
{
	return g_ud_comet_initialized;
}

static UCS_CLASS_DEFINE_NEW_FUNC(uct_ud_comet_iface_t, uct_iface_t, uct_md_h,
                                 uct_worker_h, const uct_iface_params_t*,
                                 const uct_iface_config_t*);


UCT_TL_COMPONENT_DEFINE(uct_ud_comet_tl,
                        uct_ud_comet_query_resources,
                        uct_ud_comet_iface_t,
                        UCT_UD_COMET_TL_NAME,
                        "UD_COMET_",
                        uct_ud_comet_iface_config_table,
                        uct_ud_comet_iface_config_t);
UCT_MD_REGISTER_TL(&uct_ib_mdc, &uct_ud_comet_tl);
