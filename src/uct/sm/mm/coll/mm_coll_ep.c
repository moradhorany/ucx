/**
* Copyright (C) UT-Battelle, LLC. 2015. ALL RIGHTS RESERVED.
* Copyright (C) Mellanox Technologies Ltd. 2001-2019.  ALL RIGHTS RESERVED.
* Copyright (C) ARM Ltd. 2016.  ALL RIGHTS RESERVED.
* See file LICENSE for terms.
*/

#include "mm_coll_ep.h"

#include <ucs/arch/atomic.h>

static inline void uct_mm_coll_ep_update_cached_tail(uct_mm_ep_t *ep)
{
    ucs_memory_cpu_load_fence();
    ep->cached_tail = ep->fifo_ctl->tail;
}

static size_t uct_mm_coll_ep_am_common_send_action(ucs_spinlock_t *lock,
        void *base_address, size_t length, uint64_t header, const void *payload,
        uct_locked_pack_callback_t locked_pack_cb, void *arg,
        uct_pack_callback_t pack_cb, unsigned flags)
{
    if (payload != NULL) {
        *(uint64_t*)base_address = header;
        memcpy((char*)base_address + sizeof(header), payload, length);
        return length + sizeof(header);
    }

    if (pack_cb != NULL) {
        return pack_cb(base_address, arg);
    }

    return locked_pack_cb(base_address, lock, arg);
}

/* A common mm active message sending function.
 * The first parameter indicates the origin of the call.
 * is_short_add = 1 - perform AM short sending, assuming "+" for incast
 * is_short_add = 0 - perform AM bcopy sending
 */
