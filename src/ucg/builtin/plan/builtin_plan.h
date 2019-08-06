/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCG_BUILTIN_PLAN_H
#define UCG_BUILTIN_PLAN_H

#include <ucg/api/ucg_plan_component.h>
#include <ucs/datastruct/mpool.inl>
#include <uct/api/uct.h>

enum ucg_builtin_plan_topology_type {
    UCG_PLAN_RECURSIVE,
    UCG_PLAN_TREE_FANIN,
    UCG_PLAN_TREE_FANOUT,
    UCG_PLAN_TREE_FANIN_FANOUT,
    UCG_PLAN_ALLTOALL_AGGREGATION,
    UCG_PLAN_ALLTOALL_BRCUK,
    UCG_PLAN_LAST
};

enum UCS_S_PACKED ucg_builtin_plan_method_type {
    UCG_PLAN_METHOD_SEND_TERMINAL,     /* Send the message(s), nothing fancy */
    UCG_PLAN_METHOD_RECV_TERMINAL,     /* Final stop for incoming messages */
    UCG_PLAN_METHOD_BCAST_WAYPOINT,    /* receive and send on to all peers */
    UCG_PLAN_METHOD_GATHER_WAYPOINT,   /* gather from all peers, and pass on */
    UCG_PLAN_METHOD_SCATTER_TERMINAL,  /* scatter to all peers in the map */
    UCG_PLAN_METHOD_SCATTER_WAYPOINT,  /* scatter and send "downwards" */
    UCG_PLAN_METHOD_REDUCE_TERMINAL,   /* receive and reduce from each peer */
    UCG_PLAN_METHOD_REDUCE_WAYPOINT,   /* receive, reduce, and pass onwards */
    UCG_PLAN_METHOD_REDUCE_RECURSIVE,  /* send+receive and reduce (RD) */
    UCG_PLAN_METHOD_NEIGHBOR,          /* "halo exchange", for neighborhood ops */
};

typedef struct ucg_builtin_plan_phase {
    /* Parameters for buffer send/recv action */
    union {
        uct_ep_h                     *multi_eps;     /* endpoint pointer array */
        uct_ep_h                      single_ep;     /* single endpoint handle */
    };
    uint32_t                          ep_cnt;        /* Number of endpoints (below) */
    enum ucg_builtin_plan_method_type method;        /* how to apply this map */
    ucg_step_idx_t                    step_index;    /* determines step index */

    size_t                            max_short_one; /* max single short message */
    size_t                            max_short_max; /* max length to use short */
    size_t                            max_bcopy_one; /* max single bcopy message */
    size_t                            max_bcopy_max; /* max length to use bcopy */
    size_t                            max_zcopy_one; /* max single zcopy message */

    uct_md_h                          md;            /* memory (registration) domain */
    const uct_md_attr_t              *md_attr;       /* memory domain attributes */
    const uct_iface_attr_t           *ep_attr;       /* endpoint attributes */

#if ENABLE_DEBUG_DATA || ENABLE_FAULT_TOLERANCE
    ucg_group_member_index_t         *indexes;       /* array corresponding to EPs */
#endif
} ucg_builtin_plan_phase_t;

typedef struct ucg_builtin_group_ctx ucg_builtin_group_ctx_t;
typedef struct ucg_builtin_plan {
    ucg_plan_t               super;
    void                    *slots;   /* slots for builtin operations */
    ucs_list_link_t         *resend;  /* per-group list of requests to resend */
    ucs_list_link_t          list;    /* member of a per-group list of plans */
    ucs_list_link_t          by_root; /* extra phases for non-zero root */
    ucs_mpool_t              op_mp;   /* memory pool for (builtin_)operations */
    ucg_step_idx_t           phs_cnt; /* number of phases in the normal flow */
    uint8_t                  ep_cnt;  /* total endpoint count */
    uint16_t                 am_id;   /* active message ID */
    ucg_builtin_plan_phase_t phss[];  /* topology's phases */
/*  uct_ep_h                 eps[];    * logically located here */
} ucg_builtin_plan_t;

#define UCG_BUILTIN_CONNECT_SINGLE_EP ((unsigned)-1)
ucs_status_t ucg_builtin_connect(ucg_builtin_group_ctx_t *ctx,
        ucg_group_member_index_t idx, ucg_builtin_plan_phase_t *phase,
        unsigned phase_ep_index);

