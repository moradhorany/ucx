/**
* Copyright (C) UT-Battelle, LLC. 2015. ALL RIGHTS RESERVED.
* Copyright (C) Mellanox Technologies Ltd. 2001-2019.  ALL RIGHTS RESERVED.
* Copyright (C) ARM Ltd. 2016.  ALL RIGHTS RESERVED.
* See file LICENSE for terms.
*/

#include "mm_coll_ep.h"

#include <ucs/arch/atomic.h>

enum ucg_mm_coll_send_mode {
    UCG_MM_COLL_SEND_PAYLOAD = 0,
    UCG_MM_COLL_SEND_PACK_CB,
    UCG_MM_COLL_SEND_LOCKING_PACK_CB
};

#define UCG_MM_COLL_HEADER_SIZE (sizeof(uint64_t))

#define UCT_MM_COLL_IFACE_GET_FIFO_ELEM(_ep, _fifo, _index) \
        ((uct_mm_coll_fifo_element_t*)(((char*)(_fifo) + \
                                       ((_index) * (_ep)->fifo_elem_size))))

#define UCT_MM_COLL_IFACE_RESET_FIFO_ELEM(_elem, _is_loopback) { \
        ucs_assert((_elem)->pending == (_is_loopback));\
        (_elem)->super.length = 0; \
        (_elem)->pending = 0; \
}

#define UCT_MM_COLL_IFACE_RESET_FIFO_ELEM_DONE(_ep, _elem, _is_loopback) { \
        UCT_MM_COLL_IFACE_RESET_FIFO_ELEM(_elem, is_loopback); \
        uct_mm_coll_progress_fifo_tail(_ep); \
}

#define UCT_MM_COLL_GET_BASE_ADDRESS(_is_short, _batch_flag, _len, \
                                     _elem, _ep, _iface, _is_local, _is_recv) \
({ \
    void *ret_address; \
    if (_is_short) { \
        ret_address = (void*)((_elem) + 1); \
        if ((_batch_flag) && ((_ep)->my_offset)) {\
            size_t short_extension = UCG_MM_COLL_HEADER_SIZE + \
                (_ep)->my_offset * (_len); \
            ret_address = (uint8_t*)ret_address + short_extension; \
            ucs_assert((((_ep)->my_offset + 1) * (_len)) <= \
                       ((_ep)->fifo_elem_size - sizeof(uct_mm_coll_fifo_element_t))); \
        } \
    } else { \
        if (_is_local) { \
            ret_address = (uint8_t*)(_elem)->super.desc_chunk_base_addr + \
                                    (_elem)->super.desc_offset; \
            if (_is_recv) _batch_flag |= UCT_CB_PARAM_FLAG_DESC; \
        } else { \
            ret_address = (uint8_t*)uct_mm_ep_attach_remote_seg((_ep)->tx, \
                                                                &(_iface)->super, \
                                                                &(_elem)->super) + \
                          (_elem)->super.desc_offset; \
            VALGRIND_MAKE_MEM_DEFINED(ret_address, (_elem)->super.length); \
        } \
        if ((_batch_flag) && ((_ep)->my_offset)) { \
            size_t bcopy_extension = (_ep)->my_offset * \
                ucs_align_up(_len, UCS_SYS_CACHE_LINE_SIZE); \
            ret_address = (uint8_t*)ret_address + bcopy_extension; \
            ucs_assert(bcopy_extension + (_len) <= (_iface)->super.config.seg_size); \
        } \
    } \
    ret_address; \
})

static inline void uct_mm_coll_ep_update_cached_tail(uct_mm_ep_t *ep)
{
    ucs_memory_cpu_load_fence();
    ep->cached_tail = ep->fifo_ctl->tail;
}

/* A common mm active message sending function.
 * The first parameter indicates the origin of the call.
 * is_short_add = 1 - perform AM short sending, assuming "+" for incast
 * is_short_add = 0 - perform AM bcopy sending
 */
