/**
* Copyright (C) UT-Battelle, LLC. 2015. ALL RIGHTS RESERVED.
* Copyright (C) Mellanox Technologies Ltd. 2001-2019.  ALL RIGHTS RESERVED.
* Copyright (C) ARM Ltd. 2016.  ALL RIGHTS RESERVED.
* See file LICENSE for terms.
*/

#ifndef UCT_MM_COLL_EP_H
#define UCT_MM_COLL_EP_H

#include "mm_coll_iface.h"

#include <uct/sm/mm/base/mm_ep.h>

typedef struct uct_mm_coll_recv_desc {
    uct_recv_desc_t   super;
    uct_mm_coll_ep_t *ep;
} uct_mm_coll_recv_desc_t;

struct uct_mm_coll_ep {
    uct_base_ep_t           super;

    uct_mm_ep_t            *tx;              /* For sending messages to the root */
    uct_mm_ep_t            *rx;              /* For receiving messages, broadcasted by a peer */

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
    unsigned                reserved;        /* reserved for future use */

    /* --- cache-line limit (with ENABLE_STATS) --- */

    uct_mm_coll_recv_desc_t release_desc;    /* Release descriptor */
    uint8_t                 padding[ucs_padding(sizeof(uct_mm_coll_recv_desc_t),
                                                UCS_SYS_CACHE_LINE_SIZE)];
} UCS_S_PACKED UCS_V_ALIGNED(UCS_SYS_CACHE_LINE_SIZE);

UCS_CLASS_DECLARE_NEW_FUNC(uct_mm_coll_ep_t, uct_ep_t, const uct_ep_params_t*);
UCS_CLASS_DECLARE_DELETE_FUNC(uct_mm_coll_ep_t, uct_ep_t);


ucs_status_t uct_mm_coll_ep_am_short(uct_ep_h tl_ep, uint8_t id, uint64_t header,
                                     const void *payload, unsigned length);

ssize_t uct_mm_coll_ep_am_bcopy(uct_ep_h tl_ep, uint8_t id,
                                uct_pack_callback_t pack_cb,
                                void *arg, unsigned flags);

ssize_t uct_mm_coll_ep_am_slock(uct_ep_h tl_ep, uint8_t id,
                                uct_locked_pack_callback_t pack_cb,
                                void *arg, unsigned flags);

ssize_t uct_mm_coll_ep_am_block(uct_ep_h tl_ep, uint8_t id,
                                uct_locked_pack_callback_t pack_cb,
                                void *arg, unsigned flags);

#endif
