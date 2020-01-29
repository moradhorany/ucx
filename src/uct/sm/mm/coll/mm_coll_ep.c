/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "mm_coll_ep.h"

#include <ucs/arch/atomic.h>
#include <ucs/async/async.h>

#define UCT_MM_COLL_IFACE_GET_FIFO_ELEM(_ep, _fifo, _index, _elem_size) \
        ((uct_mm_coll_fifo_element_t*)(((char*)(_fifo) + \
                (((_index) & (_ep)->fifo_mask) * (_elem_size)))))

#define UCT_MM_COLL_GET_BASE_ADDRESS(_is_short, _is_batch, _elem, _ep, \
                                     _iface, _is_loopback, _is_recv, _stride) \
({ \
    uint8_t *ret_address; \
    if (_is_short) { \
        ret_address = (uint8_t*)((_elem) + 1); \
    } else { \
        if (_is_loopback) { \
            ret_address = (_elem)->super.desc_data; \
        } else { \
            uct_mm_ep_t *mm_ep = (_is_recv) ? (_ep)->rx : (_ep)->tx; \
            UCS_V_UNUSED ucs_status_t status = uct_mm_ep_get_remote_seg(mm_ep, \
                    (_elem)->super.desc.seg_id, (_elem)->super.desc.seg_size, \
                    (void**)&ret_address); \
            ucs_assert(status == UCS_OK); \
            VALGRIND_MAKE_MEM_DEFINED(ret_address, \
                                      (_elem)->super.desc.seg_size); \
            ret_address += (_elem)->super.desc.offset; \
        } \
    } \
    if ((_is_batch) && (!_is_loopback) && (!_is_recv)) { \
        ret_address += (_ep)->my_offset * (_stride); \
        if (_is_short) { \
            ucs_assert(((_ep)->my_offset * (_stride)) < ((_is_recv ? \
                       (_ep)->rx_elem_size : (_ep)->tx_elem_size) - \
                       sizeof(uct_mm_coll_fifo_element_t))); \
        } else { \
            ucs_assert(((_ep)->my_offset * (_stride)) < \
                       (_iface)->super.config.seg_size); \
        } \
    } \
    ret_address; \
})
/* TODO: support also scatter/gather-type collectives in this macro */

/*
 * This function is the common send function for three types of shared-memory
 * collective interfaces: bcoll (Batched), lcoll (Locked) and ccoll (Mixed).
 * The intent is to accomodate different kinds of contstraints - resulting in
 * different performance profiles. For example, lcoll should fit large buffers
 * reduced by a small amount of processes, but not for other cases.
 *
 * Basically, here's how the three work for reduce (1/2/3 are buffers from the
 * respective ranks, and 'p' stands for padding to cache-line size):
 * 1. bcoll ("batched" mode, where buffers are written in separate cache-lines):
 *     - is_pending_batched=false
 *     - is_payload_batched=true
 *
 *   | element->pending = 0 |      |      |      |      |
 *   | element->pending = 1 |      |      | 222p |      |
 *   | element->pending = 2 |      | 111p | 222p |      |
 *   | element->pending = 3 |      | 111p | 222p | 333p |
 *
 * 2. lcoll ("locked" mode, where the reduction is done by the sender):
 *     - is_pending_batched=false
 *     - is_payload_batched=false
 *
 *   | element->pending = 0 |             |
 *   | element->pending = 1 | 222         |
 *   | element->pending = 2 | 222+111     |
 *   | element->pending = 3 | 222+111+333 |
 *
 * 3. ccoll ("centralized" mode, like "batched" but with recv-side completion):
 *     - is_pending_batched=true
 *     - is_payload_batched=true
 *
 *   | element->pending = 0 | ???-0 | ???-0 | ???-0 |
 *   | element->pending = 0 | ???-0 | 222-1 | ???-0 |
 *   | element->pending = 2 | 111-1 | 222-1 | ???-0 | < rank#0 "triggers" checks
 *   | element->pending = 3 | 111-1 | 222-1 | 333-1 |
 *                        ^       ^       ^       ^
 *                        ^      #1      #2      #3  -> the last byte is polled
 *                        ^                             by the receiver process.
 *                        ^
 *                        the receiver process polls all these last bytes, and
 *                        once all the bytes have been set - the reciever knows
 *                        this operation is complete (none of the senders know).
 *
 * To summarize the differences:
 *
 *       | does the reduction |     mutual exclusion     | typically good for
 * ----------------------------------------------------------------------------
 * bcoll |      receiver      | "pending" is atomic      | small size, low PPN
 * ----------------------------------------------------------------------------
 * lcoll |      sender        | element access is atomic | large size
 * ----------------------------------------------------------------------------
 * mcoll |      receiver      | not mutually excluding   | small size, high PPN
 *
 * Note: this function can also run in "broadcast mode" (see "is_bcast"), where
 * the sender is placing one message and all the other processes connected to
 * this FIFO are reading it. This also requires counting - to keep track of when
 * all the processes have seen this message and it can be released. This "bcast"
 * flow is the same for the three aforementioned methods.
 */
