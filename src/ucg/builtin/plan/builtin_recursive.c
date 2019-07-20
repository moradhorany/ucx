/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <string.h>
#include <ucs/debug/log.h>
#include <ucs/debug/memtrack.h>
#include <uct/api/uct_def.h>

#include "builtin_plan.h"

ucs_config_field_t ucg_builtin_recursive_config_table[] = {
    {"FACTOR", "2", "Recursive factor",
     ucs_offsetof(ucg_builtin_recursive_config_t, factor), UCS_CONFIG_TYPE_UINT},

    {NULL}
};

ucs_status_t ucg_builtin_recursive_create(ucg_builtin_group_ctx_t *ctx,
        enum ucg_builtin_plan_topology_type plan_topo_type,
        const ucg_builtin_config_t *config,
        const ucg_group_params_t *group_params,
        const ucg_collective_type_t *coll_type,
        ucg_builtin_plan_t **plan_p)
{
    /* Calculate the number of recursive steps */
    unsigned proc_count = group_params->member_count;
    unsigned factor = config->recursive.factor;
    ucg_step_idx_t step_idx = 0;
    unsigned step_size = 1;
    if (factor < 2) {
        ucs_error("Recursive K-ing factor must be at least 2 (given %u)", factor);
        return UCS_ERR_INVALID_PARAM;
    }
    while (step_size < proc_count) {
        step_size *= factor;
        step_idx++;
    }
    if (step_size != proc_count) {
        ucs_error("Recursive K-ing must have proc# a power of the factor (factor %u procs %u)", factor, proc_count);
        /* Currently only an exact power of the recursive factor is supported */
        return UCS_ERR_UNSUPPORTED;
    }

    /* Allocate memory resources */
    size_t alloc_size = sizeof(ucg_builtin_plan_t) +
            (step_idx * sizeof(ucg_builtin_plan_phase_t));
    if (factor != 2) {
        /* Allocate extra space for the map's multiple endpoints */
        alloc_size += step_idx * (factor - 1) * sizeof(uct_ep_h);
    }
    ucg_builtin_plan_t *recursive   = (ucg_builtin_plan_t*)UCS_ALLOC_CHECK(alloc_size, "recursive topology");
    ucg_builtin_plan_phase_t *phase = &recursive->phss[0];
    uct_ep_h *next_ep               = (uct_ep_h*)(phase + step_idx);
    recursive->ep_cnt               = (factor == 2) ? 0 : step_idx * (factor - 1);
    recursive->phs_cnt              = step_idx;

    /* Find my own index */
    ucg_group_member_index_t my_index = 0;
    while ((my_index < proc_count) &&
           (group_params->distance[my_index] !=
                   UCG_GROUP_MEMBER_DISTANCE_SELF)) {
        my_index++;
    }

    if (my_index == proc_count) {
        ucs_error("No member with distance==UCP_GROUP_MEMBER_DISTANCE_SELF found");
        return UCS_ERR_INVALID_PARAM;
    }

    /* Calculate the peers for each step */
    ucs_status_t status = UCS_OK;
    for (step_idx = 0, step_size = 1;
         ((step_idx < recursive->phs_cnt) && (status == UCS_OK));
         step_idx++, phase++, step_size *= factor) {
        unsigned step_base = my_index - (my_index % (step_size * factor));
        phase->method      = UCG_PLAN_METHOD_REDUCE_RECURSIVE;
        phase->ep_cnt      = factor - 1;
        phase->step_index  = step_idx;

#if ENABLE_DEBUG_DATA || ENABLE_FAULT_TOLERANCE
        phase->indexes     = UCS_ALLOC_CHECK((factor - 1) * sizeof(my_index),
                                             "recursive topology indexes");
#endif

        /* In each step, there are one or more peers */
        unsigned step_peer_idx;
        for (step_peer_idx = 1; ((step_peer_idx < factor) && (status == UCS_OK)); step_peer_idx++) {
            ucg_group_member_index_t peer_index = step_base +
                    ((my_index - step_base + step_size * step_peer_idx) %
                     (step_size * factor));
            ucs_info("%lu's peer #%u/%u (step #%u/%u): %lu ", my_index, step_peer_idx,
                    factor - 1, step_idx + 1, recursive->phs_cnt, peer_index);
            phase->multi_eps = next_ep++;
            status = ucg_builtin_connect(ctx, peer_index, phase, (factor != 2) ?
                    (step_peer_idx - 1) : UCG_BUILTIN_CONNECT_SINGLE_EP);
        }
    }

    recursive->super.my_index = my_index;
    *plan_p = recursive;
    return status;
}
