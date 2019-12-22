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

ucs_status_t ucg_builtin_pairwise_create(ucg_builtin_group_ctx_t *ctx,
        enum ucg_builtin_plan_topology_type plan_topo_type,
        const ucg_builtin_config_t *config,
        const ucg_group_params_t *group_params,
        const ucg_collective_type_t *coll_type,
        ucg_builtin_plan_t **plan_p)
{
	ucg_step_idx_t step_idx = 1; /* Shuki: TODO - Fix this */
    /* Calculate the number of pairwise steps */
    ucg_group_member_index_t proc_idx, proc_count = group_params->member_count;

    /* Allocate memory resources */
    size_t alloc_size = sizeof(ucg_builtin_plan_t) +
                        sizeof(ucg_builtin_plan_phase_t) +
                        ((proc_count - 1) * sizeof(uct_ep_h));

    ucg_builtin_plan_t *pairwise    = (ucg_builtin_plan_t*)UCS_ALLOC_CHECK(alloc_size, "pairwise topology");
    ucg_builtin_plan_phase_t *phase = &pairwise->phss[0];
    pairwise->ep_cnt                = proc_count;
    pairwise->phs_cnt               = 1;

    ucs_assert((ucg_group_member_index_t)((typeof(pairwise->ep_cnt))-1) > proc_count);

    /* Find my own index (rank) */
    ucg_group_member_index_t my_index = 0;
    while ((my_index < proc_count) && (group_params->distance[my_index] != UCG_GROUP_MEMBER_DISTANCE_SELF)) {
        my_index++;
    }

    if (my_index == proc_count) {
        ucs_error("No member with distance==UCP_GROUP_MEMBER_DISTANCE_SELF found");
        return UCS_ERR_INVALID_PARAM;
    }

    /* Calculate the peers for each step */
    phase->method = UCG_PLAN_METHOD_PAIRWISE;
    phase->step_index = step_idx;

    for(proc_idx = 1; proc_idx < proc_count; proc_idx++) {
        /* Connect to receiver for second EP */
        ucg_group_member_index_t next_peer = (my_index + proc_idx) % proc_count;
        ucs_status_t status = ucg_builtin_connect(ctx, next_peer, phase, proc_idx - 1, 0);
        if (status != UCS_OK) {
            return status;
        }
    }

    pairwise->super.my_index = my_index;
    *plan_p = pairwise;
    return UCS_OK;
}






























