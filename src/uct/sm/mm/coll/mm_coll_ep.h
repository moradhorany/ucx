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
    uct_base_ep_t           super;

    uct_mm_ep_t            *tx;              /* for sending messages to the root */
    uct_mm_ep_t            *rx;              /* for receiving messages, broadcasted by a peer */

    uint8_t                 coll_id;         /* ID of the remote peer in this group */
    uint8_t                 my_offset;       /* where to write in "batch mode" */
    uint8_t                 fifo_shift;      /* shortcut to iface->fifo_shift */
    uint8_t                 tx_cnt;          /* shortcut to iface->sm_proc_cnt */

    unsigned                tx_index;        /* TX next writing location */
    unsigned                rx_index;        /* RX next reading location */

    unsigned                fifo_mask;       /* shortcut to iface->fifo_mask */
    unsigned                rx_elem_size;    /* fifo_elem_size used for RX */
    unsigned                tx_elem_size;    /* fifo_elem_size used for TX */
    unsigned                ref_count;       /* counts the uses of this slot */

    uint8_t                 is_flags_cached; /* indicates flags_cache usage */
    uint8_t                 flags_cache;     /* last value of element flags */
    uint16_t                reserved;        /* info: used 62 bytes out of 64 */

    /* TODO: put bcopy segment size here, instead of iface->config.seg_size */
} UCS_S_PACKED UCS_V_ALIGNED(UCS_SYS_CACHE_LINE_SIZE);

UCS_CLASS_DECLARE_NEW_FUNC(uct_mm_coll_ep_t, uct_ep_t, const uct_ep_params_t*);
UCS_CLASS_DECLARE_DELETE_FUNC(uct_mm_coll_ep_t, uct_ep_t);


ssize_t uct_mm_lcoll_ep_am_bcopy(uct_ep_h tl_ep, uint8_t id,
                                 uct_pack_callback_t pack_cb,
                                 void *arg, unsigned flags);

ssize_t uct_mm_bcoll_ep_am_bcopy(uct_ep_h tl_ep, uint8_t id,
                                 uct_pack_callback_t pack_cb,
                                 void *arg, unsigned flags);

ssize_t uct_mm_ccoll_ep_am_bcopy(uct_ep_h tl_ep, uint8_t id,
                                 uct_pack_callback_t pack_cb,
                                 void *arg, unsigned flags);

ucs_status_t uct_mm_bcoll_ep_am_short(uct_ep_h tl_ep, uint8_t id, uint64_t header,
                                      const void *payload, unsigned length);

ucs_status_t uct_mm_ccoll_ep_am_short(uct_ep_h tl_ep, uint8_t id, uint64_t header,
                                      const void *payload, unsigned length);

unsigned uct_mm_lcoll_iface_progress(uct_iface_h iface);

unsigned uct_mm_bcoll_iface_progress(uct_iface_h iface);

unsigned uct_mm_ccoll_iface_progress(uct_iface_h iface);

#endif
