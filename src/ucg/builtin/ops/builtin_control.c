/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <stddef.h>
#include <ucs/sys/compiler_def.h>

#include "builtin_ops.h"

#ifndef MPI_IN_PLACE
#define MPI_IN_PLACE ((void*)0x1)
#endif

static UCS_F_ALWAYS_INLINE ucs_status_t
ucg_builtin_step_send_flags(ucg_builtin_op_step_t *step,
                            ucg_builtin_plan_phase_t *phase,
                            const ucg_collective_params_t *params,
                            enum ucg_builtin_op_step_flags *send_flag)
{
    size_t length    = step->buffer_length;
    size_t dt_len    = params->send.dt_len;
    size_t batch_len = phase->sm_cnt *
            ucs_align_up(length + sizeof(ucg_builtin_header_t),
                         UCS_SYS_CACHE_LINE_SIZE);

    /*
     * Short messages (e.g. RDMA "inline")
     */
    if (ucs_likely(length <= phase->max_short_one)) {
        /* Short send - single message */
        *send_flag            = ((phase->flags & UCG_PLAN_FLAG_NEEDS_LOCKING) &&
                                 (batch_len > phase->max_short_one) &&
                                 ((phase->method == UCG_PLAN_METHOD_REDUCE_TERMINAL) ||
                                  (phase->method == UCG_PLAN_METHOD_REDUCE_WAYPOINT))) ?
                                UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SLOCK:
                                UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT;
        step->fragments       = 1;
    } else if (ucs_likely(length <= phase->max_short_max)) {
        /* Short send - multiple messages */
        if (phase->flags & UCG_PLAN_FLAG_NEEDS_LOCKING) {
            *send_flag        = (enum ucg_builtin_op_step_flags)
                                (UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SLOCK |
                                 UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED);
        } else {
            *send_flag        = (enum ucg_builtin_op_step_flags)
                                (UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT |
                                 UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED);
        }
        step->fragment_length = phase->max_short_one -
                               (phase->max_short_one % dt_len);
        step->fragments       = length / step->fragment_length +
                              ((length % step->fragment_length) > 0);

    /*
     * Large messages, if supported (e.g. RDMA "zero-copy")
     */
    } else if (ucs_unlikely((length >  phase->max_bcopy_max) &&
                            (phase->md_attr->cap.max_reg))) {
        if (ucs_likely(length < phase->max_zcopy_one)) {
            /* ZCopy send - single message */
            *send_flag            = UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY;
            step->fragments       = 1;
        } else {
            /* ZCopy send - single message */
            *send_flag            = (enum ucg_builtin_op_step_flags)
                                    (UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY |
                                     UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED);
            step->fragment_length = phase->max_zcopy_one -
                                   (phase->max_zcopy_one % dt_len);
            step->fragments       = length / step->fragment_length +
                                  ((length % step->fragment_length) > 0);
        }
    /*
     * Medium messages
     */
    } else if (ucs_likely(length <= phase->max_bcopy_one)) {
        /* BCopy send - single message */
        *send_flag            = ((phase->flags & UCG_PLAN_FLAG_NEEDS_LOCKING) &&
                                 (batch_len > phase->max_bcopy_one) &&
                                 ((phase->method == UCG_PLAN_METHOD_REDUCE_TERMINAL) ||
                                 (phase->method == UCG_PLAN_METHOD_REDUCE_WAYPOINT))) ?
                                UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BLOCK:
                                UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY;
        step->fragment_length = step->buffer_length;
        step->fragments       = 1;
    } else {
        /* BCopy send - multiple messages */
        if (phase->flags & UCG_PLAN_FLAG_NEEDS_LOCKING) {
            *send_flag        = (enum ucg_builtin_op_step_flags)
                                (UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BLOCK |
                                 UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED);
        } else {
            *send_flag        = (enum ucg_builtin_op_step_flags)
                                (UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY |
                                 UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED);
        }
        step->fragment_length = phase->max_bcopy_one -
                               (phase->max_bcopy_one % dt_len);
        step->fragments       = length / step->fragment_length +
                              ((length % step->fragment_length) > 0);
    }