static UCS_F_ALWAYS_INLINE ssize_t
uct_mm_coll_ep_am_common_send(unsigned is_short, unsigned is_batch,
                              uct_mm_coll_ep_t *coll_ep,
                              uct_mm_coll_iface_t *iface, uint8_t am_id,
                              size_t length, uint64_t header, const void *payload,
                              uct_locked_pack_callback_t locked_pack_cb, void *arg,
                              uct_pack_callback_t pack_cb, unsigned flags)
{
    /* Sanity checks */
    UCT_CHECK_AM_ID(am_id);

    /* Grab the next cell I haven't yet written to */
    uint64_t head = ++coll_ep->tx_index;
    uct_mm_ep_t *ep = coll_ep->tx;

    /* check if there is room in the remote process's receive FIFO to write */
    if (!UCT_MM_EP_IS_ABLE_TO_SEND(head, ep->cached_tail, iface->super.config.fifo_size)) {
        if (!ucs_arbiter_group_is_empty(&ep->arb_group)) {
            /* pending isn't empty. don't send now to prevent out-of-order sending */
            UCS_STATS_UPDATE_COUNTER(ep->super.stats, UCT_EP_STAT_NO_RES, 1);
            return UCS_ERR_NO_RESOURCE;
        } else {
            /* pending is empty */
            /* update the local copy of the tail to its actual value on the remote peer */
            uct_mm_coll_ep_update_cached_tail(ep);
            if (!UCT_MM_EP_IS_ABLE_TO_SEND(head, ep->cached_tail, iface->super.config.fifo_size)) {
                UCS_STATS_UPDATE_COUNTER(ep->super.stats, UCT_EP_STAT_NO_RES, 1);
                return UCS_ERR_NO_RESOURCE;
            }
        }
    }

    /* Obtain the next element this process should access */
    uint64_t elem_index = head & iface->super.fifo_mask;
    uct_mm_coll_fifo_element_t *elem = (uct_mm_coll_fifo_element_t*)
            UCT_MM_IFACE_GET_FIFO_ELEM(&iface->super, ep->fifo, elem_index);
    ucs_assert((elem->pending == 0) || (elem->pending & iface->my_mask));

    /* Check if this is the first message in this collective operation */
    void *base_address;
    uct_mm_coll_peer_mask_t pending;
    if (ucs_unlikely(elem->pending == 0)) {
        int is_bcast   = (coll_ep == iface->eps[0].ep);
        int needs_lock = ((!is_batch || !is_short) && !is_bcast);

        /* Check for BCOPY or SLOCK or BLOCK to require mutual exclusion */
        if (needs_lock) {
            ucs_spin_lock_alone(&elem->lock);
        }

        if (ucs_unlikely(elem->pending == 0)) {
            /* Calculate the destination address (allocate for BCOPY / BLOCK) */
            base_address = is_short ? (void*)(elem + 1) : elem->super.desc_offset +
                    uct_mm_ep_attach_remote_seg(ep, &iface->super, &elem->super);
            if (is_batch) {
                base_address = (char*)base_address + (length * coll_ep->my_offset);
            }

            /* First element - lock, and indicate the memory requires overwriting */
            length = uct_mm_coll_ep_am_common_send_action(NULL, base_address,
                    length, header, payload, locked_pack_cb, arg, pack_cb, flags);

            pending = elem->pending = iface->peer_mask;
            if (needs_lock) {
                ucs_spin_unlock_alone(&elem->lock);
            }
            goto done_reducing;
        }

        /* Same check for unlocking... */
        if (needs_lock) {
            ucs_spin_unlock_alone(&elem->lock);
        }
    }

    /* Calculate destination address for reduction */
    base_address = is_short ? (void*)(elem + 1) :
            (elem->super.desc_chunk_base_addr + elem->super.desc_offset);
    if (is_batch) {
        base_address = (char*)base_address + (iface->my_coll_id *
                ucs_align_up(length, UCS_SYS_CACHE_LINE_SIZE));
    }

    /* Reduce into the buffer */
    length = uct_mm_coll_ep_am_common_send_action(is_batch ? NULL : &elem->lock,
            base_address, length, header, payload, locked_pack_cb, arg, pack_cb, flags);

    /* Mark myself as finished regarding this element */
    ucs_assert(elem->pending & iface->my_mask); /* I'm still pending (before) */
    pending = ucs_atomic_fand64(&elem->pending, ~iface->my_mask) & ~iface->my_mask;

done_reducing:
    /* Check if this sender is the last expected sender for this element */
    if (ucs_unlikely(pending == coll_ep->tx_peer_mask)) {
        /* Write the per-collective fields (only once per element) */
        elem->super.am_id  = am_id;
        elem->super.length = length;
        elem->super.flags  = is_short ?
                elem->super.flags |  UCT_MM_FIFO_ELEM_FLAG_INLINE:
                elem->super.flags & ~UCT_MM_FIFO_ELEM_FLAG_INLINE;

        if (is_batch) {
            elem->super.flags |= UCT_MM_FIFO_ELEM_FLAG_BATCH;
        } else {
            elem->super.flags &= ~UCT_MM_FIFO_ELEM_FLAG_BATCH;
        }

        /* memory barrier - make sure that the memory is flushed before setting the
         * 'writing is complete' flag which the reader checks */
        ucs_memory_cpu_store_fence();

        /* change the owner bit to indicate that the writing is complete.
         * the owner bit flips after every FIFO wraparound */
        if (head & iface->super.config.fifo_size) {
            elem->super.flags |= UCT_MM_FIFO_ELEM_FLAG_OWNER;
        } else {
            elem->super.flags &= ~UCT_MM_FIFO_ELEM_FLAG_OWNER;
        }

        /* Update the remote head element */
        ep->fifo_ctl->head = coll_ep->tx_index;
        // TODO: why mm_ep.c needs ucs_atomic_cswap64() and this doesn't?
        ucs_writeback_cache(ucs_unaligned_ptr(&ep->fifo_ctl->head),
                            ucs_unaligned_ptr(&ep->fifo_ctl->head + 1));
    }

    if (is_short) {
        UCT_CHECK_LENGTH(length, 0, iface->super.config.fifo_elem_size -
                sizeof(uct_mm_coll_fifo_element_t), "am_scoll");
        uct_iface_trace_am(&iface->super.super, UCT_AM_TRACE_TYPE_SEND, am_id,
                base_address, length, "TX: AM_SHORT");
        UCT_TL_EP_STAT_OP(&ep->super, AM, SHORT, length);
    } else {
        uct_iface_trace_am(&iface->super.super, UCT_AM_TRACE_TYPE_SEND, am_id,
                base_address, length, "TX: AM_BCOPY");
        UCT_TL_EP_STAT_OP(&ep->super, AM, BCOPY, length);
    }

    return length;
}

