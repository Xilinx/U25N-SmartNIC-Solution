// SPDX-License-Identifier: GPL-2.0-only
/*
 * EMCDI framework support in Xilinx Axi Ethernet device driver
 * Copyright (c) 2021  Xilinx, Inc. All rights reserved.
 *
 * This is a driver for the  Xilinx Axi Ethernet which is used in the Virtex6
 * and Spartan6.
 */

#include "xilinx_axienet_mcdi.h"

#define NETLINK_USER 31
#define PAYLOAD_SIZE  200
struct sock *nl_sk = NULL;
EXPORT_SYMBOL(nl_sk);

extern struct axienet_local *lp_g;
/* Attribute validation policy */
extern int snd_que;
int pid[8]; 
EXPORT_SYMBOL(pid);

void add_emcdi_header_to_logs(struct sk_buff *skb, struct nlmsghdr *nlh, int data_len)
{
        struct emcdi_ethhdr *emcdi_hdr;
        u8 emcdi_src_mac_addr[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
        static int seqnum = 0;
	struct sk_buff *skb_out;
	int rc;
	skb_out = alloc_skb(1000 ,GFP_KERNEL);
   	if (skb_out == NULL) {
		printk(KERN_ALERT "Error creating skb.\n");
                return;
  	}

	skb_reserve(skb_out, sizeof(struct emcdi_ethhdr) + 10 ); // +10
        skb_put(skb_out, PAYLOAD_SIZE);
	strncpy(skb_out->data, nlmsg_data(nlh), data_len); //MSP
        emcdi_hdr = (struct emcdi_ethhdr *)skb_push(skb_out, sizeof(struct emcdi_ethhdr));
        eth_broadcast_addr(emcdi_hdr->h_dest);
        ether_addr_copy(emcdi_hdr->h_source, emcdi_src_mac_addr);
        emcdi_hdr->h_inner_vlan_proto = htons(ETH_P_8021Q);
        emcdi_hdr->h_inner_vlan_TCI = htons(U25_MPORT_TO_VLAN(U25_MPORT_ID_CONTROL));
        emcdi_hdr->h_vlan_encapsulated_proto = htons(ETH_P_802_EX1);
        emcdi_hdr->type = U25_EMCDI_TYPE_LOGS;
        emcdi_hdr->reserved = 0;
        emcdi_hdr->seq_num = htons(seqnum);
        seqnum++;

	skb_out->queue_mapping = snd_que;
	netif_tx_lock(lp_g->ndev);
        lp_g->ndev->netdev_ops->ndo_start_xmit(skb_out, lp_g->ndev);
	netif_tx_unlock(lp_g->ndev);
}

static void nl_recv_msg(struct sk_buff *skb)
{
        struct nlmsghdr *nlh;
	uint8_t chan_id;
#if 0
	int i;
        char tmp[3];
        char pkt[200] = { [0 ... 199] = 0 };
        for (i = 0; i < 40; i++) {
                snprintf(tmp, 3, "%02X", skb->data[i]);
                strcat(pkt, tmp);
        }
        pr_info(" ----- %s ---- pkt %s \n", __func__, pkt);
#endif
	nlh = (struct nlmsghdr *)skb->data;
	if (nlh->nlmsg_type == NL_TYPE_ACK) {
		struct emcdi_ethhdr *hdr;
		skb_pull(skb, NLMSG_HDRLEN);
		chan_id = skb->data[0];
		hdr = (struct emcdi_ethhdr *)skb_pull(skb, 1);
		if (hdr->h_vlan_encapsulated_proto == htons(ETH_P_802_EX1)) {
			int len = nlh->nlmsg_len - NLMSG_HDRLEN;
			int offset;
			struct sk_buff *skb_out;
			/* struct sk_buff *skb_out = NULL;
			 * skb_out = skb_clone(skb, GFP_ATOMIC);
			 */
			skb_out = netdev_alloc_skb(lp_g->ndev, PKT_BUFF_SZ);
			//skb_out = skb_dequeue(lp_g->skbq);

			skb_put(skb_out, len);
			//skb_out->data = skb->data;
			memcpy(skb_out->data, skb->data, len);
			if (skb_out->data == NULL) {
				//printk("error while receiving data\n");
				//skb_queue_head(lp_g->skbq, skb_out);
				goto out;
			}
			skb_out->queue_mapping = chan_id;
			netif_tx_lock(lp_g->ndev);
			lp_g->ndev->netdev_ops->ndo_start_xmit(skb_out, lp_g->ndev);
			netif_tx_unlock(lp_g->ndev);
#if 0
			offset = (SKB_QUEUE_LEN - skb_queue_len(lp_g->skbq)) - 1;
			lp_g->skbuff[offset] = netdev_alloc_skb(lp_g->ndev, PKT_BUFF_SZ);
			skb_queue_tail(lp_g->skbq, lp_g->skbuff[offset]);
#endif
			//printk("Freeing skb in %s\n", __func__);
			//dev_kfree_skb(skb);
		} else
			printk("Not an emcdi packet nlmsg_type: %d\n", nlh->nlmsg_type);
	} else if(nlh->nlmsg_type == NL_TYPE_PID) {
		printk(KERN_INFO "Netlink received msg payload:%s\n", (char *)NLMSG_DATA(nlh));
		if (!strcmp(NLMSG_DATA(nlh), "MAE")) {
			pr_info("MAE application\n");
			pid[0] = nlh->nlmsg_pid;
		} 
		if (!strcmp(NLMSG_DATA(nlh), "IPSEC")) {
			pr_info("IPSEC application\n");
			pid[1] = nlh->nlmsg_pid;
		}
		if (!strcmp(NLMSG_DATA(nlh), "FIREWALL")) {
			pr_info("FIREWALL application\n");
			pid[2] = nlh->nlmsg_pid;
		}
		if (!strcmp(nlmsg_data(nlh), "IMAGE UPGRADE")) {
			pr_info("IMAGE UPGRADE Application\n");
			pid[7] = nlh->nlmsg_pid;
		}
		if (!strcmp(NLMSG_DATA(nlh), "CONTROLLER")) {
                        pr_info("CONTROLLER application\n");
                        pid[6] = nlh->nlmsg_pid;
                }
		if (!strcmp(nlmsg_data(nlh), "FLASH UPGRADE")) {
                        pr_info("FLASH UPGRADE Application\n");
                        pid[5] = nlh->nlmsg_pid;
                }
		if (!strcmp(NLMSG_DATA(nlh), "QOS_HTB_CONFIG")) {
			pr_info("QOS_HTB_CONFIG application\n");
			pid[3] = nlh->nlmsg_pid;
		}
		if (!strcmp(NLMSG_DATA(nlh), "LOGS")) {
                        pr_info("LOGGER application\n");
                        pid[4] = nlh->nlmsg_pid;
                }

	} else if(nlh->nlmsg_type ==  NL_TYPE_LOGS) {
		skb_pull(skb, NLMSG_HDRLEN);
		add_emcdi_header_to_logs(skb , nlh,  nlh->nlmsg_len - NLMSG_HDRLEN );
	} else {
		printk("Unknown nlmsg_type: %d\n", nlh->nlmsg_type);
	}
out:
        return;
}

int u25_netlink_init(void) 
{
	struct netlink_kernel_cfg cfg = {
	      	.input = nl_recv_msg,
	};
	nl_sk = netlink_kernel_create(&init_net, NETLINK_USER, &cfg);
   	if (!nl_sk) 
	        return -ENOMEM;

	return 0;

}
EXPORT_SYMBOL(u25_netlink_init);

int u25_netlink_exit(void)
{
        netlink_kernel_release(nl_sk);
        return 0;
}
EXPORT_SYMBOL(u25_netlink_exit);

