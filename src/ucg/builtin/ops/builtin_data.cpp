/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <string.h>

#include <ucs/datastruct/queue.h>
#include <ucs/datastruct/list.h>
#include <ucs/profile/profile.h>
#include <ucs/debug/memtrack.h>
#include <ucs/debug/assert.h>
#include <ucp/dt/dt_contig.h>

#define UCG_FRAGMENT_SIZE 8128

#if HAVE_OMPI_SRC
#include <ucs/vector/vector.hpp>
UCS_VECTOR_DECLARE(builtin, UCG_FRAGMENT_SIZE)
#endif

#include "builtin_cb.inl"

/******************************************************************************
 *                                                                            *
 *                            Operation Execution                             *
 *                                                                            *
 ******************************************************************************/

ucs_status_t static UCS_F_ALWAYS_INLINE
ucg_builtin_step_dummy_send(ucg_builtin_request_t *req,
        ucg_builtin_op_step_t *step, uct_ep_h ep, int is_single_send)
{
    ucs_assert((step->flags & (UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SLOCK |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BLOCK |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY)) == 0);
    return UCS_OK;
}

static UCS_F_ALWAYS_INLINE ucs_status_t
ucg_builtin_step_am_short_one(ucg_builtin_request_t *req,
        ucg_builtin_op_step_t *step, uct_ep_h ep, int is_single_send)
{
    ucs_assert((step->flags & (UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SLOCK |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BLOCK |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY)) ==
                                       UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT);
    int8_t *sbuf = (step->flags & UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF) ?
            step->recv_buffer : step->send_buffer;
    return step->uct_iface->ops.ep_am_short(ep, step->am_id,
            step->am_header.header, sbuf, step->buffer_length);
}

static UCS_F_ALWAYS_INLINE ucs_status_t
ucg_builtin_step_am_short_max(ucg_builtin_request_t *req,
        ucg_builtin_op_step_t *step, uct_ep_h ep, int is_single_send) {
    ucs_status_t status;
    unsigned am_id               = step->am_id;
    ucg_offset_t frag_size       = step->fragment_length;
    int8_t *sbuf                 = (step->flags &
                                    UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF) ?
                                   step->recv_buffer : step->send_buffer;
    int8_t *buffer_iter          = sbuf + step->iter_offset;
    int8_t *buffer_iter_limit    = sbuf + step->buffer_length - frag_size;
    ucg_builtin_header_t am_iter = { .header = step->am_header.header };
    am_iter.remote_offset       += step->iter_offset;
    ucs_status_t (*ep_am_short)(uct_ep_h, uint8_t, uint64_t, const void*, unsigned) =
            step->uct_iface->ops.ep_am_short;

    ucs_assert((step->flags & (UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SLOCK |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BLOCK |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY)) ==
                                       UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT);
    ucs_assert(step->iter_offset != UCG_BUILTIN_OFFSET_PIPELINE_READY);
    ucs_assert(step->iter_offset != UCG_BUILTIN_OFFSET_PIPELINE_PENDING);

    /* send every fragment but the last */
    if (ucs_likely(buffer_iter < buffer_iter_limit)) {
        do {
            status = ep_am_short(ep, am_id, am_iter.header, buffer_iter, frag_size);

            if (is_single_send) {
                return status;
            }

            buffer_iter           += frag_size;
            am_iter.remote_offset += frag_size;
        } while ((status == UCS_OK) && (buffer_iter < buffer_iter_limit));

        /* send last fragment of the message */
        if (ucs_unlikely(status != UCS_OK)) {
            /* assuming UCS_ERR_NO_RESOURCE, restore the state for re-entry */
            step->iter_offset = buffer_iter - frag_size - sbuf;
            return status;
        }
    }

    status = ep_am_short(ep, am_id, am_iter.header, buffer_iter, sbuf + step->buffer_length - buffer_iter);
    step->iter_offset = (status == UCS_OK) ? 0 : buffer_iter - sbuf;
    return status;
}

#define UCG_BUILTIN_BCOPY_PACK_CB(source, offset, length) {                    \
    ucg_builtin_op_step_t *step      = (ucg_builtin_op_step_t*)arg;            \
    ucg_builtin_header_t *header_ptr = (ucg_builtin_header_t*)dest;            \
    header_ptr->header               = step->am_header.header;                 \
                                                                               \
    memcpy(header_ptr + 1, source + offset, length);                           \
    return sizeof(*header_ptr) + length;                                       \
}

static size_t ucg_builtin_step_am_bcopy_single_frag_packer_sbuf(void *dest, void *arg)
UCG_BUILTIN_BCOPY_PACK_CB(step->send_buffer, 0,                 step->buffer_length)

static size_t ucg_builtin_step_am_bcopy_full_frag_packer_sbuf(void *dest, void *arg)
UCG_BUILTIN_BCOPY_PACK_CB(step->send_buffer, step->iter_offset, step->fragment_length)

static size_t ucg_builtin_step_am_bcopy_partial_frag_packer_sbuf(void *dest, void *arg)
UCG_BUILTIN_BCOPY_PACK_CB(step->send_buffer, step->iter_offset, step->buffer_length - step->iter_offset)

