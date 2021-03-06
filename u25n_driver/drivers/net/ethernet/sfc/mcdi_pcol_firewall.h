/****************************************************************************
 * Stateless Firewall Offload framework for Xilinx U25N Accelerator Card.
 * Copyright 2021 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef MCDI_PCOL_FIREWALL_H
#define MCDI_PCOL_FIREWALL_H
/************************************/
/* LEGACY_FIREWALL_OFFSETS 
 * FIREWALL_FIELD_MASK_VALUE_PAIRS_V2 structuredef 
 */
/* FIREWALL_FIELD_MASK_VALUE_PAIRS_V2 structuredef */
#define MC_CMD_FIREWALL_RULE_ADD 0x300
#define MC_CMD_FIREWALL_RULE_DEL 0x301
#define MC_CMD_FIREWALL_CAPS 	 0x302

/* MC_CMD_MAE_ACTION_RULE_INSERT_OUT msgresponse */
#define    MC_CMD_FIREWALL_ACTION_RULE_INSERT_OUT_LEN 4
#define       MC_CMD_FIREWALL_ACTION_RULE_INSERT_OUT_AR_ID_OFST 0
#define       MC_CMD_FIREWALL_ACTION_RULE_INSERT_OUT_AR_ID_LEN 4

#define    FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_LEN 100
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_INGRESS_PORT_OFST 0
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_INGRESS_PORT_LEN 1
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_INGRESS_PORT_LBN 0
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_INGRESS_PORT_WIDTH 8
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_INGRESS_PORT_MASK_OFST 1
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_INGRESS_PORT_MASK_LEN 1
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_INGRESS_PORT_MASK_LBN 8
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_INGRESS_PORT_MASK_WIDTH 8

#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_CHAIN_IDX_OFST 2
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_CHAIN_IDX_LEN 1
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_CHAIN_IDX_LBN 16
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_CHAIN_IDX_WIDTH 8
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_CHAIN_IDX_MASK_OFST 3
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_CHAIN_IDX_MASK_LEN 1
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_CHAIN_IDX_MASK_LBN 24
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_CHAIN_IDX_MASK_WIDTH 8

#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_IP_PROTO_OFST 4
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_IP_PROTO_LEN 1
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_IP_PROTO_LBN 32
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_IP_PROTO_WIDTH 8
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_IP_PROTO_MASK_OFST 5
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_IP_PROTO_MASK_LEN 1
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_IP_PROTO_MASK_LBN 40
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_IP_PROTO_MASK_WIDTH 8

#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_SRC_IP6_BE_OFST 6
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_SRC_IP6_BE_LEN 16
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_SRC_IP6_BE_LBN 48
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_SRC_IP6_BE_WIDTH 128
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_SRC_IP6_BE_MASK_OFST 22
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_SRC_IP6_BE_MASK_LEN 16
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_SRC_IP6_BE_MASK_LBN 176
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_SRC_IP6_BE_MASK_WIDTH 128

#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_DST_IP6_BE_OFST 38
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_DST_IP6_BE_LEN 16
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_DST_IP6_BE_LBN 304
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_DST_IP6_BE_WIDTH 128
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_DST_IP6_BE_MASK_OFST 54
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_DST_IP6_BE_MASK_LEN 16
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_DST_IP6_BE_MASK_LBN 432
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_DST_IP6_BE_MASK_WIDTH 128

#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_ETH_PROTO_OFST 70
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_ETH_PROTO_LEN 2
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_ETH_PROTO_LBN 560
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_ETH_PROTO_WIDTH 16

#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_SRC_IP4_BE_OFST 72
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_SRC_IP4_BE_LEN 4
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_SRC_IP4_BE_LBN 576
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_SRC_IP4_BE_WIDTH 32
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_SRC_IP4_BE_MASK_OFST 76
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_SRC_IP4_BE_MASK_LEN 4
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_SRC_IP4_BE_MASK_LBN 608
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_SRC_IP4_BE_MASK_WIDTH 32

#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_DST_IP4_BE_OFST 80
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_DST_IP4_BE_LEN 4
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_DST_IP4_BE_LBN 640
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_DST_IP4_BE_WIDTH 32
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_DST_IP4_BE_MASK_OFST 84
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_DST_IP4_BE_MASK_LEN 4
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_DST_IP4_BE_MASK_LBN 672
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_DST_IP4_BE_MASK_WIDTH 32

#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_L4_SPORT_BE_OFST 88
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_L4_SPORT_BE_LEN 2
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_L4_SPORT_BE_LBN 704
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_L4_SPORT_BE_WIDTH 16
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_L4_SPORT_BE_MASK_OFST 90
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_L4_SPORT_BE_MASK_LEN 2
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_L4_SPORT_BE_MASK_LBN 720
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_L4_SPORT_BE_MASK_WIDTH 16

#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_L4_DPORT_BE_OFST 92
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_L4_DPORT_BE_LEN 2
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_L4_DPORT_BE_LBN 736
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_L4_DPORT_BE_WIDTH 16
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_L4_DPORT_BE_MASK_OFST 94
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_L4_DPORT_BE_MASK_LEN 2
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_L4_DPORT_BE_MASK_LBN 752
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_L4_DPORT_BE_MASK_WIDTH 16

#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_ACTION_FLAG_OFST 96
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_ACTION_FLAG_LEN 1
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_ACTION_FLAG_LBN 768
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_ACTION_FLAG_WIDTH 8

#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_ACTION_FLAG0_OFST 97
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_ACTION_FLAG0_LEN 1
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_ACTION_FLAG0_LBN 776
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_ACTION_FLAG0_WIDTH 8

#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_ACTION_FLAG1_OFST 98
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_ACTION_FLAG1_LEN 1
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_ACTION_FLAG1_LBN 784
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_ACTION_FLAG1_WIDTH 8

#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_ACTION_FLAG2_OFST 99
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_ACTION_FLAG2_LEN 1
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_ACTION_FLAG2_LBN 792
#define       FIREWALL_FIELD_MASK_VALUE_PAIRS_V2_ACTION_FLAG2_WIDTH 8

/* FIREWALL_FIELD_MASK_VALUE_PAIRS_V2 structuredef ends */
#endif /* MCDI_PCOL_FIREWALL_H */
