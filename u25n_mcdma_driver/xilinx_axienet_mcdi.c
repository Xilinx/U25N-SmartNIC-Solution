// SPDX-License-Identifier: GPL-2.0-only
/*
 * EMCDI framework support in Xilinx Axi Ethernet device driver
 * Copyright (c) 2021  Xilinx, Inc. All rights reserved.
 *
 * This is a driver for the  Xilinx Axi Ethernet which is used in the Virtex6
 * and Spartan6.
 */

#include "xilinx_axienet_mcdi.h"

extern int pid[8], snd_seq;
extern struct sock *nl_sk;
struct axienet_local *lp_g;
EXPORT_SYMBOL(lp_g);
int snd_que;
EXPORT_SYMBOL(snd_que);

void pkt_hex_dump(struct sk_buff *skb)
{
#if 0
    size_t len;
    int rowsize = 16;
    int i, l, linelen, remaining;
    uint8_t *data, ch;

    printk("Packet hex dump:\n");
    data = (uint8_t *) skb->data;
    printk("data[34] is %d\n",data[34]);

    if(data[34] != 0 && data[58] != 0 ) {

	    len = 64;

	    remaining = len;
	    for (i = 0; i < len; i += rowsize) {

		    linelen = min(remaining, rowsize);
		    remaining -= rowsize;

		    for (l = 0; l < linelen; l++) {
			    ch = data[l];
			    printk(KERN_CONT "%02X ", (uint32_t) ch);
		    }

		    data += linelen;

		    printk(KERN_CONT "\n");
	    }

   }	
#endif
}

void *add_netlink_header(struct sk_buff *skb, uint8_t index)
{
        struct nlmsghdr *nlh;
        nlh = (struct nlmsghdr *)skb_push(skb , NLMSG_HDRLEN);
        nlh->nlmsg_type = NL_TYPE_CAM;
        nlh->nlmsg_len = skb->len;
        nlh->nlmsg_flags = 0;
        nlh->nlmsg_pid = pid[index];
        nlh->nlmsg_seq = 0;
	
        return (char*) nlh;
}

void control_packet_handle(struct sk_buff *skb, uint8_t qid, uint8_t index)
{
	int  err;
        void *msg_head;

#if 0	
	if(index == 5) {
		int n;
		n = sizeof(pid)/sizeof(pid[0]);
		skb_push(skb , 1);
		skb->data[0] = n-2;
	}
#endif
	skb_push(skb , 1);
	skb->data[0] = qid;

	msg_head = add_netlink_header(skb, index);
        if (msg_head == NULL) {
                pr_info("ERROR in adding netlink header\n");
                err = -ENOMEM;
                goto out;
        }
 
        nlmsg_end(skb , msg_head);
	if (pid[index] == 0) {
		pr_info("ERR: Application not available!\n");
		goto out;
	} else {
		rcu_read_lock();
        	err = nlmsg_unicast(nl_sk, skb, pid[index]);
        	if (err != 0) {
        	        pr_info("ERR:nlmsg_unicast err: %d\n", err);
        	}
		rcu_read_unlock();
		return;
	}
out:
	dev_kfree_skb(skb);
	return;
}

void ipsec_counter_ack_packet_handle(uint16_t seq_num, uint8_t *data)
{
#if 0
	pr_info("((((((( ipsec counter packet )))))))\n");
	pr_info("seq_no %d, status %x\n", seq_num, *data);
        return;
#endif
}