static size_t ucg_builtin_step_am_bcopy_single_frag_packer_rbuf(void *dest, void *arg)
UCG_BUILTIN_BCOPY_PACK_CB(step->recv_buffer, 0,                 step->buffer_length)

static size_t ucg_builtin_step_am_bcopy_full_frag_packer_rbuf(void *dest, void *arg)
UCG_BUILTIN_BCOPY_PACK_CB(step->recv_buffer, step->iter_offset, step->fragment_length)

static size_t ucg_builtin_step_am_bcopy_partial_frag_packer_rbuf(void *dest, void *arg)
UCG_BUILTIN_BCOPY_PACK_CB(step->recv_buffer, step->iter_offset, step->buffer_length - step->iter_offset)

#define UCG_BUILTIN_COLL_PACK_CB(source, offset, length, part) {               \
    ucg_builtin_request_t *req = (ucg_builtin_request_t*)arg;                  \
                                                                               \
    /* First writer to this buffer - overwrite the existing data */            \
    if (ucs_unlikely(!lock)) {                                                 \
        arg = (ucg_builtin_op_step_t*)(req->step);                             \
        UCG_BUILTIN_BCOPY_PACK_CB(source, offset, length)                      \
    } else {                                                                   \
        /* Otherwise - reduce onto existing data */                            \
        ucg_builtin_op_step_t *step = (ucg_builtin_op_step_t*)arg;             \
        ucg_builtin_header_t *header_ptr = (ucg_builtin_header_t*)dest;        \
        return sizeof(*header_ptr) + ucg_builtin_atomic_reduce_ ## part        \
                (req, offset, source, header_ptr + 1, length, lock);           \
    }                                                                          \
}

static size_t ucg_builtin_step_am_coll_single_frag_packer_sbuf(void *dest, ucs_spinlock_t *lock, void *arg)
UCG_BUILTIN_COLL_PACK_CB(step->send_buffer, 0,                 step->buffer_length, partial)

static size_t ucg_builtin_step_am_coll_full_frag_packer_sbuf(void *dest, ucs_spinlock_t *lock, void *arg)
UCG_BUILTIN_COLL_PACK_CB(step->send_buffer, step->iter_offset, step->fragment_length, full)

static size_t ucg_builtin_step_am_coll_partial_frag_packer_sbuf(void *dest, ucs_spinlock_t *lock, void *arg)
UCG_BUILTIN_COLL_PACK_CB(step->send_buffer, step->iter_offset, step->buffer_length - step->iter_offset, partial)

static size_t ucg_builtin_step_am_coll_single_frag_packer_rbuf(void *dest, ucs_spinlock_t *lock, void *arg)
UCG_BUILTIN_COLL_PACK_CB(step->recv_buffer, 0,                 step->buffer_length, partial)

static size_t ucg_builtin_step_am_coll_full_frag_packer_rbuf(void *dest, ucs_spinlock_t *lock, void *arg)
UCG_BUILTIN_COLL_PACK_CB(step->recv_buffer, step->iter_offset, step->fragment_length, full)

static size_t ucg_builtin_step_am_coll_partial_frag_packer_rbuf(void *dest, ucs_spinlock_t *lock, void *arg)
UCG_BUILTIN_COLL_PACK_CB(step->recv_buffer, step->iter_offset, step->buffer_length - step->iter_offset, partial)

ucs_status_t static UCS_F_ALWAYS_INLINE
ucg_builtin_step_am_packed_send_one_common(ucg_builtin_request_t *req,
        ucg_builtin_op_step_t *step, uct_ep_h ep, uct_pack_callback_t packer_cb,
        enum ucg_builtin_op_step_flags type, void *cb_arg)
{
    ucs_assert((step->flags & (UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SLOCK |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BLOCK |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY)) == type);

    /* send active message to remote endpoint */
    ssize_t len;
    switch (type) {
    case UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SLOCK:
        len = step->uct_iface->ops.ep_am_slock(ep, step->am_id, (uct_locked_pack_callback_t)packer_cb, cb_arg, 0);
        break;
    case UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY:
        len = step->uct_iface->ops.ep_am_bcopy(ep, step->am_id, packer_cb, cb_arg, 0);
        break;
    case UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BLOCK:
        len = step->uct_iface->ops.ep_am_block(ep, step->am_id, (uct_locked_pack_callback_t)packer_cb, cb_arg, 0);
        break;
    default:
        return UCS_ERR_INVALID_PARAM;
    }
    return (ucs_unlikely(len < 0)) ? (ucs_status_t)len : UCS_OK;
}

ucs_status_t static UCS_F_ALWAYS_INLINE
ucg_builtin_step_am_slock_one(ucg_builtin_request_t *req,
        ucg_builtin_op_step_t *step, uct_ep_h ep, int is_single_send)
{
    /* send active message to remote endpoint */
    int use_rbuf = (step->flags & UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF);
    return ucg_builtin_step_am_packed_send_one_common(req, step, ep, use_rbuf ?
            (uct_pack_callback_t)ucg_builtin_step_am_coll_single_frag_packer_rbuf:
            (uct_pack_callback_t)ucg_builtin_step_am_coll_single_frag_packer_sbuf,
            UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SLOCK, req);
}