static UCS_F_ALWAYS_INLINE ssize_t
uct_mm_coll_ep_am_common_send(int is_short,
                              int is_payload_batched,
                              int is_pending_batched,
                              uct_ep_h tl_ep, uint8_t am_id, size_t length,
                              uint64_t header, const void *payload,
                              uct_pack_callback_t pack_cb,
                              void *arg, unsigned flags)
{
    uct_mm_coll_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_mm_coll_iface_t);
    uct_mm_coll_ep_t *coll_ep = ucs_derived_of(tl_ep, uct_mm_coll_ep_t);
    uint8_t elem_flags;

    /* Sanity checks */
    UCT_CHECK_AM_ID(am_id);

    /* Grab the next cell I haven't yet written to */
    unsigned head   = ++coll_ep->tx_index;
    uct_mm_ep_t *ep = coll_ep->tx;

    /* check if there is room in the remote process's receive FIFO to write */
    if (!UCT_MM_EP_IS_ABLE_TO_SEND(head, ep->cached_tail, iface->super.config.fifo_size)) {
        if (!ucs_arbiter_group_is_empty(&ep->arb_group)) {
            /* pending isn't empty. don't send now to prevent out-of-order sending */
            coll_ep->tx_index--;
            UCS_STATS_UPDATE_COUNTER(ep->super.stats, UCT_EP_STAT_NO_RES, 1);
            return UCS_ERR_NO_RESOURCE;
        } else {
            /* pending is empty */
            /* update the local copy of the tail to its actual value on the remote peer */
            uct_mm_ep_update_cached_tail(ep);
            if (!UCT_MM_EP_IS_ABLE_TO_SEND(head, ep->cached_tail, iface->super.config.fifo_size)) {
                coll_ep->tx_index--;
                UCS_STATS_UPDATE_COUNTER(ep->super.stats, UCT_EP_STAT_NO_RES, 1);
                return UCS_ERR_NO_RESOURCE;
            }
        }
    }

    /* If the payload is batched - calculate the stride size (otherwise 0) */
    int is_bcast = (coll_ep == iface->eps);
    size_t stride = (is_short ? (coll_ep->tx_elem_size -
                                 sizeof(uct_mm_coll_fifo_element_t)) :
                                (iface->super.config.seg_size));
    if (!is_bcast) {
        stride /= (coll_ep->tx_cnt + 1);
    }

    /* Obtain the next element this process should access */
    uct_mm_coll_fifo_element_t *elem =
            UCT_MM_COLL_IFACE_GET_FIFO_ELEM(coll_ep, ep->fifo_elems, head,
                                            coll_ep->tx_elem_size);

    /* Check my "position" in the order of writers to this element */
    uint8_t *base_address;
    uint32_t previous_pending;
    if (!is_bcast && !is_payload_batched) {
        ucs_spin_lock(&elem->lock);
        previous_pending = elem->pending++;
    }

    /* Write the buffer (or reduce onto an existing buffer) */
    base_address = UCT_MM_COLL_GET_BASE_ADDRESS(is_short, is_payload_batched,
            elem, coll_ep, iface, is_bcast, 0 /* _is_recv */, stride);
    if (is_short) {
        /* Assuming there's no aggregation - just place the payload and go */
        uct_am_short_fill_data(base_address, header, payload, length);

        length += sizeof(header);
        ucs_assert(is_payload_batched);
    } else {
        /* For some reduce operations - ask the callback to do the reduction */
        if (!is_bcast &&
            !is_payload_batched &&
            ucs_likely(previous_pending != 0)) {
            ucs_assert_always((((uintptr_t)arg) & UCT_PACK_CALLBACK_REDUCE) == 0);
            arg = (void*)((uintptr_t)arg | UCT_PACK_CALLBACK_REDUCE);
        }

        /* Write the portion of this process into the shared buffer */
        length = pack_cb(base_address, arg);
    }

    /* No need to mess with coordination if I'm the only writer (broadcast) */
    if (is_bcast) {
        ucs_assert(elem->pending == 0);
        elem->pending = 1;
        goto last_writer;
    }

    /* ccoll ("centralized") mode only: mark my slot as "written" */
    if (is_pending_batched) {
        ucs_assert(length < stride);

        /* Make sure data is written before the "done" flag */
        ucs_memory_cpu_store_fence();

        /* Mark own slot as "done" */
        base_address[stride - 1] = UCT_MM_FIFO_ELEM_FLAG_OWNER;

        /* One process notifies the receiver (doesn't mean others are done) */
        if (coll_ep->my_offset == 0) {
            goto last_writer;
        } else {
            goto trace_send;
        }
    }

    if (is_payload_batched) {
        /* bcoll ("batched") mode only: atomic increment of the pending count */
        ucs_memory_cpu_store_fence();
        previous_pending = ucs_atomic_fadd32(&elem->pending, 1);
    } else {
        /* lcoll ("locked") mode only: unlock the element for the next proc. */
        ucs_spin_unlock(&elem->lock);
    }