static UCS_F_ALWAYS_INLINE ssize_t
uct_mm_coll_ep_am_common_send(enum ucg_mm_coll_send_mode mode,
                              unsigned short_flag, unsigned batch_flag,
                              uct_mm_coll_ep_t *coll_ep,
                              uct_mm_coll_iface_t *iface, uint8_t am_id,
                              size_t length, uint64_t header, const void *payload,
                              uct_locked_pack_callback_t locked_pack_cb, void *arg,
                              uct_pack_callback_t pack_cb, unsigned flags)
{
    /* Sanity checks */
    UCT_CHECK_AM_ID(am_id);

    /* Grab the next cell I haven't yet written to */
    unsigned head   = ++coll_ep->tx_index;
    uct_mm_ep_t *ep = coll_ep->tx;

    /* check if there is room in the remote process's receive FIFO to write */
    if (!UCT_MM_EP_IS_ABLE_TO_SEND(head, ep->cached_tail, coll_ep->fifo_size)) {
        if (!ucs_arbiter_group_is_empty(&ep->arb_group)) {
            /* pending isn't empty. don't send now to prevent out-of-order sending */
            UCS_STATS_UPDATE_COUNTER(ep->super.stats, UCT_EP_STAT_NO_RES, 1);
            coll_ep->tx_index--;
            return UCS_ERR_NO_RESOURCE;
        } else {
            /* pending is empty */
            /* update the local copy of the tail to its actual value on the remote peer */
            uct_mm_coll_ep_update_cached_tail(ep);
            if (!UCT_MM_EP_IS_ABLE_TO_SEND(head, ep->cached_tail, coll_ep->fifo_size)) {
                UCS_STATS_UPDATE_COUNTER(ep->super.stats, UCT_EP_STAT_NO_RES, 1);
                coll_ep->tx_index--;
                return UCS_ERR_NO_RESOURCE;
            }
        }
    }

    /* Obtain the next element this process should access */
    uint64_t elem_index = head & coll_ep->fifo_mask;
    uct_mm_coll_fifo_element_t *elem =
            UCT_MM_COLL_IFACE_GET_FIFO_ELEM(coll_ep, ep->fifo, elem_index);

    /* Check if this is the first message in this collective operation */
    uint32_t pending = ucs_atomic_cswap32(&elem->pending, 0, coll_ep->tx_cnt);

    /* If I'm broadcasting - no need to enable "batched mode" */
    int is_loopback = coll_ep->is_loopback;
    if (is_loopback) {
        batch_flag = 0;
    } else {
        /* Specifically for BCOPY, we need the first writer to write the length so
         * the others could calculate their base address offset in the batch... */
        if ((batch_flag && !short_flag) && (pending)) {
            do {
                length = elem->super.length;
            } while (length == 0); /* non-zero since it's not a short message */
        }
    }

    /* Calculate the destination address (allocate for BCOPY / BLOCK) */
    void *base_address = UCT_MM_COLL_GET_BASE_ADDRESS(short_flag, batch_flag,
            length, elem, coll_ep, iface, is_loopback, 0);

    /* Check for BCOPY or SLOCK or BLOCK to require mutual exclusion */
    ucs_spinlock_pure_t *lock;
    int is_locking_packed_cb = (mode == UCG_MM_COLL_SEND_LOCKING_PACK_CB);
    int needs_lock = (!batch_flag &&
                      !is_loopback &&
                      (!is_locking_packed_cb || (pending == 0)));
    if (ucs_unlikely(needs_lock)) {
        ucs_spin_pure_lock(&elem->lock);
        lock = NULL;
        /* NULL sets a special mode in locked_pack_cb(), doing copy instead of reduce */
    } else {
        lock = (is_locking_packed_cb && pending) ? &elem->lock : NULL;
    }

    /* Reduce into the buffer (or just write, in some cases, e.g. lock is NULL) */
    switch (mode) {
    case UCG_MM_COLL_SEND_PAYLOAD:
        if (batch_flag && coll_ep->my_offset) {
            memcpy(base_address, payload, length);
        } else {
            *(uint64_t*)base_address = header;
            memcpy((uint64_t*)base_address + 1, payload, length);
        }
        length += UCG_MM_COLL_HEADER_SIZE;
        break;

    case UCG_MM_COLL_SEND_PACK_CB:
        length = pack_cb(base_address, arg);
        break;

    case UCG_MM_COLL_SEND_LOCKING_PACK_CB:
        length = locked_pack_cb(base_address, lock, arg);
        break;
    }

    /* Write the per-collective fields (only once per element) */
    if (ucs_unlikely(pending == 0)) {
        elem->super.length = length;
        elem->super.am_id  = am_id;
        elem->super.flags  = short_flag | batch_flag |
                (elem->super.flags & UCT_MM_FIFO_ELEM_FLAG_OWNER);
    }

    /* Mark myself as finished regarding this element */
    if (ucs_unlikely(needs_lock)) {
        pending = elem->pending--;
        ucs_spin_pure_unlock(&elem->lock);
    } else {
        pending = ucs_atomic_fadd32(&elem->pending, (uint32_t)-1);
    }

    //printf("#%i SEND (%lu): write_idx=%u (mode=%i, pending=%u, header=%lu flags=%i)\n",
    //       getpid(), length, head, mode, pending, *(uint64_t*)base_address, elem->super.flags);

    /* Check if this sender is the last expected sender for this element */
    if ((is_loopback) || (pending == 2 /* target + myself */)) {
        /* memory barrier - make sure that the memory is flushed before setting the
         * 'writing is complete' flag which the reader checks */
        ucs_memory_cpu_store_fence();

        /* change the owner bit to indicate that the writing is complete.
         * the owner bit flips after every FIFO wraparound */
        elem->super.flags ^= UCT_MM_FIFO_ELEM_FLAG_OWNER;

        /* Update the remote head element */
        ep->fifo_ctl->head = coll_ep->tx_index;
        ucs_writeback_cache(ucs_unaligned_ptr(&ep->fifo_ctl->head),
                            ucs_unaligned_ptr(&ep->fifo_ctl->head + 1));
    }

    if (short_flag) {
        uct_iface_trace_am(&iface->super.super, UCT_AM_TRACE_TYPE_SEND, am_id,
                           base_address, length, "RX: AM_SHORT");
        UCT_TL_EP_STAT_OP(&ep->super, AM, SHORT, length);
    } else {
        uct_iface_trace_am(&iface->super.super, UCT_AM_TRACE_TYPE_SEND, am_id,
                           base_address, length, "RX: AM_BCOPY");
        UCT_TL_EP_STAT_OP(&ep->super, AM, BCOPY, length);
    }

    return length;
}