ucs_status_t static UCS_F_ALWAYS_INLINE
ucg_builtin_step_am_bcopy_one(ucg_builtin_request_t *req,
        ucg_builtin_op_step_t *step, uct_ep_h ep, int is_single_send)
{
    /* send active message to remote endpoint */
    int use_rbuf = (step->flags & UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF);
    return ucg_builtin_step_am_packed_send_one_common(req, step, ep, use_rbuf ?
            ucg_builtin_step_am_bcopy_single_frag_packer_rbuf:
            ucg_builtin_step_am_bcopy_single_frag_packer_sbuf,
            UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY, step);
}

ucs_status_t static UCS_F_ALWAYS_INLINE
ucg_builtin_step_am_block_one(ucg_builtin_request_t *req,
        ucg_builtin_op_step_t *step, uct_ep_h ep, int is_single_send)
{
    /* send active message to remote endpoint */
    int use_rbuf = (step->flags & UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF);
    return ucg_builtin_step_am_packed_send_one_common(req, step, ep, use_rbuf ?
            (uct_pack_callback_t)ucg_builtin_step_am_coll_single_frag_packer_rbuf:
            (uct_pack_callback_t)ucg_builtin_step_am_coll_single_frag_packer_sbuf,
            UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BLOCK, req);
}

typedef ssize_t (*packed_send_t)(uct_ep_h, uint8_t, uct_pack_callback_t, void*, unsigned);

ucs_status_t static UCS_F_ALWAYS_INLINE
ucg_builtin_step_am_packed_send_max_common(ucg_builtin_request_t *req,
        ucg_builtin_op_step_t *step, uct_ep_h ep, int is_single_send,
        uct_pack_callback_t packer_full_cb,
        uct_pack_callback_t packer_partial_cb,
        enum ucg_builtin_op_step_flags type, void *cb_arg) {
    ssize_t len;
    unsigned am_id           = step->am_id;
    ucg_offset_t frag_size   = step->fragment_length;
    ucg_offset_t iter_limit  = step->buffer_length - frag_size;

    packed_send_t send_func;
    switch (type) {
    case UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SLOCK:
        send_func = (packed_send_t)step->uct_iface->ops.ep_am_slock;
        break;
    case UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY:
        send_func = step->uct_iface->ops.ep_am_bcopy;
        break;
    case UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BLOCK:
        send_func = (packed_send_t)step->uct_iface->ops.ep_am_block;
        break;
    default:
        return UCS_ERR_INVALID_PARAM;
    }

    ucs_assert((step->flags & (UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SLOCK |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BLOCK |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY)) == type);
    ucs_assert(step->iter_offset != UCG_BUILTIN_OFFSET_PIPELINE_READY);
    ucs_assert(step->iter_offset != UCG_BUILTIN_OFFSET_PIPELINE_PENDING);

    /* check if this is not, by any chance, the last fragment */
    if (ucs_likely(step->iter_offset < iter_limit)) {
        /* send every fragment but the last */
        do {
            len = send_func(ep, am_id, packer_full_cb, cb_arg, 0);

            if (is_single_send) {
                return ucs_unlikely(len < 0) ? (ucs_status_t)len : UCS_OK;
            }

            step->am_header.remote_offset += frag_size;
            step->iter_offset             += frag_size;
        } while ((len >= 0) && (step->iter_offset < iter_limit));

        if (ucs_unlikely(len < 0)) {
            step->am_header.remote_offset -= frag_size;
            step->iter_offset             -= frag_size;
            return (ucs_status_t)len;
        }
    }

    /* Send last fragment of the message */
    len = send_func(ep, am_id, packer_partial_cb, cb_arg, 0);
    if (ucs_unlikely(len < 0)) {
        return (ucs_status_t)len;
    }

    step->am_header.remote_offset = 0;
    step->iter_offset = 0;
    return UCS_OK;
}

ucs_status_t static UCS_F_ALWAYS_INLINE
ucg_builtin_step_am_slock_max(ucg_builtin_request_t *req,
        ucg_builtin_op_step_t *step, uct_ep_h ep, int is_single_send) {
    int use_rbuf = step->flags & UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF;
    return ucg_builtin_step_am_packed_send_max_common(req, step, ep, is_single_send,
            ((uct_pack_callback_t)(use_rbuf ?
                    ucg_builtin_step_am_coll_full_frag_packer_rbuf :
                    ucg_builtin_step_am_coll_full_frag_packer_sbuf)),
            ((uct_pack_callback_t)(use_rbuf ?
                    ucg_builtin_step_am_coll_partial_frag_packer_rbuf :
                    ucg_builtin_step_am_coll_partial_frag_packer_sbuf)),
            UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SLOCK, req);
}

