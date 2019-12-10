/**
 * Copyright (C) Mellanox Technologies Ltd. 2001-2014.  ALL RIGHTS RESERVED.
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
    uct_ud_mlx5_iface_t                 super;
#if 0
    struct {
        uct_ib_mlx5_txwq_t              wq;
    } tx;
    struct {
        uct_ib_mlx5_rxwq_t              wq;
    } rx;
    uct_ib_mlx5_cq_t                    cq[UCT_IB_DIR_NUM];
    uct_ud_mlx5_iface_common_t          ud_mlx5_common;
#endif
    /* Huawei COMET device reference */
    void							    *comet_ref;

    /* Huawei COMET device index */
    uint32_t							comet_device_index;
} uct_ud_comet_iface_t;


/**
 * COMET resource descriptor of a transport device
 */
#define UCT_COMET_DEVICE_NAME_MAX (UCT_DEVICE_NAME_MAX - sizeof(const struct comet_capabilities *))

#define UCT_UD_COMET_TL_NAME "UD_COMET"

typedef struct uct_tl_comet_device_resource {
    char                     tl_name[UCT_TL_NAME_MAX];   /**< Transport name */
    char                     dev_name[UCT_COMET_DEVICE_NAME_MAX]; /**< Hardware device name */
    const struct comet_capabilities *comet_capabilities_p; /* Pointer to COMET capabilities */

    uct_device_type_t         type;     /**< Device type. To which UCT group it belongs to */
} uct_tl_comet_device_resource_t;

#endif /* _UD_COMET_H_ */

