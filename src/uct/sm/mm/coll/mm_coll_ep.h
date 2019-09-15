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

struct uct_mm_coll_ep {
    uct_base_ep_t super;

    uct_mm_ep_t  *tx;           /* For sending messages to the root */
    uint64_t      tx_peer_mask; /* The mask of the remote peer */

    uct_mm_ep_t  *rx;           /* For receiving messages, broadcasted by a peer */
    uint64_t      rx_index;     /* RX actual reading location */
};

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