ucs_status_t uct_mm_coll_ep_am_short(uct_ep_h tl_ep, uint8_t id, uint64_t header,
                                     const void *payload, unsigned length)
{
    uct_mm_coll_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_mm_coll_iface_t);
    uct_mm_coll_ep_t *ep = (uct_mm_coll_ep_t*)tl_ep;

    return uct_mm_coll_ep_am_common_send(UCG_MM_COLL_SEND_PAYLOAD,
                                         UCT_MM_FIFO_ELEM_FLAG_INLINE,
                                         UCT_MM_FIFO_ELEM_FLAG_BATCH,
                                         ep, iface, id, length,
                                         header, payload, NULL, NULL, NULL, 0);
}

ssize_t uct_mm_coll_ep_am_bcopy(uct_ep_h tl_ep, uint8_t id,
                                uct_pack_callback_t pack_cb,
                                void *arg, unsigned flags)
{
    uct_mm_coll_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_mm_coll_iface_t);
    uct_mm_coll_ep_t *ep = (uct_mm_coll_ep_t*)tl_ep;

    return uct_mm_coll_ep_am_common_send(UCG_MM_COLL_SEND_PACK_CB,
                                         0, UCT_MM_FIFO_ELEM_FLAG_BATCH,
                                         ep, iface, id, 0, 0,
                                         NULL, NULL, arg, pack_cb, flags);
}
ssize_t uct_mm_coll_ep_am_slock(uct_ep_h tl_ep, uint8_t id,
                                uct_locked_pack_callback_t pack_cb,
                                void *arg, unsigned flags)
{
    uct_mm_coll_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_mm_coll_iface_t);
    uct_mm_coll_ep_t *ep = (uct_mm_coll_ep_t*)tl_ep;

    return uct_mm_coll_ep_am_common_send(UCG_MM_COLL_SEND_LOCKING_PACK_CB,
                                         UCT_MM_FIFO_ELEM_FLAG_INLINE, 0,
                                         ep, iface, id, 0, 0,
                                         NULL, pack_cb, arg, NULL, flags);
}