typedef struct ucg_builtin_config ucg_builtin_config_t;

typedef struct ucg_builtin_tree_config {
    unsigned radix;
#define UCG_BUILTIN_TREE_MAX_RADIX (32)
    unsigned sock_thresh;
} ucg_builtin_tree_config_t;
extern ucs_config_field_t ucg_builtin_tree_config_table[];
ucs_status_t ucg_builtin_tree_create(ucg_builtin_group_ctx_t *ctx,
        enum ucg_builtin_plan_topology_type plan_topo_type,
        const ucg_builtin_config_t *config,
        const ucg_group_params_t *group_params,
        const ucg_collective_type_t *coll_type,
        ucg_builtin_plan_t **plan_p);
ucs_status_t ucg_builtin_topo_tree_set_root(ucg_group_member_index_t root,
        ucg_group_member_index_t my_index,
        ucg_builtin_plan_t *plan,
        ucg_builtin_plan_phase_t **first_phase_p,
        unsigned *phase_count_p);
typedef struct ucg_builtin_tree_params {
    enum ucg_builtin_plan_topology_type topo_type;
    const ucg_group_params_t           *group_params;
    const ucg_collective_type_t        *coll_type;
    const ucg_builtin_tree_config_t    *config;
    ucg_group_member_index_t            root;
    ucg_builtin_group_ctx_t            *ctx;
} ucg_builtin_tree_params_t;
typedef struct ucg_builtin_topo_tree_root_phase {
    ucs_list_link_t          list;
    ucg_group_member_index_t root;
    ucg_step_idx_t           phs_cnt;
    ucg_builtin_plan_phase_t phss[UCG_BUILTIN_TREE_MAX_RADIX];
} ucg_builtin_topo_tree_root_phase_t;
ucs_status_t ucg_builtin_tree_connect(ucg_builtin_plan_t *tree,
        ucg_builtin_topo_tree_root_phase_t *root,
        const ucg_builtin_tree_params_t *params,
        ucg_step_idx_t step_offset, uct_ep_h *first_ep,
        ucg_group_member_index_t *host_up,   unsigned host_up_cnt,
        ucg_group_member_index_t *net_up,    unsigned net_up_cnt,
        ucg_group_member_index_t *net_down,  unsigned net_down_cnt,
        ucg_group_member_index_t *host_down, unsigned host_down_cnt);
ucs_status_t ucg_builtin_tree_add_intra(const ucg_builtin_tree_params_t *params,
        ucg_group_member_index_t *my_idx,
        unsigned *ppn,
        ucg_group_member_index_t *up,
        unsigned *final_up_cnt,
        ucg_group_member_index_t *down,
        unsigned *final_down_cnt,
        enum ucg_group_member_distance *master_phase);

typedef struct ucg_builtin_recursive_config {
    unsigned factor;
} ucg_builtin_recursive_config_t;
extern ucs_config_field_t ucg_builtin_recursive_config_table[];
ucs_status_t ucg_builtin_recursive_create(ucg_builtin_group_ctx_t *ctx,
        enum ucg_builtin_plan_topology_type plan_topo_type,
        const ucg_builtin_config_t *config,
        const ucg_group_params_t *group_params,
        const ucg_collective_type_t *coll_type,
        ucg_builtin_plan_t **plan_p);

typedef struct ucg_builtin_neighbor_config {
    unsigned dimension;
} ucg_builtin_neighbor_config_t;
extern ucs_config_field_t ucg_builtin_neighbor_config_table[];
ucs_status_t ucg_topo_neighbor_create(ucg_builtin_group_ctx_t *ctx,
        enum ucg_builtin_plan_topology_type plan_topo_type,
        const ucg_builtin_config_t *config,
        const ucg_group_params_t *group_params,
        const ucg_collective_type_t *coll_type,
        ucg_builtin_plan_t **plan_p);

struct ucg_builtin_config {
    ucg_plan_config_t    super;

    ucg_builtin_tree_config_t      tree;
    ucg_builtin_recursive_config_t recursive;
    ucg_builtin_neighbor_config_t  neighbor;

    unsigned                       cache_size;
    size_t                         short_max_tx;
    size_t                         bcopy_max_tx;
    unsigned                       mem_reg_opt_cnt;
};

#endif
