/**
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */
#ifndef _UD_COMET_H_
#define _UD_COMET_H_

#include <uct/ib/ud/accel/ud_mlx5_common.h>

#include <uct/ib/ud/base/ud_iface.h>
#include <uct/ib/ud/base/ud_ep.h>

#include "../accel/ud_mlx5.h"

typedef struct {
    uct_ud_iface_config_t               super;
    uct_ib_mlx5_iface_config_t          mlx5_common;
    uct_ud_mlx5_iface_common_config_t   ud_mlx5_common;

    /* COMET-specific configurations */
    unsigned                            device_index;
} uct_ud_comet_iface_config_t;

/**
 * COMET resource descriptor of a transport device
 */
#define UCT_COMET_DEVICE_NAME_MAX (UCT_DEVICE_NAME_MAX - sizeof(const struct comet_capabilities *))

#define UCT_UD_COMET_TL_NAME "ud_comet"

enum uct_ud_comet_coll_type {
    UCT_UD_COMET_COLL_TYPE_ALLREDUCE = COMET_TABLE_OPERATION_REDUCE,
    UCT_UD_COMET_COLL_TYPE_ALLTOALL  = COMET_TABLE_OPERATION_ALLTOALL
};

typedef struct uct_ud_comet_table {
    enum uct_ud_comet_coll_type collective_type;
    uct_tag_t                   incoming_prefix;
    void*                       incoming_data_va;
    void*                       incoming_data_pa;
} uct_ud_comet_table_t;

typedef struct uct_tl_comet_device_resource {
    char                     tl_name[UCT_TL_NAME_MAX];   /**< Transport name */
    char                     dev_name[UCT_COMET_DEVICE_NAME_MAX]; /**< Hardware device name */
    const struct comet_capabilities *comet_capabilities_p; /* Pointer to COMET capabilities */

    uct_device_type_t         type;     /**< Device type. To which UCT group it belongs to */
} uct_tl_comet_device_resource_t;

typedef uct_ud_mlx5_ep_t uct_comet_ep_t;

typedef struct uct_ud_comet_iface {
    uct_ud_mlx5_iface_t   super;

    uint32_t              sm_proc_cnt;         /* provided PPN information */
    uct_md_attr_t         md_attr;             /* memory domain attributes */
    void                 *comet_ref;           /* device reference / handle */
    uint32_t              comet_device_index;  /* device index */
    uint32_t              table_cnt;           /* number of tables */
    uint32_t              last_table_comp_idx; /* index of the last completed table */
    uct_ud_comet_table_t *tables;              /* per-table information */
} uct_ud_comet_iface_t;

#endif /* _UD_COMET_H_ */

