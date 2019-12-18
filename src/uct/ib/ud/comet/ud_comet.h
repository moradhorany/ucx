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

typedef struct uct_tl_comet_device_resource {
    char                     tl_name[UCT_TL_NAME_MAX];   /**< Transport name */
    char                     dev_name[UCT_COMET_DEVICE_NAME_MAX]; /**< Hardware device name */
    const struct comet_capabilities *comet_capabilities_p; /* Pointer to COMET capabilities */

    uct_device_type_t         type;     /**< Device type. To which UCT group it belongs to */
} uct_tl_comet_device_resource_t;

typedef uct_ud_mlx5_ep_t uct_comet_ep_t;

typedef struct uct_ud_comet_iface {
    uct_ud_mlx5_iface_t super;

    uint32_t            sm_proc_cnt;
    uct_md_attr_t       md_attr;

    void               *comet_ref;          /* Huawei COMET device reference */
    uint32_t            comet_device_index; /* Huawei COMET device index */
} uct_ud_comet_iface_t;

#endif /* _UD_COMET_H_ */