ucs_status_t static UCS_F_ALWAYS_INLINE
ucg_builtin_step_am_bcopy_max(ucg_builtin_request_t *req,
        ucg_builtin_op_step_t *step, uct_ep_h ep, int is_single_send) {
    int use_rbuf = step->flags & UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF;
    return ucg_builtin_step_am_packed_send_max_common(req, step, ep, is_single_send,
            use_rbuf ? ucg_builtin_step_am_bcopy_full_frag_packer_rbuf :
                       ucg_builtin_step_am_bcopy_full_frag_packer_sbuf,
            use_rbuf ? ucg_builtin_step_am_bcopy_partial_frag_packer_rbuf :
                       ucg_builtin_step_am_bcopy_partial_frag_packer_sbuf,
            UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY, step);
}

ucs_status_t static UCS_F_ALWAYS_INLINE
ucg_builtin_step_am_block_max(ucg_builtin_request_t *req,
        ucg_builtin_op_step_t *step, uct_ep_h ep, int is_single_send) {
    int use_rbuf = step->flags & UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF;
    return ucg_builtin_step_am_packed_send_max_common(req, step, ep, is_single_send,
            ((uct_pack_callback_t)(use_rbuf ?
                    ucg_builtin_step_am_coll_full_frag_packer_rbuf :
                    ucg_builtin_step_am_coll_full_frag_packer_sbuf)),
            ((uct_pack_callback_t)(use_rbuf ?
                    ucg_builtin_step_am_coll_partial_frag_packer_rbuf :
                    ucg_builtin_step_am_coll_partial_frag_packer_sbuf)),
            UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BLOCK, req);
}

ucs_status_t static UCS_F_ALWAYS_INLINE
ucg_builtin_step_am_zcopy_one(ucg_builtin_request_t *req,
        ucg_builtin_op_step_t *step, uct_ep_h ep, int is_single_send)
{
    uct_iov_t iov = {
            .buffer = (step->flags &
                       UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF) ?
                      step->recv_buffer : step->send_buffer,
            .length = step->buffer_length,
            .memh   = step->zcopy.memh,
            .stride = 0,
            .count  = 1
    };

    ucs_assert((step->flags & (UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SLOCK |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BLOCK |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY)) ==
                                       UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY);

    ucg_builtin_zcomp_t *zcomp = &step->zcopy.zcomp[step->iter_ep];
    zcomp->req = req;

    ucs_status_t status = step->uct_iface->ops.ep_am_zcopy(ep, step->am_id,
            &step->am_header, sizeof(step->am_header), &iov, 1, 0, &zcomp->comp);
    return ucs_unlikely(status != UCS_INPROGRESS) ? status : UCS_OK;
}

ucs_status_t static UCS_F_ALWAYS_INLINE
ucg_builtin_step_am_zcopy_max(ucg_builtin_request_t *req,
        ucg_builtin_op_step_t *step, uct_ep_h ep, int is_single_send)
{
    ucs_status_t status;
    unsigned am_id             = step->am_id;
    ucg_offset_t frag_size     = step->fragment_length;

    int8_t *sbuf               = (step->flags &
                                  UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF) ?
                                 step->recv_buffer : step->send_buffer;
    void* iov_buffer_limit     = sbuf + step->buffer_length - frag_size;
    unsigned zcomp_index       = step->iter_ep * step->fragments +
                                 step->iter_offset / step->fragment_length;
    ucg_builtin_zcomp_t *zcomp = &step->zcopy.zcomp[zcomp_index];
    ucs_status_t (*ep_am_zcopy)(uct_ep_h, uint8_t, const void*, unsigned,
            const uct_iov_t*, size_t, unsigned, uct_completion_t*) =
                    step->uct_iface->ops.ep_am_zcopy;

    uct_iov_t iov = {
            .buffer = sbuf + step->iter_offset,
            .length = frag_size,
            .memh   = step->zcopy.memh,
            .stride = 0,
            .count  = 1
    };


    ucs_assert((step->flags & (UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SLOCK |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BLOCK |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY)) ==
                                       UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY);
    ucs_assert(step->iter_offset != UCG_BUILTIN_OFFSET_PIPELINE_READY);
    ucs_assert(step->iter_offset != UCG_BUILTIN_OFFSET_PIPELINE_PENDING);

    /* check if this is not, by any chance, the last fragment */
    if (ucs_likely(iov.buffer < iov_buffer_limit)) {
        /* send every fragment but the last */
        do {
            status = ep_am_zcopy(ep, am_id, &step->am_header,
                                 sizeof(step->am_header), &iov,
                                 1, 0, &zcomp->comp);
            (zcomp++)->req = req;

            if (is_single_send) {
                return status;
            }

            step->am_header.remote_offset += frag_size;
            iov.buffer = (void*)((int8_t*)iov.buffer + frag_size);
        } while ((status == UCS_INPROGRESS) && (iov.buffer < iov_buffer_limit));

        if (ucs_unlikely(status != UCS_INPROGRESS)) {
            step->iter_offset = (int8_t*)iov.buffer - sbuf - frag_size;
            return status;
        }
    }

    /* Send last fragment of the message */
    zcomp->req = req;
    iov.length = sbuf + step->buffer_length - (int8_t*)iov.buffer;
    status     = ep_am_zcopy(ep, am_id, &step->am_header,
                             sizeof(step->am_header),
                             &iov, 1, 0, &zcomp->comp);
    if (ucs_unlikely(status != UCS_INPROGRESS)) {
        step->iter_offset = (int8_t*)iov.buffer - sbuf;
        return status;
    }

    step->am_header.remote_offset = 0;
    step->iter_offset = 0;
    return UCS_OK;
}

