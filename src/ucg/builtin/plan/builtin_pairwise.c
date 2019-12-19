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
    ucs_status_t status = UCS_OK;

    /* Calculate the number of pairwise steps */
    unsigned proc_count = group_params->member_count;
    ucg_step_idx_t nSteps = proc_count;

    /* Allocate memory resources */
    size_t alloc_size =          sizeof(ucg_builtin_plan_t) +
                       			(sizeof(ucg_builtin_plan_phase_t)
                   			  + (nSteps * sizeof(uct_ep_h)));/* every phase has no more than two endpoints! */

    ucg_builtin_plan_t *pairwise    = (ucg_builtin_plan_t*)UCS_ALLOC_CHECK(alloc_size, "pairwise topology");
    ucg_builtin_plan_phase_t *phase = &pairwise->phss[0];
    uct_ep_h *next_ep               = (uct_ep_h*)(phase + nSteps);
    pairwise->ep_cnt                = nSteps;
    pairwise->phs_cnt               = 1;

    /* Find my own index (rank) */
    /* Alex: use group_params' member to get my_index */
    ucg_group_member_index_t my_index = 0;
    while ((my_index < proc_count) && (group_params->distance[my_index] != UCG_GROUP_MEMBER_DISTANCE_SELF)) {
        my_index++;
    }

    if (my_index == proc_count) {
        ucs_error("No member with distance==UCP_GROUP_MEMBER_DISTANCE_SELF found");
        return UCS_ERR_INVALID_PARAM;
    }

    /* Calculate the peers for each step */
    phase->method = UCG_PLAN_METHOD_SCATTER_TERMINAL;

    for(int step_idx = 0; step_idx < (int) nSteps-1; step_idx++){
    	phase->step_index = step_idx;

    	ucg_group_member_index_t peer_index_dst = (my_index + (step_idx+1) ) % proc_count;

        /* Connect to receiver for second EP */
        status = ucg_builtin_connect( ctx, peer_index_src, phase, phase_ep_index, 0 );
        if( status != UCS_OK ){
        	return status;
        }
    }

    pairwise->super.my_index = my_index;
    *plan_p = pairwise;

	return status;
}






























