/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <string.h>
#include <ucs/debug/log.h>
#include <ucs/debug/memtrack.h>
#include <uct/api/uct_def.h>
#include <ucg/api/ucg_mpi.h>

#include "builtin_plan.h"

ucs_status_t ucg_builtin_bruck_create(ucg_builtin_group_ctx_t *ctx,
        enum ucg_builtin_plan_topology_type plan_topo_type,
        const ucg_builtin_config_t *config,
        const ucg_group_params_t *group_params,
        const ucg_collective_type_t *coll_type,
        ucg_builtin_plan_t **plan_p)
{
    /* Choose between Alltoall and Allgather */
    int is_allgather =
            (coll_type->modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST);
    enum ucg_builtin_plan_method_type phase_method = is_allgather ?
            UCG_PLAN_METHOD_ALLGATHER_BRUCK : UCG_PLAN_METHOD_ALLTOALL_BRUCK;

    /* Calculate the number of bruck steps */
    unsigned proc_count = group_params->member_count;
    ucg_step_idx_t step_idx = 0;
    unsigned step_size = 1;

    while (step_size < proc_count) {
        step_size <<= 1;
        step_idx++; /* step_idx set to number of steps here */
    }

    /* Allocate memory resources */
    size_t alloc_size = sizeof(ucg_builtin_plan_t) + (step_idx *
            sizeof(ucg_builtin_plan_phase_t));

    ucg_builtin_plan_t *bruck       = (ucg_builtin_plan_t*)UCS_ALLOC_CHECK(alloc_size, "bruck topology");
    ucg_builtin_plan_phase_t *phase = &bruck->phss[0];
    bruck->phs_cnt                  = step_idx;
    bruck->ep_cnt                   = 0;

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
    for (step_idx = 0, step_size = 1;
         step_idx < bruck->phs_cnt;
         step_idx++, phase++, step_size <<= 1)
    {
        ucg_group_member_index_t peer_index =
                (my_index - step_size + proc_count) % proc_count;
        ucs_status_t status = ucg_builtin_single_connection_phase(ctx,
                peer_index, step_idx, phase_method, phase);
        if (status != UCS_OK)  {
            return status;
        }
    }

    bruck->super.my_index = my_index;
    *plan_p = bruck;
    return UCS_OK;
}