/*
 * Below is a set of macros, generating most bit-field combinations of
 * step->flags in the switch-case inside @ref ucg_builtin_step_execute() .
 */

#define case_send_calc(_is_rbuf, step, ep_cnt)                 \
{                                                              \
    size_t calc_offset = (base_offset + (item_interval *       \
                          (send_count - step->iter_calc)) %    \
                           (step->buffer_length * ep_cnt));    \
    if (_is_rbuf) {                                            \
        step->recv_buffer = base_buffer + calc_offset;         \
    } else {                                                   \
        step->send_buffer = base_buffer + calc_offset;         \
    }                                                          \
    --step->iter_calc;                                         \
}

#define case_send_full(/* General parameters */                                \
                       req, ureq, step, phase,                                 \
                       /* Receive-related indicators, for non-send-only steps*/\
                       _is_recv, _is_rs1, _is_r1s, _is_rbuf, _is_pipelined,    \
                       /* Step-completion-related indicators */                \
                       _is_first, _is_last, _is_one_ep,                        \
                       /* Send-related  parameters */                          \
                       _is_calc, _send_flag, _send_func)                       \
   case ((_is_calc      ? UCG_BUILTIN_OP_STEP_FLAG_CALC_SENT_BUFFERS  : 0) |   \
         (_is_one_ep    ? UCG_BUILTIN_OP_STEP_FLAG_SINGLE_ENDPOINT    : 0) |   \
         (_is_last      ? UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP          : 0) |   \
         (_is_first     ? UCG_BUILTIN_OP_STEP_FLAG_FIRST_STEP         : 0) |   \
         (_is_pipelined ? UCG_BUILTIN_OP_STEP_FLAG_PIPELINED          : 0) |   \
         (_is_rbuf      ? UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF : 0) |   \
         (_is_r1s       ? UCG_BUILTIN_OP_STEP_FLAG_RECV1_BEFORE_SEND  : 0) |   \
         (_is_rs1       ? UCG_BUILTIN_OP_STEP_FLAG_RECV_BEFORE_SEND1  : 0) |   \
         (_is_recv      ? UCG_BUILTIN_OP_STEP_FLAG_RECV_AFTER_SEND    : 0) |   \
         _send_flag):                                                          \
                                                                               \
        is_zcopy = (_send_flag) & UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY;      \
        if ((_is_rs1 || _is_r1s) && (step->iter_ep == 0)) {                    \
            uint32_t new_cnt = step->iter_ep = _is_r1s ? 1 : phase->ep_cnt - 1;\
            if (_is_pipelined) {                                               \
               memset((void*)step->fragment_pending, new_cnt, step->fragments);\
            }                                                                  \
            if (!is_zcopy) {                                                   \
                req->pending = new_cnt * step->fragments;                      \
            } /* Otherwise default init of ep_cnt*num_fragments is correct */  \
            break; /* Beyond the switch-case we fall-back to receiving */      \
        }                                                                      \
                                                                               \
        if (_is_recv && is_zcopy) {                                            \
            /* Both zcopy callbacks and incoming messages use pending, so ...*/\
            req->pending = 2 * step->fragments * phase->ep_cnt;                \
        }                                                                      \
                                                                               \
        if (_is_calc) {                                                        \
            ucs_assert(!_is_pipelined);                                        \
            ucs_assert(step->phase->ep_cnt <                                   \
                       UCS_BIT(sizeof(step->iter_calc) << 3));                 \
            step->calc_cb(req, &send_count, &base_offset, &item_interval);     \
            if (!step->iter_calc) {                                            \
                step->iter_calc = send_count;                                  \
            }                                                                  \
            if (_is_rbuf) {                                                    \
                base_buffer = step->recv_buffer;                               \
            } else {                                                           \
                base_buffer = step->send_buffer;                               \
            }                                                                  \
        }                                                                      \
                                                                               \
        /* Perform one or many send operations, unless an error occurs */      \
        if (_is_one_ep) {                                                      \
            ucs_assert(!_is_pipelined); /* makes no sense in single-ep case */ \
            do {                                                               \
                status = _send_func (req, step, phase->single_ep, 0);          \
                if (ucs_unlikely(UCS_STATUS_IS_ERR(status))) {                 \
                    goto step_execute_error;                                   \
                }                                                              \
                                                                               \
                if (_is_calc) {                                                \
                    case_send_calc(_is_rbuf, step, 1);                         \
                }                                                              \
            } while (_is_calc && step->iter_calc);                             \
        } else {                                                               \
            if ((_is_pipelined) && (ucs_unlikely(step->iter_offset ==          \
                    UCG_BUILTIN_OFFSET_PIPELINE_PENDING))) {                   \
                /* find a pending offset to progress */                        \
                unsigned frag_idx = 0;                                         \
                while ((frag_idx < step->fragments) &&                         \
                       (step->fragment_pending[frag_idx] ==                    \
                               UCG_BUILTIN_FRAG_PENDING)) {                    \
                    frag_idx++;                                                \
                }                                                              \
                ucs_assert(frag_idx < step->fragments);                        \
                step->iter_offset = frag_idx * step->fragment_length;          \
            }                                                                  \
                                                                               \
            uct_ep_h *ep_iter, *ep_last;                                       \
            ep_iter = ep_last = phase->multi_eps;                              \
            ep_iter += step->iter_ep;                                          \
            ep_last += phase->ep_cnt;                                          \
            do {                                                               \
                status = _send_func (req, step, *ep_iter, _is_pipelined);      \
                if (ucs_unlikely(UCS_STATUS_IS_ERR(status))) {                 \
                    /* Store the pointer, e.g. for UCS_ERR_NO_RESOURCE */      \
                    step->iter_ep = ep_iter - phase->multi_eps;                \
                    goto step_execute_error;                                   \
                }                                                              \
                                                                               \
                if (_is_calc) {                                                \
                    case_send_calc(_is_rbuf, step, phase->ep_cnt);             \
                }                                                              \
            } while (++ep_iter < ep_last);                                     \
                                                                               \
            if (_is_pipelined) {                                               \
                ucs_assert(!_is_calc);                                         \
                                                                               \
                /* Reset the iterator for the next pipelined incoming packet */\
                step->iter_ep = _is_r1s ? 1 : phase->ep_cnt - 1;               \
                ucs_assert(_is_r1s + _is_rs1 > 0);                             \
                                                                               \
                /* Check if this invocation is a result of a resend attempt */ \
                unsigned idx = step->iter_offset / step->fragment_length;      \
                if (ucs_unlikely(step->fragment_pending[idx] ==                \
                        UCG_BUILTIN_FRAG_PENDING)) {                           \
                    step->fragment_pending[idx] = 0;                           \
                                                                               \
                    /* Look for other packets in need of resending */          \
                    for (idx = 0; idx < step->fragments; idx++) {              \
                        if (step->fragment_pending[idx] ==                     \
                                UCG_BUILTIN_FRAG_PENDING) {                    \
                            /* Found such packets - mark for next resend */    \
                            step->iter_offset = idx * step->fragment_length;   \
                            status            = UCS_ERR_NO_RESOURCE;           \
                            goto step_execute_error;                           \
                        }                                                      \
                    }                                                          \
                } else {                                                       \
                    ucs_assert(step->fragment_pending[idx] == 0);              \
                }                                                              \
                step->iter_offset = UCG_BUILTIN_OFFSET_PIPELINE_READY;         \
            } else {                                                           \
                step->iter_ep = 0; /* Reset the per-step endpoint iterator */  \
                ucs_assert(step->iter_offset == 0);                            \
            }                                                                  \
        }                                                                      \
                                                                               \
        if (_is_calc) {                                                        \
            ucs_assert(step->iter_calc == 0);                                  \
                if (_is_rbuf) {                                                \
                    step->recv_buffer = base_buffer;                           \
                } else {                                                       \
                    step->send_buffer = base_buffer;                           \
                }                                                              \
        }                                                                      \
                                                                               \
        /* Potential completions (the operation may have finished by now) */   \
        if ((!_is_recv && !is_zcopy) || (req->pending == 0)) {                 \
            /* Nothing else to do - complete this step */                      \
            if (_is_last) {                                                    \
                if (!ureq) {                                                   \
                    ucg_builtin_comp_last_step_cb(req, UCS_OK);                \
                }                                                              \
                return UCS_OK;                                                 \
            } else {                                                           \
                return ucg_builtin_comp_step_cb(req, ureq);                    \
            }                                                                  \
        }                                                                      \
        break;

