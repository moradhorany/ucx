/**
* Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/
#include "ud_comet.h"
#include "../accel/ud_mlx5.h"

#include <comet_lib.h>

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

/* Full re-use of the mlx5 ops functions */
extern uct_ud_iface_ops_t uct_ud_mlx5_iface_ops;

extern UCS_F_NOINLINE void
uct_ud_mlx5_iface_post_recv(uct_ud_comet_iface_t *iface);

/* COMET interface - Uninitialized, will be initialized by
   calling ud_comet_ops_mlx5_reuse_initialize()
*/
static uct_ud_iface_ops_t uct_ud_comet_iface_ops;

static UCS_CLASS_DEFINE_DELETE_FUNC(uct_ud_comet_iface_t, uct_iface_t);

extern UCS_CLASS_INIT_FUNC(uct_ud_mlx5_iface_t,
                           uct_md_h md, uct_worker_h worker,
                           const uct_iface_params_t *params,
                           const uct_iface_config_t *tl_config);

static unsigned uct_ud_comet_iface_progress(uct_iface_h tl_iface)
{
    uct_ud_comet_iface_t *iface = ucs_derived_of(tl_iface, uct_ud_comet_iface_t);
    ucs_status_t status;
    unsigned idx, count;

    for (idx = self->last_done_idx + 1;
         idx < self->last_done_idx;
         idx = (idx + 1) % self->num_tables) {
        if (self->prefixes[idx]) {
            void *base_address = ???;
            uct_am_handler();

            self->next_prefix = idx;
            uct_iface_trace_am(&iface->super.super, UCT_AM_TRACE_TYPE_RECV,
                    am_id, base_address, length, "RX: AM_ZCOPY");
            return 1;
        }
    }

    return uct_ud_mlx5_iface_progress();
}

ucs_status_t uct_ud_comet_iface_tag_recv_zcopy(uct_iface_h iface, uct_tag_t tag,
                                               uct_tag_t tag_mask,
                                               const uct_iov_t *iov,
                                               size_t iovcnt,
                                               uct_tag_context_t *ctx)
{
    uct_ud_comet_table_t *table = 0;
    uint64_t comet_prefix = (uint64_t)tag;
    void *base_address = iov[0].buffer;

    /* Sanity checks */
    ucs_assert(iovcnt == 2);
    ucs_assert(comet_prefix != 0);
    ucs_assert(tag_mask == (uct_tag_t)-1);
    ucs_assert(ctx->completed_cb != NULL);

    /* Give COMET the */
    comet_recv(comet_iface->comet_handle, uint32_t table_index,
           uint64_t host_prefix_destination_address,
           uint64_t host_data_destination_address);
}

static void ud_comet_ops_mlx5_reuse_initialize(uct_ud_iface_ops_t *ops)
{
    ops->super.super.iface_close = UCS_CLASS_DELETE_FUNC_NAME(uct_ud_comet_iface_t);
    ops->super.super.iface_progress = uct_ud_comet_iface_progress;
    ops->super.super.iface_tag_recv_zcopy = uct_ud_comet_iface_tag_recv_zcopy;
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
    ud_comet_ops_mlx5_reuse_initialize(&uct_ud_comet_iface_ops);

    /* Get the attributes of this MD for connection establishment later */
    status = uct_mm_md_query(md, &self->md_attr);
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

    return UCS_OK;
}

static UCS_CLASS_CLEANUP_FUNC(uct_ud_comet_iface_t)
{
    /* Close COMET lib reference */
    ucs_assert(self->comet_ref != NULL);
    comet_close(self->comet_ref);
}

UCS_CLASS_DEFINE(uct_ud_comet_iface_t, uct_ud_mlx5_iface_t);

static UCS_CLASS_DEFINE_NEW_FUNC(uct_ud_comet_iface_t, uct_iface_t, uct_md_h,
                                 uct_worker_h, const uct_iface_params_t*,
                                 const uct_iface_config_t*);

static ucs_status_t
uct_ud_comet_query_resources(uct_md_h md,
                            uct_tl_resource_desc_t **resources_p,
                            unsigned *num_resources_p)
{
    ucs_status_t status;
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
    if ((num_comet_devices == 0) || (*num_resources_p == 0)) {
        ucs_debug("%s - Not enough devices: num_comet_devices=%u, num_tl_devices=%u\n",
                __func__, num_comet_devices, *num_resources_p);
        status = UCS_ERR_NO_RESOURCE;
        goto exit_error_free_resources;
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

    return UCS_OK;

exit_error_free_resources:
    if (*resources_p != NULL) {
        ucs_free(*resources_p);
        *resources_p = NULL;
    }

    *num_resources_p = 0;

    return status;
}

UCT_TL_COMPONENT_DEFINE(uct_ud_comet_tl,
                        uct_ud_comet_query_resources,
                        uct_ud_comet_iface_t,
                        UCT_UD_COMET_TL_NAME,
                        "UD_COMET_",
                        uct_ud_comet_iface_config_table,
                        uct_ud_comet_iface_config_t);
UCT_MD_REGISTER_TL(&uct_ib_mdc, &uct_ud_comet_tl);