void axienet_counter_packet_handler(struct axienet_local *lp, struct sk_buff *skb)
{
        struct emcdi_ethhdr *emcdi_hdr;
        u8 emcdi_src_mac_addr[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
	static int seqnum = 0;
	uint8_t *data;

//	pkt_hex_dump(skb);
	data = (uint8_t *) skb->data;
	if(data[34] != 0 && data[58] != 0) {

		if(data[49] >= 0 && data[49] <= 7)
                        skb->queue_mapping = 0;
                if(data[49] >= 8 && data[49] <= 15)
                        skb->queue_mapping = 1;

		skb_pull(skb, 28);

        	emcdi_hdr = (struct emcdi_ethhdr *)skb_push(skb, sizeof(struct emcdi_ethhdr));
        	eth_broadcast_addr(emcdi_hdr->h_dest);
        	ether_addr_copy(emcdi_hdr->h_source, emcdi_src_mac_addr);
        	emcdi_hdr->h_inner_vlan_proto = htons(ETH_P_8021Q);
        	emcdi_hdr->h_inner_vlan_TCI = htons(U25_MPORT_TO_VLAN(U25_MPORT_ID_COUNTER));
        	emcdi_hdr->h_vlan_encapsulated_proto = htons(ETH_P_802_EX1);
        	emcdi_hdr->type = U25_EMCDI_TYPE_COUNTER;
        	emcdi_hdr->reserved = 0;
        	emcdi_hdr->seq_num = htons(seqnum);
        	seqnum++;
		netif_tx_lock(lp_g->ndev);
                lp_g->ndev->netdev_ops->ndo_start_xmit(skb, lp_g->ndev);
                netif_tx_unlock(lp_g->ndev);

	} else 
		dev_kfree_skb(skb);

	return;
}

int axienet_emcdi_packet_handler(struct axienet_local *lp, struct sk_buff *skb, uint8_t qid)
{
	uint8_t type;
	uint16_t inner_vlan;
	uint32_t inner_mport;
	static uint8_t pre_type = 0;
	
	lp_g = lp;	
	struct emcdi_ethhdr *hdr = (struct emcdi_ethhdr *)skb->data;

        if((hdr->h_inner_vlan_proto != htons(ETH_P_8021Q)) ||
        	(hdr->h_vlan_encapsulated_proto != htons(ETH_P_802_EX1)))
		return -EIO;

	inner_vlan = htons(hdr->h_inner_vlan_TCI);
	inner_mport = U25_VLAN_TO_MPORT(inner_vlan);
	type = hdr->type;
	if (pre_type != type) {
		//printk("----%s----and type is %d\n", __func__, type);
		pre_type = type;
	}
	
	if (inner_mport == U25_MPORT_ID_CONTROL) {
        switch (type) {
            case U25_EMCDI_TYPE_CONTROL:
                control_packet_handle(skb, qid - 1, U25_EMCDI_TYPE_CONTROL_INDEX);
                break;
            case U25_EMCDI_TYPE_IPSEC:
                control_packet_handle(skb, qid - 1, U25_EMCDI_TYPE_IPSEC_INDEX);
                break;
            case U25_EMCDI_TYPE_FIREWALL:
                control_packet_handle(skb, qid - 1, U25_EMCDI_TYPE_FIREWALL_INDEX);
                break;
	    case U25_EMCDI_TYPE_IMG:
		control_packet_handle(skb, qid - 1, U25_EMCDI_TYPE_IMG_INDEX);
		break;
	    case U25_EMCDI_TYPE_CONTROLLER:
                control_packet_handle(skb, qid - 1, U25_EMCDI_TYPE_CONTROLLER_INDEX);
                break;
#if 1
            case U25_EMCDI_TYPE_QOS_HTB_CONFIG:
                control_packet_handle(skb, qid - 1, U25_EMCDI_TYPE_QOS_HTB_CONFIG_INDEX);
                break;
	    case U25_EMCDI_TYPE_LOGS:
		snd_que = qid-1;
		control_packet_handle(skb, qid - 1, U25_EMCDI_TYPE_LOGS_INDEX);
                break;
#endif
	    case U25_EMCDI_TYPE_FLASH_UPGRADE:
                control_packet_handle(skb, qid - 1, U25_EMCDI_TYPE_FLASH_UPGRADE_INDEX);
                break;
            default:
                pr_info("%s:Mismatch in control packet type\n",__func__);
                return -EIO;
        }
	} else {
		pr_info("%s: error in receieving packet\n",__func__);
		return -EIO;
	}

	return 0;
}