#define case_send_rs1(r, u, s, p,    _is_rs1, _is_r1s, _is_rbuf, _is_ppld, _is_first, _is_last, _is_one_ep, _is_calc, _send_flag, _send_func) \
       case_send_full(r, u, s, p, 0, _is_rs1, _is_r1s, _is_rbuf, _is_ppld, _is_first, _is_last, _is_one_ep, _is_calc, _send_flag, _send_func) \
       case_send_full(r, u, s, p, 1, _is_rs1, _is_r1s, _is_rbuf, _is_ppld, _is_first, _is_last, _is_one_ep, _is_calc, _send_flag, _send_func)

#define case_send_r1s(r, u, s, p,    _is_r1s, _is_rbuf, _is_ppld, _is_first, _is_last, _is_one_ep, _is_calc, _send_flag, _send_func) \
        case_send_rs1(r, u, s, p, 0, _is_r1s, _is_rbuf, _is_ppld, _is_first, _is_last, _is_one_ep, _is_calc, _send_flag, _send_func) \
        case_send_rs1(r, u, s, p, 1, _is_r1s, _is_rbuf, _is_ppld, _is_first, _is_last, _is_one_ep, _is_calc, _send_flag, _send_func)

#define case_send_rbuf(r, u, s, p,    _is_rbuf, _is_ppld, _is_first, _is_last, _is_one_ep, _is_calc, _send_flag, _send_func) \
         case_send_r1s(r, u, s, p, 0, _is_rbuf, _is_ppld, _is_first, _is_last, _is_one_ep, _is_calc, _send_flag, _send_func) \
         case_send_r1s(r, u, s, p, 1, _is_rbuf, _is_ppld, _is_first, _is_last, _is_one_ep, _is_calc, _send_flag, _send_func)

