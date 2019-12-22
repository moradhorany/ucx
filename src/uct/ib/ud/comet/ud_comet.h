/**
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */
#ifndef _UD_COMET_H_
#define _UD_COMET_H_

#include <comet_lib.h>

#include <uct/ib/ud/base/ud_ep.h>
#include <uct/ib/ud/accel/ud_mlx5.h>

typedef struct {
    uct_ud_iface_config_t               super;
    uct_ib_mlx5_iface_config_t          mlx5_common;
    uct_ud_mlx5_iface_common_config_t   ud_mlx5_common;

    /* COMET-specific configurations */
    unsigned                            device_index;
} uct_ud_comet_iface_config_t;

/**
 * COMET resource descriptor of a transport device
 */
#define UCT_COMET_DEVICE_NAME_MAX (UCT_DEVICE_NAME_MAX - sizeof(const struct comet_capabilities *))

#define UCT_UD_COMET_TL_NAME "ud_comet"

enum uct_ud_comet_coll_type {
    UCT_UD_COMET_COLL_TYPE_REDUCE    = COMET_TABLE_OPERATION_REDUCE,
    UCT_UD_COMET_COLL_TYPE_ALLTOALL  = COMET_TABLE_OPERATION_ALLTOALL,

    UCT_UD_COMET_COLL_TYPE_LAST
};

typedef union uct_ud_comet_table {
    volatile uint64_t           incoming_prefix;  /* Prefix is placed here upon completion */
    enum uct_ud_comet_coll_type collective_type;  /* Type of collective set for this table */
    void*                       incoming_data_va; /* Virtual address to write the payload */
    void*                       incoming_data_pa; /* Physical address to write the payload */
    size_t                      data_length;      /* Send/recv buffer (data) length */
    uct_tag_context_t           tag_ctx;          /* Tag context - for completion callbacks */
} uct_ud_comet_table_t;

typedef struct uct_tl_comet_device_resource {
    uct_device_type_t type;                                /**< Device type */
    char              tl_name[UCT_TL_NAME_MAX];            /**< Transport name */
    char              dev_name[UCT_COMET_DEVICE_NAME_MAX]; /**< Hardware device name */

    const struct comet_capabilities *comet_capabilities_p; /* Pointer to COMET capabilities */
} uct_tl_comet_device_resource_t;

typedef struct uct_ud_comet_ep_addr {
    uct_ud_ep_addr_t super;

    /* COMET peer capabilites (sent from server to client) */
    struct comet_capabilities comet_device_capabilities;

    uint16_t         table_id[UCT_UD_COMET_COLL_TYPE_LAST];
} uct_ud_comet_ep_addr_t;

typedef struct uct_ud_comet_ep {
    uct_ud_mlx5_ep_t super;

    /* Header for sending comet packets */
    struct comet_packet_header header[UCT_UD_COMET_COLL_TYPE_LAST];
} uct_ud_comet_ep_t;

typedef struct uct_ud_comet_iface {
    uct_ud_mlx5_iface_t   super;

    /* For functions in need of calling their super-class equivalent: */
    unsigned            (*super_progress)(uct_iface_h tl_iface);
    ucs_status_t        (*super_get_addr)(uct_ep_h ep, uct_ep_addr_t *addr);
    ucs_status_t        (*super_connect) (uct_ep_h ep,
                                          const uct_device_addr_t *dev_addr,
                                          const uct_ep_addr_t *ep_addr);
    ucs_status_t 		(*super_uct_ud_ep_am_zcopy)(uct_ep_h tl_ep, uint8_t id,
                                          const void *header,
                                          unsigned header_length,
                                          const uct_iov_t *iov,
                                          size_t iovcnt, unsigned flags,
                                          uct_completion_t *comp);

    uint32_t              sm_proc_cnt;         /* provided PPN information */
    uct_md_attr_t         md_attr;             /* memory domain attributes */
    void                 *comet_ref;           /* device reference / handle */
    uint32_t              comet_device_index;  /* device index */
    uint32_t              table_cnt;           /* number of tables */
    uct_ud_comet_table_t *tables;              /* per-table information */
    uint8_t				  my_group_index;	   /* COMET slot-id */
} uct_ud_comet_iface_t;

int
ud_comet_is_initialized(void);

#endif /* _UD_COMET_H_ */