ucs_status_t uct_mm_coll_ep_am_short(uct_ep_h tl_ep, uint8_t id, uint64_t header,
                                     const void *payload, unsigned length)
{
    uct_mm_coll_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_mm_coll_iface_t);
    uct_mm_coll_ep_t *ep = (uct_mm_coll_ep_t*)tl_ep;

    UCT_CHECK_LENGTH(length + sizeof(header), 0, iface->super.config.fifo_elem_size -
                     sizeof(uct_mm_coll_fifo_element_t), "am_short");

    return uct_mm_coll_ep_am_common_send(1, 1, ep, iface, id, length,
                                         header, payload, NULL, NULL, NULL, 0);
}

ssize_t uct_mm_coll_ep_am_bcopy(uct_ep_h tl_ep, uint8_t id,
                                uct_pack_callback_t pack_cb,
                                void *arg, unsigned flags)
{
    uct_mm_coll_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_mm_coll_iface_t);
    uct_mm_coll_ep_t *ep = (uct_mm_coll_ep_t*)tl_ep;

    return uct_mm_coll_ep_am_common_send(0, 1, ep, iface, id, 0, 0,
                                         NULL, NULL, arg, pack_cb, flags);
}
ssize_t uct_mm_coll_ep_am_slock(uct_ep_h tl_ep, uint8_t id,
                                uct_locked_pack_callback_t pack_cb,
                                void *arg, unsigned flags)
{
    uct_mm_coll_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_mm_coll_iface_t);
    uct_mm_coll_ep_t *ep = (uct_mm_coll_ep_t*)tl_ep;

    return uct_mm_coll_ep_am_common_send(1, 0, ep, iface, id, 0, 0,
                                         NULL, pack_cb, arg, NULL, flags);
}

ssize_t uct_mm_coll_ep_am_block(uct_ep_h tl_ep, uint8_t id,
                               uct_locked_pack_callback_t pack_cb,
                               void *arg, unsigned flags)
{
    uct_mm_coll_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_mm_coll_iface_t);
    uct_mm_coll_ep_t *ep = (uct_mm_coll_ep_t*)tl_ep;

    return uct_mm_coll_ep_am_common_send(0, 0, ep, iface, id, 0, 0,
                                         NULL, pack_cb, arg, NULL, flags);
}

static UCS_CLASS_INIT_FUNC(uct_mm_coll_ep_t, const uct_ep_params_t *params)
{
    ucs_status_t status;
    uct_mm_coll_iface_t *iface = ucs_derived_of(params->iface, uct_mm_coll_iface_t);

    UCS_CLASS_CALL_SUPER_INIT(uct_base_ep_t, &iface->super.super);

    /* Create a two-way channel */
    const uct_mm_coll_iface_addr_t *addr = (const void *)params->iface_addr;
    uct_ep_params_t per_ep_params        = *params;
    per_ep_params.iface_addr             = (const uct_iface_addr_t*)&addr->rx;
    status = UCS_CLASS_NEW(uct_mm_ep_t, &self->tx, &per_ep_params);
    if (status != UCS_OK) {
        return status;
    }
    per_ep_params.iface_addr = (const uct_iface_addr_t*)&addr->tx;
    status = UCS_CLASS_NEW(uct_mm_ep_t, &self->rx, &per_ep_params);
    if (status != UCS_OK) {
        UCS_CLASS_DELETE(uct_mm_ep_t, &self->tx);
        ucs_class_free(self->tx);
        return status;
    }

    /* Put self in the next vacant slot in the interface's list of endpoints */
    uct_mm_coll_peer_ep_t *iface_ep_slot;
    status = uct_mm_coll_iface_get_ep(iface, UCT_MM_COLL_NO_PEER_ID, &iface_ep_slot);
    ucs_assert(status == UCS_ERR_NO_ELEM);
    iface_ep_slot->ep = self;

    self->my_offset    = iface->my_coll_id - (uint32_t)(addr->coll_id < iface->my_coll_id);
    self->tx_peer_mask = UCS_BIT(addr->coll_id);
    self->tx_index     = (uint64_t)-1;
    self->rx_index     = 0;

    ucs_debug("mm_coll: ep connected: %p, id: %u", self, addr->coll_id);

    return UCS_OK;
}

static UCS_CLASS_CLEANUP_FUNC(uct_mm_coll_ep_t)
{
    // TODO: UCS_CLASS_CLEANUP(uct_mm_ep_t, &self->rx);
    // TODO: UCS_CLASS_CLEANUP(uct_mm_ep_t, &self->tx);
    ucs_class_free(self->rx);
    ucs_class_free(self->tx);
}