ssize_t uct_mm_coll_ep_am_block(uct_ep_h tl_ep, uint8_t id,
                               uct_locked_pack_callback_t pack_cb,
                               void *arg, unsigned flags)
{
    uct_mm_coll_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_mm_coll_iface_t);
    uct_mm_coll_ep_t *ep = (uct_mm_coll_ep_t*)tl_ep;

    return uct_mm_coll_ep_am_common_send(UCG_MM_COLL_SEND_LOCKING_PACK_CB,
                                         0, 0, ep, iface, id, 0, 0,
                                         NULL, pack_cb, arg, NULL, flags);
}

static void uct_mm_coll_ep_release_desc(uct_recv_desc_t *self, void *desc);

static UCS_CLASS_INIT_FUNC(uct_mm_coll_ep_t, const uct_ep_params_t *params)
{
    ucs_status_t status;
    uct_mm_coll_iface_t *iface = ucs_derived_of(params->iface,
                                                uct_mm_coll_iface_t);
    ucs_assert(params->field_mask & UCT_EP_PARAM_FIELD_IFACE);

    UCS_CLASS_CALL_SUPER_INIT(uct_base_ep_t, &iface->super.super);

    /* The first connection (ever) is a loopback connection */
    const uct_mm_coll_iface_addr_t *addr = (const void *)params->iface_addr;
    int is_loopback = (addr->coll_id == iface->my_coll_id);

    /* Create a two-way channel */
    uct_ep_params_t per_ep_params = *params;
    per_ep_params.iface_addr = is_loopback ? (const uct_iface_addr_t*)&addr->tx:
                                             (const uct_iface_addr_t*)&addr->rx;
    status = UCS_CLASS_NEW(uct_mm_ep_t, &self->tx, &per_ep_params);
    if (status != UCS_OK) {
        return status;
    }
    per_ep_params.iface_addr = is_loopback ? (const uct_iface_addr_t*)&addr->rx:
                                             (const uct_iface_addr_t*)&addr->tx;
    status = UCS_CLASS_NEW(uct_mm_ep_t, &self->rx, &per_ep_params);
    if (status != UCS_OK) {
        UCS_CLASS_DELETE(uct_mm_ep_t, &self->tx);
        ucs_class_free(self->tx);
        return status;
    }

    /* Put self in the next vacant slot in the interface's list of endpoints */
    uct_mm_coll_peer_ep_t *iface_ep_slot;
    if (is_loopback) {
        iface_ep_slot = &iface->eps[0];
        ucs_assert(!iface_ep_slot->ep);
        iface_ep_slot->peer_id = UCT_MM_COLL_MY_PEER_ID;
    } else {
        status = uct_mm_coll_iface_get_ep(iface, UCT_MM_COLL_NO_PEER_ID, &iface_ep_slot);
        ucs_assert(status == UCS_ERR_NO_ELEM);
        iface_ep_slot->peer_id = addr->coll_id;
    }
    iface_ep_slot->ep = self;

    self->release_desc.super.cb = uct_mm_coll_ep_release_desc;
    self->release_desc.ep       = self;
    self->my_coll_id            = iface->my_coll_id;
    self->my_offset             = iface->my_coll_id -
                                  (uint32_t)(addr->coll_id < iface->my_coll_id);
    self->fifo_shift            = iface->super.fifo_shift;
    self->is_loopback           = is_loopback;
    self->tx_cnt                = iface->sm_proc_cnt;
    self->tx_index              = (typeof(self->tx_index)) -1;
    self->rx_index              = 0;
    self->fifo_mask             = iface->super.fifo_mask;
    self->fifo_size             = iface->super.config.fifo_size;
    self->fifo_elem_size        = iface->super.config.fifo_elem_size;

    ucs_debug("mm_coll: ep connected: %p, id: %u", self, addr->coll_id);

    return UCS_OK;
}