#define case_send_ppld(r, u, s, p,    _is_ppld, _is_first, _is_last, _is_one_ep, _is_calc, _send_flag, _send_func) \
        case_send_rbuf(r, u, s, p, 0, _is_ppld, _is_first, _is_last, _is_one_ep, _is_calc, _send_flag, _send_func) \
        case_send_rbuf(r, u, s, p, 1, _is_ppld, _is_first, _is_last, _is_one_ep, _is_calc, _send_flag, _send_func)

#define case_send_first(r, u, s, p,    _is_first, _is_last, _is_one_ep, _is_calc, _send_flag, _send_func) \
         case_send_ppld(r, u, s, p, 0, _is_first, _is_last, _is_one_ep, _is_calc, _send_flag, _send_func) \
         case_send_ppld(r, u, s, p, 1, _is_first, _is_last, _is_one_ep, _is_calc, _send_flag, _send_func)

#define  case_send_last(r, u, s, p,    _is_last,_is_one_ep, _is_calc, _send_flag, _send_func) \
        case_send_first(r, u, s, p, 0, _is_last,_is_one_ep, _is_calc, _send_flag, _send_func) \
        case_send_first(r, u, s, p, 1, _is_last,_is_one_ep, _is_calc, _send_flag, _send_func) \

#define case_send_one_ep(r, u, s, p,    _is_one_ep, _is_calc, _send_flag, _send_func) \
          case_send_last(r, u, s, p, 0, _is_one_ep, _is_calc, _send_flag, _send_func) \
          case_send_last(r, u, s, p, 1, _is_one_ep, _is_calc, _send_flag, _send_func)

#define case_send_scatter(r, u, s, p,    _is_calc, _send_flag, _send_func) \
         case_send_one_ep(r, u, s, p, 0, _is_calc, _send_flag, _send_func) \
         case_send_one_ep(r, u, s, p, 1, _is_calc, _send_flag, _send_func)

#define         case_send(r, u, s, p,    _send_flag, _send_func) \
        case_send_scatter(r, u, s, p, 0, _send_flag, _send_func) \
        case_send_scatter(r, u, s, p, 1, _send_flag, _send_func)

#define INIT_USER_REQUEST_IF_GIVEN(user_req, req) {                            \
    if (ucs_unlikely(user_req != NULL)) {                                      \
        /* Initialize user's request part (checked for completion) */          \
        if (*user_req) {                                                       \
            req->comp_req = *user_req - 1;                                     \
        } else {                                                               \
            req->comp_req = &req->super;                                       \
            *user_req     = &req->super + 1;                                   \
        }                                                                      \
        req->comp_req->flags = 0;                                              \
        user_req = NULL;                                                       \
    }                                                                          \
}
/*
 * Executing a single step is the heart of the Builtin planner.
 * This function advances to the next step (some invocations negate that...),
 * sends and then recieves according to the instructions of this step.
 * The function returns the status, typically one of the following:
 * > UCS_OK - collective operation (not just this step) has been completed.
 * > UCS_INPROGRESS - sends complete, waiting on some messages to be recieved.
 * > otherwise - an error has occurred.
 *
 * For example, a "complex" case is when the message is fragmented, and requires
 * both recieveing and sending in a single step, like in REDUCE_WAYPOINT. The
 * first call, coming from @ref ucg_builtin_op_trigger() , will enter the first
 * branch ("step_ep" is zero when a new step is starting), will process some
 * potential incoming messages (arriving beforehand) - returning UCS_INPROGRESS.
 * Subsequent calls to "progress()" will handle the rest of the incoming
 * messages for this step, and eventually call this function again from within
 * @ref ucg_builtin_comp_step_cb() . This call will choose the second branch,
 * the swith-case, which will send the message and
 */