    return UCS_OK;
}

ucs_status_t ucg_builtin_step_create(ucg_builtin_plan_phase_t *phase,
                                     unsigned extra_flags,
                                     unsigned base_am_id,
                                     ucg_group_id_t group_id,
                                     const ucg_collective_params_t *params,
                                     int8_t **current_data_buffer,
                                     ucg_builtin_op_step_t *step)
{
    /* Set the parameters determining the send-flags later on */
    step->buffer_length      = params->send.dt_len * params->send.count;
    step->uct_md             = phase->md;
    if (phase->md) {
        step->uct_iface      = (phase->ep_cnt == 1) ? phase->single_ep->iface :
                                                      phase->multi_eps[0]->iface;
    }
    /* Note: we assume all the UCT endpoints have the same interface */
    step->phase              = phase;
    step->am_id              = base_am_id;
    step->am_header.group_id = group_id;
    step->am_header.step_idx = phase->step_index;
    step->iter_ep            = 0;
    step->iter_offset        = 0;
    step->fragment_pending   = NULL;
    step->recv_buffer        = (int8_t*)params->recv.buf;
    step->send_buffer        = ((params->send.buf == MPI_IN_PLACE) ||
            !(extra_flags & UCG_BUILTIN_OP_STEP_FLAG_FIRST_STEP)) ?
                    (int8_t*)params->recv.buf : (int8_t*)params->send.buf;
    if (*current_data_buffer) {
        step->send_buffer = *current_data_buffer;
    } else {
        *current_data_buffer = step->recv_buffer;
    }
    ucs_assert(base_am_id < UCP_AM_ID_MAX);

    /* Decide how the messages are sent (regardless of my role) */
    enum ucg_builtin_op_step_flags send_flag;
    ucs_status_t status = ucg_builtin_step_send_flags(step, phase, params, &send_flag);
    extra_flags |= (send_flag & UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED);
    if (ucs_unlikely(status != UCS_OK)) {
        return status;
    }

    /* Set the actual step-related parameters */
    switch (phase->method) {
    /* Send-only */
    case UCG_PLAN_METHOD_SCATTER_TERMINAL:
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_LENGTH_PER_REQUEST;
        /* no break */
    case UCG_PLAN_METHOD_SEND_TERMINAL:
        step->flags       = send_flag | extra_flags;
        break;

    /* Recv-only */
    case UCG_PLAN_METHOD_RECV_TERMINAL:
    case UCG_PLAN_METHOD_REDUCE_TERMINAL:
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_RECV_AFTER_SEND;
        step->flags       = extra_flags;
        break;

    /* Recv-all, Send-one */
    case UCG_PLAN_METHOD_GATHER_WAYPOINT:
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_LENGTH_PER_REQUEST;
        /* no break */
    case UCG_PLAN_METHOD_REDUCE_WAYPOINT:
        if (send_flag & UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED) {
            extra_flags  |= UCG_BUILTIN_OP_STEP_FLAG_PIPELINED;
        }
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_RECV_BEFORE_SEND1;
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF;
        step->flags       = send_flag | extra_flags;
        step->recv_buffer = *current_data_buffer =
                (int8_t*)ucs_calloc(1, step->buffer_length, "ucg_fanin_waypoint_buffer");
        if (!step->recv_buffer) return UCS_ERR_NO_MEMORY;
        // TODO: memory registration, and de-registration at some point...
        break;

    /* Recv-one, Send-all */
    case UCG_PLAN_METHOD_BCAST_WAYPOINT:
        if (send_flag & UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED) {
            extra_flags  |= UCG_BUILTIN_OP_STEP_FLAG_PIPELINED;
        }
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_RECV1_BEFORE_SEND;
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF;
        step->flags       = send_flag | extra_flags;
        break;

    case UCG_PLAN_METHOD_SCATTER_WAYPOINT:
        if (send_flag & UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED) {
            extra_flags  |= UCG_BUILTIN_OP_STEP_FLAG_PIPELINED;
        }
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_RECV1_BEFORE_SEND;
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_LENGTH_PER_REQUEST;
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF;
        step->flags       = send_flag | extra_flags;
        step->recv_buffer = *current_data_buffer =
                (int8_t*)ucs_calloc(1, step->buffer_length, "ucg_fanout_waypoint_buffer");
        if (!step->recv_buffer) return UCS_ERR_NO_MEMORY;
        // TODO: memory registration, and de-registration at some point...
        break;

    /* Recursive patterns */
    case UCG_PLAN_METHOD_REDUCE_RECURSIVE:
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_RECV_AFTER_SEND;
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF;
        step->flags       = send_flag | extra_flags;
        break;

    default:
        ucs_error("Invalid method for a collective operation.");
        return UCS_ERR_INVALID_PARAM;
    }

    /* fill in additional data before finishing this step */
    if (phase->ep_cnt == 1) {
        step->flags |= UCG_BUILTIN_OP_STEP_FLAG_SINGLE_ENDPOINT;
    }
    if (step->flags & send_flag) {
        step->am_header.remote_offset = 0;
    }

    /* memory registration (using the memory registration cache) */
    if (send_flag & UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY) {
        ucs_status_t status = ucg_builtin_step_zcopy_prep(step);
        if (ucs_unlikely(status != UCS_OK)) {
            return status;
        }
    }

    /* Pipelining preparation */
    if (step->flags & UCG_BUILTIN_OP_STEP_FLAG_PIPELINED) {
        step->fragment_pending = (uint8_t*)UCS_ALLOC_CHECK(sizeof(phase->ep_cnt),
                                                           "ucg_builtin_step_pipelining");
    }

    /* Select the right completion callback */
    return ucg_builtin_step_select_callbacks(phase, &step->recv_cb,
            params->send.count > 0, step->flags);
}

