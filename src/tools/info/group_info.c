/**
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */

#include "ucx_info.h"

#include <ucg/api/ucg_mpi.h>
#include <ucg/api/ucg_plan_component.h>
#include <ucg/api/ucg_mpi.h>
#include <ucs/debug/memtrack.h>

/* In accordance with @ref enum ucg_predefined */
const char *ucg_predefined_collective_names[] = {
    [UCG_PRIMITIVE_BARRIER]            = "barrier",
    [UCG_PRIMITIVE_REDUCE]             = "reduce",
    [UCG_PRIMITIVE_GATHER]             = "gather",
    [UCG_PRIMITIVE_GATHERV]            = "gatherv",
    [UCG_PRIMITIVE_BCAST]              = "bcast",
    [UCG_PRIMITIVE_SCATTER]            = "scatter",
    [UCG_PRIMITIVE_SCATTERV]           = "scatterv",
    [UCG_PRIMITIVE_ALLREDUCE]          = "allreduce",
    [UCG_PRIMITIVE_ALLTOALL]           = "alltoall",
    [UCG_PRIMITIVE_REDUCE_SCATTER]     = "reduce_scatter",
    [UCG_PRIMITIVE_ALLGATHER]          = "allgather",
    [UCG_PRIMITIVE_ALLGATHERV]         = "allgatherv",
    [UCG_PRIMITIVE_ALLTOALLW]          = "alltoallv",
    [UCG_PRIMITIVE_NEIGHBOR_ALLTOALLW] = "alltoallw"
};

#define EMPTY UCG_GROUP_MEMBER_DISTANCE_LAST

ucp_address_t *worker_address = 0;
int dummy_resolve_address(void *cb_group_obj,
                          ucg_group_member_index_t index,
                          ucp_address_t **addr, size_t *addr_len)
{
    *addr = worker_address;
    *addr_len = 0; /* special debug flow: replace uct_ep_t with member indexes */
    return 0;
}

void dummy_release_address(ucp_address_t *addr) { }

ucs_status_t gen_ucg_topology(ucg_group_member_index_t me,
        ucg_group_member_index_t peer_count[4],
        enum ucg_group_member_distance **distance_array_p,
        ucg_group_member_index_t *distance_array_length_p)
{
    printf("UCG Processes per socket:  %lu\n", peer_count[1]);
    printf("UCG Sockets per host:      %lu\n", peer_count[2]);
    printf("UCG Hosts in the network:  %lu\n", peer_count[3]);

    /* generate the array of distances in order to create a group */
    ucg_group_member_index_t member_count = 1;
    peer_count[0] = 1; /* not initialized by the user */
    unsigned distance_idx;
    for (distance_idx = 0; distance_idx < 4; distance_idx++) {
        if (peer_count[distance_idx]) {
            member_count *= peer_count[distance_idx];
            peer_count[distance_idx] = member_count;
        }
    }

    if (me >= member_count) {
        printf("<Error: index is %lu, out of %lu total>\n", me, member_count);
        return UCS_ERR_INVALID_PARAM;
    }

    /* create the distance array for group creation */
    printf("UCG Total member count:    %lu\n", member_count);
    enum ucg_group_member_distance *distance_array =
            ucs_malloc(member_count * sizeof(*distance_array), "distance array");
    if (!distance_array) {
        printf("<Error: failed to allocate the distance array>\n");
        return UCS_ERR_NO_MEMORY;
    }

    memset(distance_array, EMPTY, member_count * sizeof(*distance_array));
    enum ucg_group_member_distance distance = UCG_GROUP_MEMBER_DISTANCE_SELF;
    for (distance_idx = 0; distance_idx < 4; distance_idx++) {
        if (peer_count[distance_idx]) {
            unsigned array_idx, array_offset = me - (me % peer_count[distance_idx]);
            for (array_idx = 0; array_idx < peer_count[distance_idx]; array_idx++) {
                if (distance_array[array_idx + array_offset] == EMPTY) {
                    distance_array[array_idx + array_offset] = distance;
                }
            }
        }

        switch (distance) {
        case UCG_GROUP_MEMBER_DISTANCE_SELF:
            distance = UCG_GROUP_MEMBER_DISTANCE_SOCKET;
            break;

        case UCG_GROUP_MEMBER_DISTANCE_SOCKET:
            distance = UCG_GROUP_MEMBER_DISTANCE_HOST;
            break;

        case UCG_GROUP_MEMBER_DISTANCE_HOST:
            distance = UCG_GROUP_MEMBER_DISTANCE_NET;
            break;

        case UCG_GROUP_MEMBER_DISTANCE_NET:
            distance = UCG_GROUP_MEMBER_DISTANCE_LAST;
            break;

        default:
            break;
        }
    }

    *distance_array_length_p = member_count;
    *distance_array_p = distance_array;
    return UCS_OK;
}

