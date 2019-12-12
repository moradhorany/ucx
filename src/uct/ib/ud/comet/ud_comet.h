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

typedef struct uct_comet_ep uct_comet_ep_t;

/* UCG integration */
typedef struct uct_comet_recv_desc {
    uct_recv_desc_t   super;
    uct_comet_ep_t *ep;
} uct_comet_recv_desc_t;

struct uct_comet_ep {
    uct_base_ep_t           super;

    uct_comet_ep_t          *tx;              /* For sending messages to the root */
    uct_comet_ep_t          *rx;              /* For receiving messages, broadcasted by a peer */

    uint8_t                 my_coll_id;      /* My ID within this host */
    uint8_t                 my_offset;       /* Where to write in "batch mode" */
    uint8_t                 fifo_shift;      /* shortcut to iface->fifo_shift */
    uint8_t                 is_loopback;     /* Indicates a special endpoint */
    uint32_t                tx_cnt;          /* shortcut to iface->sm_proc_cnt */

    unsigned                tx_index;        /* TX next writing location */
    unsigned                rx_index;        /* RX next reading location */

    unsigned                fifo_mask;       /* shortcut to iface->fifo_mask */
    unsigned                fifo_size;       /* shortcut to iface->config.fifo_size */
    unsigned                fifo_elem_size;  /* shortcut to iface->config.fifo_elem_size */

    /* --- cache-line limit (with ENABLE_STATS) --- */

    uct_comet_recv_desc_t release_desc;    /* Release descriptor */
} UCS_S_PACKED UCS_V_ALIGNED(UCS_SYS_CACHE_LINE_SIZE);

typedef struct uct_comet_peer_ep {
#define UCT_COMET_NO_PEER_ID ((uint64_t)-1)
    uint64_t          peer_id;
    uct_comet_ep_t    *ep;
} uct_ud_comet_peer_ep_t;

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

    uct_ud_comet_peer_ep_t				*eps;

    uint32_t							sm_proc_cnt;
} uct_ud_comet_iface_t;

static inline ucs_status_t
uct_ud_comet_iface_get_ep(uct_ud_comet_iface_t *iface,
        uint64_t peer_id, uct_ud_comet_peer_ep_t **peer_ep)
{
    /*
     * Note: iface->eps is initially allocated to the maximal required size,
     * and also "null-terminated". This function is intended for "slow-path".
     */
	uct_ud_comet_peer_ep_t *iter = iface->eps;
    while (iter->ep && (iter->peer_id != peer_id)) {
        iter++;
    }

    *peer_ep = iter; /* If not found - return the next vacant slot to be used */
    return iter->ep ? UCS_OK : UCS_ERR_NO_ELEM;
}

#endif /* _UD_COMET_H_ */