ucs_status_t ucg_builtin_op_create(ucg_plan_t *plan,
                                   const ucg_collective_params_t *params,
                                   ucg_op_t **new_op)
{
    ucs_status_t status;
    ucg_builtin_plan_t *builtin_plan     = (ucg_builtin_plan_t*)plan;
    ucg_builtin_plan_phase_t *next_phase = &builtin_plan->phss[0];
    unsigned phase_count                 = builtin_plan->phs_cnt;

    /* Check for non-zero-root trees */
    if (ucs_unlikely(params->type.root != 0)) {
        /* Assume the plan is tree-based, since Recursive K-ing has no root */
        status = ucg_builtin_topo_tree_set_root(params->type.root,
                plan->my_index, builtin_plan, &next_phase, &phase_count);
        if (ucs_unlikely(status != UCS_OK)) {
            return status;
        }
    }

    ucg_builtin_op_t *op                 = (ucg_builtin_op_t*)
            ucs_mpool_get_inline(&builtin_plan->op_mp);
    ucg_builtin_op_step_t *next_step     = &op->steps[0];
    unsigned am_id                       = builtin_plan->am_id;
    int8_t *current_data_buffer          = NULL;

    /* Select the right initialization callback */
    status = ucg_builtin_op_select_callback(builtin_plan, &op->init_cb);
    if (status != UCS_OK) {
        goto op_cleanup;
    }

    /* Create a step in the op for each phase in the topology */
    if (phase_count == 1) {
        /* The only step in the plan */
        status = ucg_builtin_step_create(next_phase,
                UCG_BUILTIN_OP_STEP_FLAG_FIRST_STEP |
                UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP,
                am_id, plan->group_id, params,
                &current_data_buffer, next_step);
    } else {
        /* First step of many */
        status = ucg_builtin_step_create(next_phase,
                UCG_BUILTIN_OP_STEP_FLAG_FIRST_STEP, am_id, plan->group_id,
                params, &current_data_buffer, next_step);
        if (ucs_unlikely(status != UCS_OK)) {
            goto op_cleanup;
        }

        ucg_step_idx_t step_cnt;
        for (step_cnt = 1; step_cnt < phase_count - 1; step_cnt++) {
            status = ucg_builtin_step_create(++next_phase, 0, am_id,
                    plan->group_id, params, &current_data_buffer, ++next_step);
            if (ucs_unlikely(status != UCS_OK)) {
                goto op_cleanup;
            }
        }

        /* Last step gets a special flag */
        status = ucg_builtin_step_create(++next_phase,
                UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP, am_id, plan->group_id,
                params, &current_data_buffer, ++next_step);
    }
    if (ucs_unlikely(status != UCS_OK)) {
        goto op_cleanup;
    }

    /* Select the right optimization callback */
    status = ucg_builtin_op_consider_optimization(op,
            (ucg_builtin_config_t*)plan->planner->plan_config);
    if (status != UCS_OK) {
        goto op_cleanup;
    }

    UCS_STATIC_ASSERT(sizeof(ucg_builtin_header_t) <= UCP_WORKER_HEADROOM_PRIV_SIZE);
    UCS_STATIC_ASSERT(sizeof(ucg_builtin_header_t) == sizeof(uint64_t));

    op->slots  = (ucg_builtin_comp_slot_t*)builtin_plan->slots;
    op->resend = builtin_plan->resend;
    *new_op    = &op->super;
    return UCS_OK;

op_cleanup:
    ucs_mpool_put_inline(op);
    return status;
}


