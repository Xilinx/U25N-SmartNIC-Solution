// SPDX-License-Identifier: GPL-2.0-only
/*
 * EMCDI framework support in Xilinx Axi Ethernet device driver
 * Copyright (c) 2021  Xilinx, Inc. All rights reserved.
 *
 * This is a driver for the  Xilinx Axi Ethernet which is used in the Virtex6
 * and Spartan6.
 */
#ifndef XILINX_MCDI_H
#define XILINX_MCDI_H

#include "xilinx_axienet.h"

#define U25_EMCDI_TYPE_CONTROL          	0
#define U25_EMCDI_TYPE_COUNTER          	1
#define U25_EMCDI_TYPE_COUNTER_ACK      	2
#define U25_EMCDI_TYPE_IPSEC            	3
#define U25_EMCDI_TYPE_FIREWALL         	4

#define U25_EMCDI_TYPE_IMG 			8
#define U25_EMCDI_TYPE_CONTROLLER       	6
#define U25_EMCDI_TYPE_LOGS             	9
#define U25_EMCDI_TYPE_FLASH_UPGRADE    	5
#define U25_EMCDI_TYPE_QOS_HTB_CONFIG		15

#define U25_EMCDI_TYPE_CONTROL_INDEX		0
#define U25_EMCDI_TYPE_IPSEC_INDEX		1
#define U25_EMCDI_TYPE_FIREWALL_INDEX		2
#define U25_EMCDI_TYPE_QOS_HTB_CONFIG_INDEX	3
#define U25_EMCDI_TYPE_LOGS_INDEX        	4

#define U25_EMCDI_TYPE_IMG_INDEX	   	7
#define U25_EMCDI_TYPE_CONTROLLER_INDEX    	6
#define U25_EMCDI_TYPE_FLASH_UPGRADE_INDEX 	5

#define U25_MPORT_ID_COUNTER            	0xFD
#define U25_MPORT_ID_CONTROL            	0xFE
#define U25_MPORT_TO_VLAN(mport)        	mport ^ 0x800
#define U25_VLAN_TO_MPORT(vlan)         	vlan ^ 0x800


struct emcdi_ethhdr {
        unsigned char   h_dest[ETH_ALEN];
        unsigned char   h_source[ETH_ALEN];
//        __be16          h_outer_vlan_proto;
//        __be16          h_outer_vlan_TCI;
        __be16          h_inner_vlan_proto;
        __be16          h_inner_vlan_TCI;
        __be16          h_vlan_encapsulated_proto;
        unsigned char   type;
        unsigned char   reserved;
        __be16          seq_num;
} __attribute__((packed));

struct stats_hdr {
        __u8            version;
        __u8            rsvd[3];
        __be16          seq_index;
        __be16          n_counters;
} __attribute__((packed));

struct counter { 
	struct axienet_local *lp; 
	struct sk_buff *skb;
       	struct work_struct work; 
};
void control_packet_handle(struct sk_buff *skb, uint8_t qid, uint8_t index);
int axienet_emcdi_packet_handler(struct axienet_local *lp , struct sk_buff *skb, uint8_t qid);
void counter_ack_packet_handle(uint8_t seq_num);
void axienet_counter_packet_handler(struct axienet_local *lp, struct sk_buff *skb);

#endif /* XILINX_MCDI_H */