#define UCT_MM_COLL_GET_MD(mm_ep) \
        (((uct_base_iface_t*)((mm_ep)->super.super.iface))->md)

static UCS_CLASS_CLEANUP_FUNC(uct_mm_coll_ep_t)
{
    if (UCT_MM_COLL_GET_MD(self->rx)) {
        UCS_CLASS_DELETE(uct_mm_ep_t, self->rx);
    }
    if (UCT_MM_COLL_GET_MD(self->tx)) {
        UCS_CLASS_DELETE(uct_mm_ep_t, self->tx);
    }
}

UCS_CLASS_DEFINE(uct_mm_coll_ep_t, uct_base_ep_t)
UCS_CLASS_DEFINE_NEW_FUNC(uct_mm_coll_ep_t, uct_ep_t, const uct_ep_params_t *);
UCS_CLASS_DEFINE_DELETE_FUNC(uct_mm_coll_ep_t, uct_ep_t);

static UCS_F_ALWAYS_INLINE
void uct_mm_coll_progress_fifo_tail(uct_mm_coll_ep_t *coll_ep)
{
    uint64_t prev_tail = coll_ep->rx_index;
    ucs_atomic_cswap64(&coll_ep->rx->fifo_ctl->tail, prev_tail, prev_tail + 1);
}

static void uct_mm_coll_ep_release_desc(uct_recv_desc_t *self, void *desc)
{
    uct_mm_coll_recv_desc_t *rdesc = (uct_mm_coll_recv_desc_t*)self;
    uct_mm_coll_ep_t *ep           = rdesc->ep;
    uint64_t elem_index            = ep->rx->fifo_ctl->tail;
    uct_mm_iface_t *mm_iface       = (uct_mm_iface_t*)ep->rx->super.super.iface;
    size_t rx_headroom             = mm_iface->rx_headroom;
    uct_mm_recv_desc_t *mm_desc    = (uct_mm_recv_desc_t*)desc;

    /* Find the element which this descriptor belongs to - should be near... */
    int is_match;
    uct_mm_coll_fifo_element_t *elem;
    do {
        elem = UCT_MM_COLL_IFACE_GET_FIFO_ELEM(ep, ep->rx->fifo,
                (elem_index++ & ep->fifo_mask));
        is_match = ((elem->super.desc_mmid == mm_desc->key) &&
                    (elem->super.desc_chunk_base_addr == mm_desc->base_address) &&
                    (elem->super.desc_offset == (rx_headroom +
                            (ptrdiff_t) ((void*) (mm_desc + 1) - mm_desc->base_address))));
        ucs_assert(elem_index <= ep->rx_index);
    } while (!is_match);

    /* Mark this element as handled (by me, at least) */
    UCT_MM_COLL_IFACE_RESET_FIFO_ELEM(elem, 0); // TODO: Fix?

    /* Check if this element has been released by all peers and can be re-used */
    if (--elem_index == ep->rx->fifo_ctl->tail) {
        while ((elem->pending == 0) && (elem_index < ep->rx_index)) {
            elem->super.length = 0;
            ep->rx->fifo_ctl->tail = elem_index;
            elem = UCT_MM_COLL_IFACE_GET_FIFO_ELEM(ep, ep->rx->fifo,
                    (elem_index++ & ep->fifo_mask));
        }
    }

    /* Return this segment to the pool */
    ucs_mpool_put(mm_desc - 1);
}

/**
 * This function serves two purposes:
 * ep == NULL : Check for incoming messages on
 */