skip_payload:
    /* Check if this sender is the last expected sender for this element */
    if (previous_pending == coll_ep->tx_cnt) {
last_writer:
        /* change the owner bit to indicate that the writing is complete.
         * the owner bit flips after every FIFO wraparound */
        elem_flags = (elem->super.flags & UCT_MM_FIFO_ELEM_FLAG_OWNER) ^
                                          UCT_MM_FIFO_ELEM_FLAG_OWNER;
        if (is_short) {
            elem_flags |= UCT_MM_FIFO_ELEM_FLAG_INLINE;
        }

        elem->super.length = is_payload_batched ? stride : length;
        elem->super.am_id  = am_id;

        /* memory barrier - make sure that the memory is flushed before setting the
         * 'writing is complete' flag which the reader checks */
        ucs_memory_cpu_store_fence();

        /* Set this element as "written" - pass ownership to the receiver */
        elem->super.flags = elem_flags;

        /* update the remote head element */
        ep->fifo_ctl->head = head;

        /* signal remote, if so requested */
        if (ucs_unlikely(flags & UCT_SEND_FLAG_SIGNALED)) {
            uct_mm_ep_signal_remote(ep);
        }
    }

trace_send:
    uct_iface_trace_am(&iface->super.super.super, UCT_AM_TRACE_TYPE_SEND, am_id,
                       base_address, length, is_short ? "TX: AM_SHORT" :
                                                        "TX: AM_BCOPY");
    if (is_short) {
        UCT_TL_EP_STAT_OP(&ep->super, AM, SHORT, length);
    } else {
        UCT_TL_EP_STAT_OP(&ep->super, AM, BCOPY, length);
    }

    return UCS_OK;
}

ssize_t uct_mm_lcoll_ep_am_bcopy(uct_ep_h tl_ep, uint8_t id,
                                 uct_pack_callback_t pack_cb,
                                 void *arg, unsigned flags)
{
    return uct_mm_coll_ep_am_common_send(0, 0, 0, tl_ep, id, 0, 0,
                                         NULL, pack_cb, arg, flags);
}

ucs_status_t uct_mm_bcoll_ep_am_short(uct_ep_h tl_ep, uint8_t id, uint64_t header,
                                     const void *payload, unsigned length)
{
    ssize_t ret = uct_mm_coll_ep_am_common_send(1, 1, 0, tl_ep, id, length,
                                                header, payload, NULL, NULL, 0);
    return (ret > 0) ? UCS_OK : (ucs_status_t)ret;
}