UCS_PROFILE_FUNC(ucs_status_t, ucg_builtin_step_execute, (req, user_req),
                 ucg_builtin_request_t *req, ucg_request_t **user_req)
{
    int is_zcopy;
    int8_t *base_buffer;
    uint16_t local_id;
    uint8_t send_count;
    size_t base_offset;
    size_t item_interval;
    ucs_status_t status;

    ucg_builtin_op_step_t *step     = req->step;
    ucg_builtin_plan_phase_t *phase = step->phase;
    ucg_builtin_comp_slot_t *slot   = ucs_container_of(req, ucg_builtin_comp_slot_t, req);
    step->am_header.coll_id         = slot->coll_id;
    ucs_assert(slot->step_idx == step->am_header.step_idx);

    /* This step either starts by sending or contains no send operations */
    switch (step->flags) {
    /* Single-send operations (only one fragment passed to UCT) */
    case_send(req, user_req, step, phase, 0, /* for recv-only steps */
              ucg_builtin_step_dummy_send);
    case_send(req, user_req, step, phase, UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT,
              ucg_builtin_step_am_short_one);
    case_send(req, user_req, step, phase, UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SLOCK,
              ucg_builtin_step_am_slock_one);
    case_send(req, user_req, step, phase, UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY,
              ucg_builtin_step_am_bcopy_one);
    case_send(req, user_req, step, phase, UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BLOCK,
              ucg_builtin_step_am_block_one);
    case_send(req, user_req, step, phase, UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY,
              ucg_builtin_step_am_zcopy_one);

    /* Multi-send operations (using iter_ep and iter_offset for context) */
    case_send(req, user_req, step, phase, UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED,
              ucg_builtin_step_dummy_send);
    case_send(req, user_req, step, phase, UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED|
                                          UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT,
              ucg_builtin_step_am_short_max);
    case_send(req, user_req, step, phase, UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED|
                                          UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SLOCK,
              ucg_builtin_step_am_slock_max);
    case_send(req, user_req, step, phase, UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED|
                                          UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY,
              ucg_builtin_step_am_bcopy_max);
    case_send(req, user_req, step, phase, UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED|
                                          UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BLOCK,
              ucg_builtin_step_am_block_max);
    case_send(req, user_req, step, phase, UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED|
                                          UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY,
              ucg_builtin_step_am_zcopy_max);

    default:
        ucs_error("Invalid method for a collective operation step.");
        status = UCS_ERR_INVALID_PARAM;
        goto step_execute_error;
    }

    /* Initialize the users' request object, if applicable */
    INIT_USER_REQUEST_IF_GIVEN(user_req, req);
    slot->cb = step->recv_cb;

    /* Check pending incoming messages - invoke the callback on each one */
    if (ucs_likely(ucs_list_is_empty(&slot->msg_head))) {
        return UCS_INPROGRESS;
    }

    /* Look for matches in list of packets waiting on this slot */
    local_id = slot->local_id;
    ucg_builtin_comp_desc_t *desc, *iter;
    ucs_list_for_each_safe(desc, iter, &slot->msg_head, super.tag_list[0]) {
        /*
         * Note: stored message coll_id can be either larger or smaller than
         * the one currently handled - due to coll_id wrap-around.
         */
        ucs_assert((desc->header.coll_id  != slot->coll_id) ||
                   (desc->header.step_idx >= slot->step_idx));

        if (ucs_likely(desc->header.local_id == local_id)) {
            /* Remove the packet (next call may lead here recursively) */
            ucs_list_del(&desc->super.tag_list[0]);

            /* Handle this "waiting" packet, possibly completing the step */
            int is_step_done, is_batch = desc->super.flags & UCT_CB_PARAM_FLAG_BATCH;
            char *desc_data = &desc->data[0];
            size_t desc_len = desc->super.length;
            do {
                is_step_done = step->recv_cb(&slot->req,
                        desc->header.remote_offset, desc_data, desc_len);
                desc_data += desc_len;
                // TODO: consider incrementing offset too in some cases
            } while (is_batch && !is_step_done);

            /* Dispose of the packet, according to its allocation */
            if (desc->super.flags == UCT_CB_PARAM_FLAG_DESC) {
                uct_iface_release_desc(desc);
            } else {
                ucs_mpool_put_inline(desc);
            }

            /* If the step has indeed completed - check the entire op */
            if (is_step_done) {
                return (req->comp_req->flags & UCP_REQUEST_FLAG_COMPLETED) ?
                        req->comp_req->status : UCS_INPROGRESS;
            }
        }
    }

    return UCS_INPROGRESS;

    /************************** Error flows ***********************************/
step_execute_error:
    if (status == UCS_ERR_NO_RESOURCE) {
        /* Special case: send incomplete - enqueue for resend upon progress */
        INIT_USER_REQUEST_IF_GIVEN(user_req, req);

        if (step->flags & UCG_BUILTIN_OP_STEP_FLAG_PIPELINED) {
            step->fragment_pending[step->iter_offset / step->fragment_length] =
                    UCG_BUILTIN_FRAG_PENDING;
            step->iter_offset = UCG_BUILTIN_OFFSET_PIPELINE_PENDING;
        }

        ucs_list_add_tail(req->op->resend, &req->send_list);
        return UCS_INPROGRESS;
    }

    /* Generic error - reset the collective and mark the request as completed */
    ucg_builtin_comp_last_step_cb(req, status);
    return status;
}

BEGIN_C_DECLS

void ucg_builtin_op_initialize() {
#if HAVE_OMPI_SRC
    UCS_VECTOR_INIT(builtin, UCG_FRAGMENT_SIZE)
#endif
}

END_C_DECLS