static UCS_F_ALWAYS_INLINE
unsigned uct_mm_coll_iface_poll_fifo(uct_mm_coll_iface_t *iface,
                                     uct_mm_coll_ep_t *coll_ep,
                                     int is_loopback /* compile-time */)
{
    uct_mm_ep_t *ep         = coll_ep->rx; /* Look at the remote peer's bcast */
    unsigned read_index     = coll_ep->rx_index;
    unsigned read_index_loc = (read_index & coll_ep->fifo_mask);

    /* the fifo_element which the read_index points to */
    uct_mm_coll_fifo_element_t *elem = UCT_MM_COLL_IFACE_GET_FIFO_ELEM(coll_ep,
            ep->fifo, read_index_loc);

    /* check the read_index to see if there is a new item to read (checking the owner bit) */
    if (((read_index >> coll_ep->fifo_shift) & 1) !=
        ((elem->super.flags) & UCT_MM_FIFO_ELEM_FLAG_OWNER)) {
        return 0;
    }

    ucs_memory_cpu_load_fence();
    ucs_assert(elem->pending != 0);

    /* Detect incoming message parameters */
    uint8_t am_id       = elem->super.am_id;
    uint8_t flags       = elem->super.flags;
    size_t length       = elem->super.length;
    int is_short        = flags & UCT_MM_FIFO_ELEM_FLAG_INLINE;
    unsigned batch_flag = flags & UCT_MM_FIFO_ELEM_FLAG_BATCH;
    void *base_address  = UCT_MM_COLL_GET_BASE_ADDRESS(is_short, batch_flag,
            length, elem, coll_ep, iface, is_loopback, 1);

    /* Process the incoming message */
    ucs_status_t status = uct_iface_invoke_am(&iface->super.super,
            am_id, base_address, length, batch_flag);

    unsigned pending = (unsigned)-1;
    if (ucs_unlikely(status == UCS_INPROGRESS)) {
        void *desc = (void *)((uintptr_t)base_address - iface->super.rx_headroom);
        ucs_assert(!is_short);

        /* If I'm the owner of this memory - I can replace the element's segment */
        if (is_loopback) {
            /* assign a new receive descriptor to this FIFO element.*/
            uct_mm_assign_desc_to_fifo_elem(&iface->super, &elem->super, 1);

            /* later release of this desc - the easy way */
            uct_recv_desc(desc) = (uct_recv_desc_t*)&iface->super.release_desc;

            /* Mark element as done (and re-usable) */
            UCT_MM_COLL_IFACE_RESET_FIFO_ELEM_DONE(coll_ep, elem, is_loopback);
        } else {
            /* later release of this desc - the hard way... */
            uct_recv_desc(desc) = (uct_recv_desc_t*)&coll_ep->release_desc;
        }
    } else {
        if ((is_loopback) ||
            ((pending = ucs_atomic_fadd32(&elem->pending, (uint32_t)-1)) == 1)) {
            /* Mark element as done (and re-usable) */
            UCT_MM_COLL_IFACE_RESET_FIFO_ELEM_DONE(coll_ep, elem, is_loopback);
        }
    }

    coll_ep->rx_index++;

    //printf("#%i RECV (%lu): header=%lu pending=%u read_idx=%u tail_idx=%lu "
    //        "status=%i (is_loopback? %i short? %i batch? %i)\n", getpid(),
    //        length, *(uint64_t*)base_address, pending, coll_ep->rx_index,
    //        coll_ep->rx->fifo_ctl->tail, status, is_loopback, is_short, batch_flag);

    if (is_short) {
        uct_iface_trace_am(&iface->super.super, UCT_AM_TRACE_TYPE_RECV,
                am_id, base_address, length, "RX: AM_SHORT");
    } else {
        uct_iface_trace_am(&iface->super.super, UCT_AM_TRACE_TYPE_RECV,
                am_id, base_address, length, "RX: AM_BCOPY");
    }

    return 1;
}

unsigned uct_mm_coll_iface_progress(void *arg)
{
    unsigned count = 0;
    uct_mm_coll_iface_t *iface = arg;
    uct_mm_coll_peer_ep_t *iter = iface->eps;
    uct_mm_coll_ep_t *next_ep = (iter++)->ep;

    while (uct_mm_coll_iface_poll_fifo(iface, next_ep, 1)) {
        count++;
    }

    while ((next_ep = iter->ep) != NULL) {
        ucs_assert(iter->peer_id != iface->my_coll_id); // TODO: my_coll_id is local...
        while (uct_mm_coll_iface_poll_fifo(iface, next_ep, 0)) {
            count++;
        }
        iter++;
    }

    /* progress the pending sends (if there are any) */
    ucs_arbiter_dispatch(&iface->super.arbiter, 1, uct_mm_ep_process_pending, NULL);

    return count;
}