ssize_t uct_mm_bcoll_ep_am_bcopy(uct_ep_h tl_ep, uint8_t id,
                                 uct_pack_callback_t pack_cb,
                                 void *arg, unsigned flags)
{
    return uct_mm_coll_ep_am_common_send(0, 1, 0, tl_ep, id, 0, 0,
                                         NULL, pack_cb, arg, flags);
}

ucs_status_t uct_mm_ccoll_ep_am_short(uct_ep_h tl_ep, uint8_t id, uint64_t header,
                                     const void *payload, unsigned length)
{
    ssize_t ret = uct_mm_coll_ep_am_common_send(1, 1, 1, tl_ep, id, length,
                                                header, payload, NULL, NULL, 0);
    return (ret > 0) ? UCS_OK : (ucs_status_t)ret;
}

ssize_t uct_mm_ccoll_ep_am_bcopy(uct_ep_h tl_ep, uint8_t id,
                                 uct_pack_callback_t pack_cb,
                                 void *arg, unsigned flags)
{
    return uct_mm_coll_ep_am_common_send(0, 1, 1, tl_ep, id, 0, 0,
                                         NULL, pack_cb, arg, flags);
}

UCS_CLASS_INIT_FUNC(uct_mm_coll_ep_t, const uct_ep_params_t *params)
{
    ucs_status_t status;
    uct_mm_coll_iface_t *iface = ucs_derived_of(params->iface,
                                                uct_mm_coll_iface_t);
    ucs_assert(params->field_mask & UCT_EP_PARAM_FIELD_IFACE);
    ucs_assert(params->field_mask & UCT_EP_PARAM_FIELD_IFACE_ADDR);

    UCS_CLASS_CALL_SUPER_INIT(uct_base_ep_t, &iface->super.super.super);

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

    self->coll_id         = addr->coll_id;
    self->my_offset       = iface->my_coll_id -
                            (uint32_t)(addr->coll_id < iface->my_coll_id);
    self->fifo_shift      = iface->super.fifo_shift;
    self->tx_cnt          = iface->sm_proc_cnt - 2;
    self->tx_index        = (typeof(self->tx_index))-1;
    self->rx_index        = 0;
    self->fifo_mask       = iface->super.fifo_mask;
    self->rx_elem_size    = is_loopback ? iface->super.config.fifo_elem_size:
                                          iface->bcast.config.fifo_elem_size;
    self->tx_elem_size    = is_loopback ? iface->bcast.config.fifo_elem_size:
                                          iface->super.config.fifo_elem_size;
    self->ref_count       = 0;
    self->is_flags_cached = 0;
    self->flags_cache     = 0;

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

static UCS_F_ALWAYS_INLINE void
uct_mm_coll_ep_update_tail(uct_mm_ep_t *mm_ep, uint64_t index)
{
    (void) ucs_atomic_cswap64(&mm_ep->fifo_ctl->tail, index, index + 1);
}

static UCS_F_ALWAYS_INLINE int
uct_mm_coll_ep_is_last_to_read(uct_mm_coll_fifo_element_t *elem,
                               uct_mm_ep_t *mm_ep,
                               uint32_t tx_cnt,
                               uint64_t last_index)
{
    uint32_t pending = ucs_atomic_fadd32(&elem->pending, 1);
    if (pending == tx_cnt + 1) {
        elem->pending = 0;
        uct_mm_coll_ep_update_tail(mm_ep, last_index);
        return 1;
    }
    return 0;
}

static UCS_F_ALWAYS_INLINE uct_mm_coll_fifo_element_t*
uct_mm_coll_ep_get_next_rx_elem(uct_mm_coll_fifo_element_t* elem,
                                uct_mm_coll_ep_t *ep,
                                uct_mm_ep_t *mm_ep,
                                uint64_t *elem_index)
{
    if (ucs_likely(++(*elem_index) & ep->fifo_mask)) {
        return (uct_mm_coll_fifo_element_t*)((char*)elem + ep->rx_elem_size);
    }

    return UCT_MM_COLL_IFACE_GET_FIFO_ELEM(ep, mm_ep->fifo_elems, *elem_index,
                                           ep->rx_elem_size);
}

void uct_mm_coll_ep_release_desc(uct_mm_coll_ep_t *coll_ep, void *desc)
{
    ucs_status_t UCS_V_UNUSED status;
    void *elem_desc;
    uct_mm_ep_t *mm_ep               = coll_ep->rx;
    uint64_t elem_index              = mm_ep->fifo_ctl->tail;
    uct_mm_iface_t *iface            = ucs_derived_of(mm_ep->super.super.iface,
                                                      uct_mm_iface_t);
    size_t rx_headroom               = iface->rx_headroom;
    uct_mm_coll_fifo_element_t *elem = UCT_MM_COLL_IFACE_GET_FIFO_ELEM(coll_ep,
            mm_ep->fifo_elems, elem_index, coll_ep->rx_elem_size);
    uct_mm_seg_id_t seg_id = elem->super.desc.seg_id;

new_segment:
    /* Find the base address for the remote segment of this element */
    status = uct_mm_ep_get_remote_seg(mm_ep, seg_id,
            elem->super.desc.seg_size, &elem_desc);
    elem_desc = UCS_PTR_BYTE_OFFSET(elem_desc - 1, -rx_headroom);
    ucs_assert(status == UCS_OK); /* since it had to have been attached */

    while ((elem->super.flags & UCT_MM_FIFO_ELEM_FLAG_INLINE) || (desc != elem_desc)) {
        ucs_assert(elem_index <= coll_ep->rx_index); /* should find eventually */
        elem = uct_mm_coll_ep_get_next_rx_elem(elem, coll_ep, mm_ep, &elem_index);
        if (ucs_unlikely(seg_id != elem->super.desc.seg_id)) {
            seg_id = elem->super.desc.seg_id;
            goto new_segment;
        }
    }

    /* Check if this element has been released by all peers and can be re-used */
    if (uct_mm_coll_ep_is_last_to_read(elem, mm_ep, coll_ep->tx_cnt, elem_index)) {
        while ((elem_index < coll_ep->rx_index) && (elem->pending == 0)) {
            uct_mm_coll_ep_update_tail(mm_ep, elem_index);
            elem = uct_mm_coll_ep_get_next_rx_elem(elem, coll_ep, mm_ep, &elem_index);
        }
    }
}

static UCS_F_ALWAYS_INLINE
int uct_mm_coll_iface_is_ccoll_elem_ready(uct_mm_coll_fifo_element_t *elem,
                                          uint8_t *base_address,
                                          uint32_t num_slots,
                                          unsigned slot_size)
{
    uint32_t slot_iter     = elem->pending; /* start from last known position */
    uint8_t *slot_iter_ptr = base_address + ((slot_iter + 1) * slot_size) - 1;

    while ((slot_iter < num_slots) && (*slot_iter_ptr)) {
        ucs_assert(*slot_iter_ptr == UCT_MM_FIFO_ELEM_FLAG_OWNER);
        *slot_iter_ptr = 0;
        slot_iter_ptr += slot_size;
        slot_iter++;
    }

    elem->pending = slot_iter;
    return (slot_iter == num_slots);
}

/**
 * This function polls for incoming messages, either incast (sent to me) or
 * bcast (sent by somebody on a group I'm a member of). Specifically, this
 * fuction is used in "loopback mode" to check for incast, in which case the
 * passed endpoint is my own. After invoking the Active Message handler, the
 * return value may indicate that this message still needs to be kept
 * (UCS_INPROGRESS), and the appropriate callbacks are set for releasing it in
 * the future (by an upper layer calling @ref uct_iface_release_desc ).
 */
static UCS_F_ALWAYS_INLINE
unsigned uct_mm_coll_iface_poll_fifo(uct_mm_coll_iface_t *iface,
                                     uct_mm_coll_ep_t *coll_ep,
                                     int is_payload_batched,
                                     int is_pending_batched,
                                     int is_loopback)
{
    uct_mm_ep_t *ep     = coll_ep->rx; /* Look at the remote peer's bcast */
    unsigned read_index = coll_ep->rx_index;

    /* the fifo_element which the read_index points to */
    uct_mm_coll_fifo_element_t *elem = UCT_MM_COLL_IFACE_GET_FIFO_ELEM(coll_ep,
            ep->fifo_elems, read_index, coll_ep->rx_elem_size);

    /* check if the owner bit is cached */
    uint8_t flags = elem->super.flags;
    if (ucs_likely(coll_ep->is_flags_cached && coll_ep->flags_cache == flags)) {
        return 0;
    }

    /* check the read_index to see if there is a new item to read (checking the owner bit) */
    uint8_t owner_bit = flags & UCT_MM_FIFO_ELEM_FLAG_OWNER;
    if (((read_index >> coll_ep->fifo_shift) & 1) != owner_bit) {
        coll_ep->flags_cache     = flags;
        coll_ep->is_flags_cached = 1;
        return 0;
    }

    ucs_memory_cpu_load_fence();
    coll_ep->is_flags_cached = 0;

    /* Detect incoming message parameters */
    int is_short          = flags & UCT_MM_FIFO_ELEM_FLAG_INLINE;
    uint8_t *base_address = UCT_MM_COLL_GET_BASE_ADDRESS(is_short,
            0 /* _is_batch */, elem, coll_ep, iface, is_loopback,
            1 /* _is_recv */, 0 /* _stride */);

    /* ccoll ("centralized") mode only - check if this is the last writer */
    unsigned stride  = elem->super.length;
    uint8_t proc_cnt = coll_ep->tx_cnt + 1;
    if (is_pending_batched && is_loopback) {
        if (!uct_mm_coll_iface_is_ccoll_elem_ready(elem,
                base_address, proc_cnt, stride)) {
            return 0; /* incast started, but not all peers have written yet */
        }
        ucs_memory_cpu_load_fence();
    }

    /* choose the flags for the Active Message callback argument */
    int am_cb_flags = is_payload_batched ? UCT_CB_PARAM_FLAG_STRIDE : 0;
    if (!is_short) {
        if (is_loopback) {
            am_cb_flags |= UCT_CB_PARAM_FLAG_DESC;
        } else {
            am_cb_flags |= UCT_CB_PARAM_FLAG_DESC | UCT_CB_PARAM_FLAG_SHARED;
        }
    }

    /* Process the incoming message using the active-message callback */
    ucs_status_t status = uct_iface_invoke_am(&iface->super.super.super,
            elem->super.am_id, base_address, stride, am_cb_flags);

    /*
     * This descriptor may resides on memory belonging to another process.
     * The consequence is that it can only be accessed for reading, not
     * writing (technically writing is possible, but would conflict with
     * other processes using this descriptor). UCT_CB_PARAM_FLAG_SHARED is
     * used to pass this information to upper layers.
     */
    if (ucs_unlikely(status == UCS_INPROGRESS)) {
        void *desc = base_address - iface->super.rx_headroom;
        ucs_assert(!is_short);

        /* If I'm the owner of this memory - I can replace the element's segment */
        if (is_loopback) {
            /* assign a new receive descriptor to this FIFO element.*/
            uct_mm_assign_desc_to_fifo_elem(&iface->super, &elem->super, 1);

            /* ccoll ("centralized") mode only - mark my slot as "written" */
            if (is_pending_batched && is_loopback) {
                uct_mm_coll_iface_init_ccoll_desc(elem, stride, proc_cnt);
            }

            /* later release of this desc - the easy way */
            uct_recv_desc(desc) = (uct_recv_desc_t*)&iface->super.release_desc;

            /* Mark element as done (and re-usable) */
            uct_mm_coll_ep_is_last_to_read(elem, ep, coll_ep->tx_cnt, read_index);
        } else {
            /* set information for @ref uct_mm_coll_iface_release_shared_desc */
            uct_recv_desc(desc) = (void*)((uintptr_t)(coll_ep->coll_id));
        }
    } else {
        /* Mark element as done (and re-usable) */
        uct_mm_coll_ep_is_last_to_read(elem, ep, coll_ep->tx_cnt, read_index);
    }

    coll_ep->rx_index++;

    uct_iface_trace_am(&iface->super.super.super, UCT_AM_TRACE_TYPE_RECV,
            elem->super.am_id, base_address, elem->super.length,
            is_short ? "RX: AM_SHORT" : "RX: AM_BCOPY");

    return 1;
}

static UCS_F_ALWAYS_INLINE
unsigned uct_mm_coll_iface_common_progress(uct_iface_h iface,
                                           int is_payload_batched,
                                           int is_pending_batched)
{
    uct_mm_coll_iface_t *coll_iface = ucs_derived_of(iface,uct_mm_coll_iface_t);
    uct_mm_coll_ep_t *next_ep       = coll_iface->eps;
    unsigned count;

    /* progress the pending sends (if there are any) */
    ucs_arbiter_dispatch(&coll_iface->super.arbiter, 1,
            uct_mm_ep_process_pending, NULL);

    count = uct_mm_coll_iface_poll_fifo(coll_iface,
            next_ep, is_payload_batched, is_pending_batched,
            1 /* the first is always the loopback endpoint */);
    /* favor faster processing of incoming messages (as root) */
    if (ucs_unlikely(count)) {
        return 1;
    }

    /* test the rest of the endpoints for incoming messages */
    count = 0;
    while (++next_ep < coll_iface->eps_limit) {
        count += uct_mm_coll_iface_poll_fifo(coll_iface, next_ep,
                is_payload_batched, is_pending_batched, 0);
    }
    return count;
}

unsigned uct_mm_lcoll_iface_progress(uct_iface_h iface)
{
    return uct_mm_coll_iface_common_progress(iface, 0, 0);
}

unsigned uct_mm_bcoll_iface_progress(uct_iface_h iface)
{
    return uct_mm_coll_iface_common_progress(iface, 1, 0);
}

unsigned uct_mm_ccoll_iface_progress(uct_iface_h iface)
{
    return uct_mm_coll_iface_common_progress(iface, 1, 1);
}

ucs_status_t uct_mm_coll_ep_create(const uct_ep_params_t *params, uct_ep_h *ep_p)
{
    uct_iface_h iface_h        = params->iface;
    uct_mm_coll_iface_t *iface = ucs_derived_of(iface_h, uct_mm_coll_iface_t);
    uct_mm_coll_ep_t *ep_iter  = iface->eps;

    ucs_assert(params->field_mask & UCT_EP_PARAM_FIELD_IFACE);
    ucs_assert(params->field_mask & UCT_EP_PARAM_FIELD_IFACE_ADDR);

    /* look for the identifier among the existing endpoints (for re-use) */
    uint8_t coll_id = ((uct_mm_coll_iface_addr_t*)params->iface_addr)->coll_id;
    while ((ep_iter < iface->eps_limit) && (coll_id != ep_iter->coll_id)) {
        ep_iter++;
    }

    if (ep_iter < iface->eps_limit) {
        ep_iter->ref_count++;
        *ep_p = (uct_ep_h)ep_iter;
        return UCS_OK;
    }

    if (ep_iter == &iface->eps[iface->ep_cnt]) {
        uct_priv_worker_t *worker = iface->super.super.super.worker;
        ucs_assert(iface->ep_cnt < iface->sm_proc_cnt);

        UCS_ASYNC_BLOCK(worker->async);

        size_t new_size = 2 * iface->ep_cnt * sizeof(uct_mm_coll_ep_t);
        int ret = ucs_posix_memalign_realloc((void**)&iface->eps,
                UCS_SYS_CACHE_LINE_SIZE, new_size, "mm_coll_eps");
        if (ret) {
            UCS_ASYNC_UNBLOCK(worker->async);
            return UCS_ERR_NO_MEMORY;
        }

        ep_iter          = &iface->eps[iface->ep_cnt];
        iface->ep_cnt   *= 2;
        iface->eps_limit = ep_iter + 1;

        UCS_ASYNC_UNBLOCK(worker->async);
    } else {
        iface->eps_limit = ep_iter + 1;
    }

    *ep_p = (uct_ep_h)ep_iter;

    /* Use the vacant slot to store the new endpoint instance */
    return UCS_CLASS_INIT(uct_mm_coll_ep_t, ep_iter, params);
}

void uct_mm_coll_ep_destroy(uct_ep_h ep)
{
    uct_mm_coll_ep_t *mm_coll_ep = ucs_derived_of(ep, uct_mm_coll_ep_t);
    if (!mm_coll_ep->ref_count--) {
        UCS_CLASS_CLEANUP(uct_mm_coll_ep_t, ep);
    }
}