UCS_CLASS_DEFINE(uct_mm_coll_ep_t, uct_base_ep_t)
UCS_CLASS_DEFINE_NEW_FUNC(uct_mm_coll_ep_t, uct_ep_t, const uct_ep_params_t *);
UCS_CLASS_DEFINE_DELETE_FUNC(uct_mm_coll_ep_t, uct_ep_t);


static inline void uct_mm_coll_progress_fifo_tail(uct_mm_coll_iface_t *iface,
                                                  uct_mm_coll_ep_t *coll_ep)
{
    /* don't progress the tail every time - release in batches. improves performance */
    if (coll_ep->rx_index & iface->super.fifo_release_factor_mask) {
        return;
    }

    coll_ep->rx->fifo_ctl->tail = coll_ep->rx_index;
}

/**
 * This function serves two purposes:
 * ep == NULL : Check for incoming messages on
 */
static inline unsigned uct_mm_coll_iface_poll_fifo(uct_mm_coll_iface_t *iface,
                                                   uct_mm_coll_ep_t *coll_ep,
                                                   int is_incast)
{
    uint64_t read_index_loc, read_index;
    uct_mm_coll_fifo_element_t* read_index_elem;
    ucs_status_t status;

    /* check the memory pool to make sure that there is a new descriptor available */
    if (ucs_unlikely(iface->super.last_recv_desc == NULL)) {
        UCT_TL_IFACE_GET_RX_DESC(&iface->super.super, &iface->super.recv_desc_mp,
                                 iface->super.last_recv_desc, return 0);
    }

    uct_mm_ep_t *ep = coll_ep->rx; /* Look at the remote peer's broadcasts */
    read_index      = coll_ep->rx_index;
    read_index_loc  = (read_index & iface->super.fifo_mask);
    /* the fifo_element which the read_index points to */
    read_index_elem = (uct_mm_coll_fifo_element_t*)
            UCT_MM_IFACE_GET_FIFO_ELEM(&iface->super, ep->fifo, read_index_loc);

    /* check the read_index to see if there is a new item to read (checking the owner bit) */
    if ((read_index_elem->pending != 0) &&
        (((read_index >> iface->super.fifo_shift) & 1) ==
         ((read_index_elem->super.flags) & UCT_MM_FIFO_ELEM_FLAG_OWNER))) {

        /* read from read_index_elem */
        ucs_memory_cpu_load_fence();

        /* Make sure it's not a message I broadcasted... */
        if (read_index_elem->pending & iface->my_mask) {
            status = uct_mm_iface_process_recv_ext(&iface->super,
                    &read_index_elem->super, read_index_elem + 1, 0);
            if (status != UCS_OK) {
                /* the last_recv_desc is in use. get a new descriptor for it */
                UCT_TL_IFACE_GET_RX_DESC(&iface->super.super,
                        &iface->super.recv_desc_mp,
                        iface->super.last_recv_desc,
                        ucs_debug("recv mpool is empty"));
                return 0; /* Don't mark this element as processed */
            }
        }

        /* Raise the (remote) read_index */
        coll_ep->rx_index++;

        if (ucs_atomic_fand64(&read_index_elem->pending, ~iface->my_mask) == iface->my_mask) {
            /* Update the tail - marking this element as usable again */
            uct_mm_coll_progress_fifo_tail(iface, coll_ep);
        }

        return 1;
    }

    return 0;
}

unsigned uct_mm_coll_iface_progress(void *arg)
{
    unsigned count = 0;
    uct_mm_coll_iface_t *iface = arg;
    uct_mm_coll_peer_ep_t *iter = iface->eps;
    uct_mm_coll_ep_t *next_ep = iter->ep;
    int is_incast = 0;

    do { /* One (loop-back) endpoint is always present */
        count += uct_mm_coll_iface_poll_fifo(iface, next_ep, is_incast);
        iter++;
        next_ep = iter->ep;
        is_incast = 1;
    } while (next_ep);

    /* progress the pending sends (if there are any) */
    ucs_arbiter_dispatch(&iface->super.arbiter, 1, uct_mm_ep_process_pending, NULL);

    return count;
}