void print_ucg_topology(const char *req_planner_name, ucp_worker_h worker,
        ucg_group_member_index_t root, ucg_group_member_index_t me,
        const char *collective_type_name, size_t dtype_count,
        enum ucg_group_member_distance *distance_array,
        ucg_group_member_index_t member_count, int is_verbose)
{
    ucs_status_t status;

    /* print the resulting distance array*/
    unsigned array_idx;
    printf("UCG Distance array for rank #%3lu [", me);
    for (array_idx = 0; array_idx < member_count; array_idx++) {
        switch (distance_array[array_idx]) {
        case UCG_GROUP_MEMBER_DISTANCE_SELF:
            printf("M");
            break;
        case UCG_GROUP_MEMBER_DISTANCE_SOCKET:
            printf(root == array_idx ? "S" : "s");
            break;
        case UCG_GROUP_MEMBER_DISTANCE_HOST:
            printf(root == array_idx ? "H" : "h");
            break;
        case UCG_GROUP_MEMBER_DISTANCE_NET:
            printf(root == array_idx ? "N" : "n");
            break;
        default:
            printf("<Failed to generate UCG distance array>\n");
            status = UCS_ERR_INVALID_PARAM;
            goto cleanup;
        }
    }
    printf("] (Capital letter for root and self)\n");
    if (!is_verbose) {
        return;
    }

    /* create a group with the generated paramters */
    ucg_group_h group;
    ucg_group_params_t group_params = {
            .field_mask        = UCG_GROUP_PARAM_FIELD_MEMBER_COUNT |
                                 UCG_GROUP_PARAM_FIELD_MEMBER_INDEX |
                                 UCG_GROUP_PARAM_FIELD_CB_CONTEXT   |
                                 UCG_GROUP_PARAM_FIELD_DISTANCES,
            .member_index      = me,
            .member_count      = member_count,
            .distance          = distance_array
    };
    size_t worker_address_length;
    status = ucp_worker_get_address(worker, &worker_address, &worker_address_length);
    if (status != UCS_OK) {
        goto cleanup;
    }

    status = ucg_group_create(worker, &group_params, &group);
    if (status != UCS_OK) {
        goto address_cleanup;
    }

    /* plan a collective operation */
    ucg_plan_t *plan;
    ucg_plan_component_t *planner;
    ucg_collective_params_t coll_params = {{0}};
    coll_params.send.buf = "send-buffer";
    coll_params.recv.buf = "recv-buffer";
    coll_params.send.dt_len =
    coll_params.recv.dt_len = 1;
    coll_params.send.count =
    coll_params.recv.count = dtype_count;
    coll_params.type.root = root;

    unsigned i, last  = ucs_static_array_size(ucg_predefined_collective_names);
    const char **name = ucg_predefined_collective_names;
    for (i = 0; i < last; i++) {
        if (!strcmp(ucg_predefined_collective_names[i], collective_type_name)) {
            coll_params.type.modifiers = UCG_GROUP_COLLECTIVE_MODIFIER_MOCK_EPS |
                    ucg_predefined_modifiers[i];
            break;
        }
        name++;
    }
    if (i == last) {
        status = UCS_ERR_UNSUPPORTED;
        goto group_cleanup;
    }

    // TODO: set req_planner_name
    size_t gctx_offset;
    status = ucg_plan_select(group, &coll_params, &planner, &gctx_offset);
    if (status != UCS_OK) {
        goto group_cleanup;
    }

    status = planner->plan(UCS_PTR_BYTE_OFFSET(group, gctx_offset),
                           &coll_params.type, &plan);
    if (status != UCS_OK) {
        goto group_cleanup;
    }

    plan->group      = group;
    plan->planner    = planner;
    plan->type       = coll_params.type;
    ucs_list_head_init(&plan->op_head);

    planner->print(plan, &coll_params);

group_cleanup:
    ucg_group_destroy(group);

address_cleanup:
    ucp_worker_release_address(worker, worker_address);

cleanup:
    if (status != UCS_OK) {
        printf("<Failed to plan a UCG collective: %s>\n", ucs_status_string(status));
    }

    ucs_free(distance_array);
}