void ucg_builtin_op_discard(ucg_op_t *op)
{
    ucg_builtin_op_t *builtin_op = (ucg_builtin_op_t*)op;
    ucg_builtin_op_step_t *step = &builtin_op->steps[0];
    do {
        if (step->flags & UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY) {
            uct_md_mem_dereg(step->uct_md, step->zcopy.memh);
            ucs_free(step->zcopy.zcomp);
        }

        if (step->flags & UCG_BUILTIN_OP_STEP_FLAG_PIPELINED) {
            ucs_free((void*)step->fragment_pending);
        }
    } while (!((step++)->flags & UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP));

    ucs_mpool_put_inline(op);
}

ucs_status_t ucg_builtin_op_trigger(ucg_op_t *op, ucg_coll_id_t coll_id, ucg_request_t **request)
{
    /* Allocate a "slot" for this operation, from a per-group array of slots */
    ucg_builtin_op_t *builtin_op  = (ucg_builtin_op_t*)op;
    ucg_builtin_comp_slot_t *slot = &builtin_op->slots[coll_id % UCG_BUILTIN_MAX_CONCURRENT_OPS];
    slot->coll_id                 = coll_id;
    if (ucs_unlikely(slot->cb != NULL)) {
        ucs_error("UCG Builtin planner exceeded the max concurrent collectives.");
        return UCS_ERR_NO_RESOURCE;
    }

    /* Initialize the request structure, located inside the selected slot s*/
    ucg_builtin_request_t *builtin_req = &slot->req;
    builtin_req->op                    = builtin_op;
    ucg_builtin_op_step_t *first_step  = builtin_op->steps;
    builtin_req->step                  = first_step;
    builtin_req->pending               = first_step->fragments *
                                         first_step->phase->ep_cnt;
    slot->step_idx                     = first_step->am_header.step_idx;

    /* Sanity checks */
    ucs_assert(first_step->iter_offset == 0);
    ucs_assert(first_step->iter_ep == 0);
    ucs_assert(request != NULL);

    /*
     * For some operations, like MPI_Reduce, MPI_Allreduce or MPI_Gather, the
     * local data has to be aggregated along with the incoming data. In others,
     * some shuffle is required once before starting (e.g. Bruck algorithms).
     */
    builtin_op->init_cb(builtin_op);

    /* Consider optimization, if this operation is used often enough */
    if (ucs_unlikely(--builtin_op->opt_cnt == 0)) {
        ucs_status_t optm_status = builtin_op->optm_cb(builtin_op);
        if (ucs_unlikely(UCS_STATUS_IS_ERR(optm_status))) {
            return optm_status;
        }
        /* Need to return original status, becuase it can be OK or INPROGRESS */
    }

    /* Start the first step, which may actually complete the entire operation */
    return ucg_builtin_step_execute(builtin_req, request);
}
