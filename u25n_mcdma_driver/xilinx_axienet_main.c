// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xilinx Axi Ethernet device driver
 *
 * Copyright (c) 2008 Nissin Systems Co., Ltd.,  Yoshio Kashiwagi
 * Copyright (c) 2005-2008 DLA Systems,  David H. Lynch Jr. <dhlii@dlasys.net>
 * Copyright (c) 2008-2009 Secret Lab Technologies Ltd.
 * Copyright (c) 2010 - 2011 Michal Simek <monstr@monstr.eu>
 * Copyright (c) 2010 - 2011 PetaLogix
 * Copyright (c) 2019 SED Systems, a division of Calian Ltd.
 * Copyright (c) 2010 - 2012 Xilinx, Inc. All rights reserved.
 *
 * This is a driver for the Xilinx Axi Ethernet which is used in the Virtex6
 * and Spartan6.
 *
 * TODO:
 *  - Add Axi Fifo support.
 *  - Factor out Axi DMA code into separate driver.
 *  - Test and fix basic multicast filtering.
 *  - Add support for extended multicast filtering.
 *  - Test basic VLAN support.
 *  - Add support for extended VLAN support.
 */

#include <linux/clk.h>
#include <linux/circ_buf.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_net.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/phy.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/iopoll.h>
#include <linux/ptp_classify.h>
#include <linux/net_tstamp.h>
#include <linux/random.h>
#include <net/sock.h>
#include <linux/xilinx_phy.h>
#include <linux/clk.h>
#include <linux/workqueue.h>
#ifndef XILINX_MAC_DEBUG
//#include "xilinx_axienet.h"
#include "xilinx_axienet_mcdi.h"
#endif
#ifdef CONFIG_XILINX_TSN_PTP
#include "xilinx_tsn_ptp.h"
#include "xilinx_tsn_timer.h"
#endif
/* Descriptors defines for Tx and Rx DMA */
#define TX_BD_NUM_DEFAULT		64
#define RX_BD_NUM_DEFAULT		1024
#define TX_BD_NUM_MAX			4096
#define RX_BD_NUM_MAX			4096

/* Must be shorter than length of ethtool_drvinfo.driver field to fit */
#define DRIVER_NAME		"xaxienet"
#define DRIVER_DESCRIPTION	"Xilinx Axi Ethernet driver"
#define DRIVER_VERSION		"1.00a"

#define AXIENET_REGS_N		40
#define AXIENET_TS_HEADER_LEN	8
#define XXVENET_TS_HEADER_LEN	4
#define NS_PER_SEC              1000000000ULL /* Nanoseconds per second */

#ifndef XILINX_MAC_DEBUG
int emcdi_packet;
EXPORT_SYMBOL(emcdi_packet);
#endif

#ifdef CONFIG_XILINX_TSN_PTP
int axienet_phc_index = -1;
EXPORT_SYMBOL(axienet_phc_index);
#endif

/* Option table for setting up Axi Ethernet hardware options */
static struct axienet_option axienet_options[] = {
	/* Turn on jumbo packet support for both Rx and Tx */
	{
		.opt = XAE_OPTION_JUMBO,
		.reg = XAE_TC_OFFSET,
		.m_or = XAE_TC_JUM_MASK,
	}, {
		.opt = XAE_OPTION_JUMBO,
		.reg = XAE_RCW1_OFFSET,
		.m_or = XAE_RCW1_JUM_MASK,
	}, { /* Turn on VLAN packet support for both Rx and Tx */
		.opt = XAE_OPTION_VLAN,
		.reg = XAE_TC_OFFSET,
		.m_or = XAE_TC_VLAN_MASK,
	}, {
		.opt = XAE_OPTION_VLAN,
		.reg = XAE_RCW1_OFFSET,
		.m_or = XAE_RCW1_VLAN_MASK,
	}, { /* Turn on FCS stripping on receive packets */
		.opt = XAE_OPTION_FCS_STRIP,
		.reg = XAE_RCW1_OFFSET,
		.m_or = XAE_RCW1_FCS_MASK,
	}, { /* Turn on FCS insertion on transmit packets */
		.opt = XAE_OPTION_FCS_INSERT,
			.reg = XAE_TC_OFFSET,
			.m_or = XAE_TC_FCS_MASK,
	}, { /* Turn off length/type field checking on receive packets */
		.opt = XAE_OPTION_LENTYPE_ERR,
			.reg = XAE_RCW1_OFFSET,
			.m_or = XAE_RCW1_LT_DIS_MASK,
	}, { /* Turn on Rx flow control */
		.opt = XAE_OPTION_FLOW_CONTROL,
			.reg = XAE_FCC_OFFSET,
			.m_or = XAE_FCC_FCRX_MASK,
	}, { /* Turn on Tx flow control */
		.opt = XAE_OPTION_FLOW_CONTROL,
			.reg = XAE_FCC_OFFSET,
			.m_or = XAE_FCC_FCTX_MASK,
	}, { /* Turn on promiscuous frame filtering */
		.opt = XAE_OPTION_PROMISC,
			.reg = XAE_FMC_OFFSET,
			.m_or = XAE_FMC_PM_MASK,
	}, { /* Enable transmitter */
		.opt = XAE_OPTION_TXEN,
			.reg = XAE_TC_OFFSET,
			.m_or = XAE_TC_TX_MASK,
	}, { /* Enable receiver */
		.opt = XAE_OPTION_RXEN,
			.reg = XAE_RCW1_OFFSET,
			.m_or = XAE_RCW1_RX_MASK,
	},
	{}
};

#ifdef XILINX_MAC_DEBUG
/* Option table for setting up Axi Ethernet hardware options */
static struct xxvenet_option xxvenet_options[] = {
	{ /* Turn on FCS stripping on receive packets */
		.opt = XAE_OPTION_FCS_STRIP,
		.reg = XXV_RCW1_OFFSET,
		.m_or = XXV_RCW1_FCS_MASK,
	}, { /* Turn on FCS insertion on transmit packets */
		.opt = XAE_OPTION_FCS_INSERT,
		.reg = XXV_TC_OFFSET,
		.m_or = XXV_TC_FCS_MASK,
	}, { /* Enable transmitter */
		.opt = XAE_OPTION_TXEN,
		.reg = XXV_TC_OFFSET,
		.m_or = XXV_TC_TX_MASK,
	}, { /* Enable receiver */
		.opt = XAE_OPTION_RXEN,
		.reg = XXV_RCW1_OFFSET,
		.m_or = XXV_RCW1_RX_MASK,
	},
	{}
};
#endif

/**
 * axienet_dma_bd_release - Release buffer descriptor rings
 * @ndev:	Pointer to the net_device structure
 *
 * This function is used to release the descriptors allocated in
 * axienet_dma_bd_init. axienet_dma_bd_release is called when Axi Ethernet
 * driver stop api is called.
 */
void axienet_dma_bd_release(struct net_device *ndev)
{
	int i;
	struct axienet_local *lp = netdev_priv(ndev);

#ifdef CONFIG_AXIENET_HAS_MCDMA
	for_each_tx_dma_queue(lp, i) {
		axienet_mcdma_tx_bd_free(ndev, lp->dq[i]);
	}
#endif
	for_each_rx_dma_queue(lp, i) {
#ifdef CONFIG_AXIENET_HAS_MCDMA
		axienet_mcdma_rx_bd_free(ndev, lp->dq[i]);
#else
		axienet_bd_free(ndev, lp->dq[i]);
#endif
	}
}

/**
 * axienet_dma_bd_init - Setup buffer descriptor rings for Axi DMA
 * @ndev:	Pointer to the net_device structure
 *
 * Return: 0, on success -ENOMEM, on failure
 *
 * This function is called to initialize the Rx and Tx DMA descriptor
 * rings. This initializes the descriptors with required default values
 * and is called when Axi Ethernet driver reset is called.
 */
static int axienet_dma_bd_init(struct net_device *ndev)
{
	int i, ret;
	struct axienet_local *lp = netdev_priv(ndev);

#ifdef CONFIG_AXIENET_HAS_MCDMA
	for_each_tx_dma_queue(lp, i) {
		ret = axienet_mcdma_tx_q_init(ndev, lp->dq[i]);
		if (ret != 0)
			break;
	}
#endif
	for_each_rx_dma_queue(lp, i) {
#ifdef CONFIG_AXIENET_HAS_MCDMA
		ret = axienet_mcdma_rx_q_init(ndev, lp->dq[i]);
#else
		ret = axienet_dma_q_init(ndev, lp->dq[i]);
#endif
		if (ret != 0) {
			netdev_err(ndev, "%s: Failed to init DMA buf\n", __func__);
			break;
		}
	}

	return ret;
}

/**
 * axienet_set_mac_address - Write the MAC address
 * @ndev:	Pointer to the net_device structure
 * @address:	6 byte Address to be written as MAC address
 *
 * This function is called to initialize the MAC address of the Axi Ethernet
 * core. It writes to the UAW0 and UAW1 registers of the core.
 */
void axienet_set_mac_address(struct net_device *ndev,
		const void *address)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (address)
		ether_addr_copy(ndev->dev_addr, address);
	if (!is_valid_ether_addr(ndev->dev_addr))
		eth_hw_addr_random(ndev);

	if (lp->axienet_config->mactype != XAXIENET_1G &&
			lp->axienet_config->mactype != XAXIENET_2_5G)
		return;

#ifdef XILINX_MAC_DEBUG
	/* Set up unicast MAC address filter set its mac address */
	axienet_iow(lp, XAE_UAW0_OFFSET,
			(ndev->dev_addr[0]) |
			(ndev->dev_addr[1] << 8) |
			(ndev->dev_addr[2] << 16) |
			(ndev->dev_addr[3] << 24));
	axienet_iow(lp, XAE_UAW1_OFFSET,
			(((axienet_ior(lp, XAE_UAW1_OFFSET)) &
			  ~XAE_UAW1_UNICASTADDR_MASK) |
			 (ndev->dev_addr[4] |
			  (ndev->dev_addr[5] << 8))));
#endif
}

/**
 * netdev_set_mac_address - Write the MAC address (from outside the driver)
 * @ndev:	Pointer to the net_device structure
 * @p:		6 byte Address to be written as MAC address
 *
 * Return: 0 for all conditions. Presently, there is no failure case.
 *
 * This function is called to initialize the MAC address of the Axi Ethernet
 * core. It calls the core specific axienet_set_mac_address. This is the
 * function that goes into net_device_ops structure entry ndo_set_mac_address.
 */
static int netdev_set_mac_address(struct net_device *ndev, void *p)
{
	struct sockaddr *addr = p;

	axienet_set_mac_address(ndev, addr->sa_data);
	return 0;
}

/**
 * axienet_set_multicast_list - Prepare the multicast table
 * @ndev:	Pointer to the net_device structure
 *
 * This function is called to initialize the multicast table during
 * initialization. The Axi Ethernet basic multicast support has a four-entry
 * multicast table which is initialized here. Additionally this function
 * goes into the net_device_ops structure entry ndo_set_multicast_list. This
 * means whenever the multicast table entries need to be updated this
 * function gets called.
 */
void axienet_set_multicast_list(struct net_device *ndev)
{
#ifdef XILINX_MAC_DEBUG
	int i;
	u32 reg, af0reg, af1reg;
	struct axienet_local *lp = netdev_priv(ndev);

	if ((lp->axienet_config->mactype != XAXIENET_1G) || lp->eth_hasnobuf)
		return;

	if (ndev->flags & (IFF_ALLMULTI | IFF_PROMISC) ||
			netdev_mc_count(ndev) > XAE_MULTICAST_CAM_TABLE_NUM) {
		/* We must make the kernel realize we had to move into
		 * promiscuous mode. If it was a promiscuous mode request
		 * the flag is already set. If not we set it.
		 */
		ndev->flags |= IFF_PROMISC;
		reg = axienet_ior(lp, XAE_FMC_OFFSET);
		reg |= XAE_FMC_PM_MASK;
		axienet_iow(lp, XAE_FMC_OFFSET, reg);
		dev_info(&ndev->dev, "Promiscuous mode enabled.\n");
	} else if (!netdev_mc_empty(ndev)) {
		struct netdev_hw_addr *ha;

		i = 0;
		netdev_for_each_mc_addr(ha, ndev) {
			if (i >= XAE_MULTICAST_CAM_TABLE_NUM)
				break;

			af0reg = (ha->addr[0]);
			af0reg |= (ha->addr[1] << 8);
			af0reg |= (ha->addr[2] << 16);
			af0reg |= (ha->addr[3] << 24);

			af1reg = (ha->addr[4]);
			af1reg |= (ha->addr[5] << 8);

			reg = axienet_ior(lp, XAE_FMC_OFFSET) & 0xFFFFFF00;
			reg |= i;

			axienet_iow(lp, XAE_FMC_OFFSET, reg);
			axienet_iow(lp, XAE_AF0_OFFSET, af0reg);
			axienet_iow(lp, XAE_AF1_OFFSET, af1reg);
			i++;
		}
	} else {
		reg = axienet_ior(lp, XAE_FMC_OFFSET);
		reg &= ~XAE_FMC_PM_MASK;

		axienet_iow(lp, XAE_FMC_OFFSET, reg);

		for (i = 0; i < XAE_MULTICAST_CAM_TABLE_NUM; i++) {
			reg = axienet_ior(lp, XAE_FMC_OFFSET) & 0xFFFFFF00;
			reg |= i;

			axienet_iow(lp, XAE_FMC_OFFSET, reg);
			axienet_iow(lp, XAE_AF0_OFFSET, 0);
			axienet_iow(lp, XAE_AF1_OFFSET, 0);
		}

		dev_info(&ndev->dev, "Promiscuous mode disabled.\n");
	}
#else
	return;
#endif
}

/**
 * axienet_setoptions - Set an Axi Ethernet option
 * @ndev:	Pointer to the net_device structure
 * @options:	Option to be enabled/disabled
 *
 * The Axi Ethernet core has multiple features which can be selectively turned
 * on or off. The typical options could be jumbo frame option, basic VLAN
 * option, promiscuous mode option etc. This function is used to set or clear
 * these options in the Axi Ethernet hardware. This is done through
 * axienet_option structure .
 */
static void axienet_setoptions(struct net_device *ndev, u32 options)
{
	int reg;
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_option *tp = &axienet_options[0];

	while (tp->opt) {
		reg = ((axienet_ior(lp, tp->reg)) & ~(tp->m_or));
		if (options & tp->opt)
			reg |= tp->m_or;
		axienet_iow(lp, tp->reg, reg);
		tp++;
	}

	lp->options |= options;
}

static void xxvenet_setoptions(struct net_device *ndev, u32 options)
{
#ifdef XILINX_MAC_DEBUG
	int reg;
	struct axienet_local *lp = netdev_priv(ndev);
	struct xxvenet_option *tp = &xxvenet_options[0];

	while (tp->opt) {
		reg = ((axienet_ior(lp, tp->reg)) & ~(tp->m_or));
		if (options & tp->opt)
			reg |= tp->m_or;
		axienet_iow(lp, tp->reg, reg);
		tp++;
	}

#else
	struct axienet_local *lp = netdev_priv(ndev);
	lp->options |= options;
#endif
}

void __axienet_device_reset(struct axienet_dma_q *q)
{
	u32 timeout;
	int status;
	/* Reset Axi DMA. This would reset Axi Ethernet core as well. The reset
	 * process of Axi DMA takes a while to complete as all pending
	 * commands/transfers will be flushed or completed during this
	 * reset process.
	 * Note that even though both TX and RX have their own reset register,
	 * they both reset the entire DMA core, so only one needs to be used.
	 */
	axienet_dma_out32(q, XAXIDMA_TX_CR_OFFSET, XAXIDMA_CR_RESET_MASK);
	timeout = DELAY_OF_ONE_MILLISEC;
	while (axienet_dma_in32(q, XAXIDMA_TX_CR_OFFSET) &
			XAXIDMA_CR_RESET_MASK) {
		udelay(1);
		if (--timeout == 0) {
			netdev_err(q->lp->ndev, "%s: DMA reset timeout!\n",
					__func__);
			status = axienet_dma_in32(q, XMCDMA_SR_OFFSET);
			//pr_info("Func:%s ***DEBUG*** MCDMA_STATUS_REG: 0x%x\n", __func__, status);
			break;
		}
	}
}

/**
 * axienet_device_reset - Reset and initialize the Axi Ethernet hardware.
 * @ndev:	Pointer to the net_device structure
 *
 * This function is called to reset and initialize the Axi Ethernet core. This
 * is typically called during initialization. It does a reset of the Axi DMA
 * Rx/Tx channels and initializes the Axi DMA BDs. Since Axi DMA reset lines
 * areconnected to Axi Ethernet reset lines, this in turn resets the Axi
 * Ethernet core. No separate hardware reset is done for the Axi Ethernet
 * core.
 */
static void axienet_device_reset(struct net_device *ndev)
{
#ifdef XILINX_MAC_DEBUG
	u32 axienet_status;
#endif
	struct axienet_local *lp = netdev_priv(ndev);
#ifdef XILINX_MAC_DEBUG
	u32 err, val;
#endif
	struct axienet_dma_q *q;
	u32 i;

	if (lp->axienet_config->mactype == XAXIENET_10G_25G) {
#ifdef XILINX_MAC_DEBUG
		/* Reset the XXV MAC */
		val = axienet_ior(lp, XXV_GT_RESET_OFFSET);
		val |= XXV_GT_RESET_MASK;
		axienet_iow(lp, XXV_GT_RESET_OFFSET, val);
		/* Wait for 1ms for GT reset to complete as per spec */
		mdelay(1);
		val = axienet_ior(lp, XXV_GT_RESET_OFFSET);
		val &= ~XXV_GT_RESET_MASK;
		axienet_iow(lp, XXV_GT_RESET_OFFSET, val);
#endif
	}

	if (!lp->is_tsn || lp->temac_no == XAE_TEMAC1) {
		for_each_rx_dma_queue(lp, i) {
			q = lp->dq[i];
			__axienet_device_reset(q);
#ifndef CONFIG_AXIENET_HAS_MCDMA
			__axienet_device_reset(q);
#endif
		}
	}

	lp->max_frm_size = XAE_MAX_VLAN_FRAME_SIZE;
	if (lp->axienet_config->mactype != XAXIENET_10G_25G) {
		lp->options |= XAE_OPTION_VLAN;
		lp->options &= (~XAE_OPTION_JUMBO);
	}

	if ((ndev->mtu > XAE_MTU) && (ndev->mtu <= XAE_JUMBO_MTU)) {
		lp->max_frm_size = ndev->mtu + VLAN_ETH_HLEN +
			XAE_TRL_SIZE;
		if (lp->max_frm_size <= lp->rxmem &&
				(lp->axienet_config->mactype != XAXIENET_10G_25G))
			lp->options |= XAE_OPTION_JUMBO;
	}

	if (!lp->is_tsn || lp->temac_no == XAE_TEMAC1) {
		if (axienet_dma_bd_init(ndev)) {
			netdev_err(ndev, "%s: descriptor allocation failed\n",
					__func__);
		}
	}

	if (lp->axienet_config->mactype != XAXIENET_10G_25G) {
#ifdef XILINX_MAC_DEBUG
		axienet_status = axienet_ior(lp, XAE_RCW1_OFFSET);
		axienet_status &= ~XAE_RCW1_RX_MASK;
		axienet_iow(lp, XAE_RCW1_OFFSET, axienet_status);
#endif
	}

	if (lp->axienet_config->mactype == XAXIENET_10G_25G) {
#ifdef XILINX_MAC_DEBUG
		/* Check for block lock bit got set or not
		 * This ensures that 10G ethernet IP
		 * is functioning normally or not.
		 */
		err = readl_poll_timeout(lp->regs + XXV_STATRX_BLKLCK_OFFSET,
				val, (val & XXV_RX_BLKLCK_MASK),
				10, DELAY_OF_ONE_MILLISEC);
		if (err) {
			netdev_err(ndev, "XXV MAC block lock not complete! Cross-check the MAC ref clock configuration\n");
		}
#endif
#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
		if (!lp->is_tsn) {
			axienet_rxts_iow(lp, XAXIFIFO_TXTS_RDFR,
					XAXIFIFO_TXTS_RESET_MASK);
			axienet_rxts_iow(lp, XAXIFIFO_TXTS_SRR,
					XAXIFIFO_TXTS_RESET_MASK);
			axienet_txts_iow(lp, XAXIFIFO_TXTS_RDFR,
					XAXIFIFO_TXTS_RESET_MASK);
			axienet_txts_iow(lp, XAXIFIFO_TXTS_SRR,
					XAXIFIFO_TXTS_RESET_MASK);
		}
#endif
	}
#ifdef XILINX_MAC_DEBUG
	if ((lp->axienet_config->mactype == XAXIENET_1G) &&
			!lp->eth_hasnobuf) {
		axienet_status = axienet_ior(lp, XAE_IP_OFFSET);
		if (axienet_status & XAE_INT_RXRJECT_MASK)
			axienet_iow(lp, XAE_IS_OFFSET, XAE_INT_RXRJECT_MASK);
		/* Enable receive erros */
		axienet_iow(lp, XAE_IE_OFFSET, lp->eth_irq > 0 ?
				XAE_INT_RECV_ERROR_MASK : 0);
	}

	if (lp->axienet_config->mactype == XAXIENET_10G_25G) {
		lp->options |= XAE_OPTION_FCS_STRIP;
		lp->options |= XAE_OPTION_FCS_INSERT;
	} else {
		axienet_iow(lp, XAE_FCC_OFFSET, XAE_FCC_FCRX_MASK);
	}
#endif
	lp->axienet_config->setoptions(ndev, lp->options &
			~(XAE_OPTION_TXEN | XAE_OPTION_RXEN));

	axienet_set_mac_address(ndev, NULL);
	axienet_set_multicast_list(ndev);
	lp->axienet_config->setoptions(ndev, lp->options);

	netif_trans_update(ndev);
}

/**
 * axienet_adjust_link - Adjust the PHY link speed/duplex.
 * @ndev:	Pointer to the net_device structure
 *
 * This function is called to change the speed and duplex setting after
 * auto negotiation is done by the PHY. This is the function that gets
 * registered with the PHY interface through the "of_phy_connect" call.
 */
static void axienet_adjust_link(struct net_device *ndev)
{
#ifdef XILINX_MAC_DEBUG
	u32 emmc_reg;
	u32 link_state;
	u32 setspeed = 1;
	struct axienet_local *lp = netdev_priv(ndev);
	struct phy_device *phy = ndev->phydev;

	link_state = phy->speed | (phy->duplex << 1) | phy->link;
	if (lp->last_link != link_state) {
		if ((phy->speed == SPEED_10) || (phy->speed == SPEED_100)) {
			if (lp->phy_mode == PHY_INTERFACE_MODE_1000BASEX)
				setspeed = 0;
		} else {
			if ((phy->speed == SPEED_1000) &&
					(lp->phy_mode == PHY_INTERFACE_MODE_MII))
				setspeed = 0;
		}

		if (setspeed == 1) {
			emmc_reg = axienet_ior(lp, XAE_EMMC_OFFSET);
			emmc_reg &= ~XAE_EMMC_LINKSPEED_MASK;

			switch (phy->speed) {
				case SPEED_2500:
					emmc_reg |= XAE_EMMC_LINKSPD_2500;
					break;
				case SPEED_1000:
					emmc_reg |= XAE_EMMC_LINKSPD_1000;
					break;
				case SPEED_100:
					emmc_reg |= XAE_EMMC_LINKSPD_100;
					break;
				case SPEED_10:
					emmc_reg |= XAE_EMMC_LINKSPD_10;
					break;
				default:
					dev_err(&ndev->dev, "Speed other than 10, 100 ");
					dev_err(&ndev->dev, "or 1Gbps is not supported\n");
					break;
			}

			axienet_iow(lp, XAE_EMMC_OFFSET, emmc_reg);
			phy_print_status(phy);
		} else {
			netdev_err(ndev,
					"Error setting Axi Ethernet mac speed\n");
		}

		lp->last_link = link_state;
	}
#endif
}

#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
/**
 * axienet_tx_hwtstamp - Read tx timestamp from hw and update it to the skbuff
 * @lp:		Pointer to axienet local structure
 * @cur_p:	Pointer to the axi_dma/axi_mcdma current bd
 *
 * Return:	None.
 */
#ifdef CONFIG_AXIENET_HAS_MCDMA
void axienet_tx_hwtstamp(struct axienet_local *lp,
		struct aximcdma_bd *cur_p)
#else
void axienet_tx_hwtstamp(struct axienet_local *lp,
		struct axidma_bd *cur_p)
#endif
{
	u32 sec = 0, nsec = 0, val;
	u64 time64;
	int err = 0;
	u32 count, len = lp->axienet_config->tx_ptplen;
	struct skb_shared_hwtstamps *shhwtstamps =
		skb_hwtstamps((struct sk_buff *)cur_p->ptp_tx_skb);

	val = axienet_txts_ior(lp, XAXIFIFO_TXTS_ISR);
	if (unlikely(!(val & XAXIFIFO_TXTS_INT_RC_MASK)))
		dev_info(lp->dev, "Did't get FIFO tx interrupt %d\n", val);

	/* If FIFO is configured in cut through Mode we will get Rx complete
	 * interrupt even one byte is there in the fifo wait for the full packet
	 */
	err = readl_poll_timeout_atomic(lp->tx_ts_regs + XAXIFIFO_TXTS_RLR, val,
			((val & XAXIFIFO_TXTS_RXFD_MASK) >=
			 len), 0, 1000000);
	if (err)
		netdev_err(lp->ndev, "%s: Didn't get the full timestamp packet",
				__func__);

	nsec = axienet_txts_ior(lp, XAXIFIFO_TXTS_RXFD);
	sec  = axienet_txts_ior(lp, XAXIFIFO_TXTS_RXFD);
	val = axienet_txts_ior(lp, XAXIFIFO_TXTS_RXFD);
	val = ((val & XAXIFIFO_TXTS_TAG_MASK) >> XAXIFIFO_TXTS_TAG_SHIFT);
	dev_dbg(lp->dev, "tx_stamp:[%04x] %04x %u %9u\n",
			cur_p->ptp_tx_ts_tag, val, sec, nsec);

	if (val != cur_p->ptp_tx_ts_tag) {
		count = axienet_txts_ior(lp, XAXIFIFO_TXTS_RFO);
		while (count) {
			nsec = axienet_txts_ior(lp, XAXIFIFO_TXTS_RXFD);
			sec  = axienet_txts_ior(lp, XAXIFIFO_TXTS_RXFD);
			val = axienet_txts_ior(lp, XAXIFIFO_TXTS_RXFD);
			val = ((val & XAXIFIFO_TXTS_TAG_MASK) >>
					XAXIFIFO_TXTS_TAG_SHIFT);

			dev_dbg(lp->dev, "tx_stamp:[%04x] %04x %u %9u\n",
					cur_p->ptp_tx_ts_tag, val, sec, nsec);
			if (val == cur_p->ptp_tx_ts_tag)
				break;
			count = axienet_txts_ior(lp, XAXIFIFO_TXTS_RFO);
		}
		if (val != cur_p->ptp_tx_ts_tag) {
			dev_info(lp->dev, "Mismatching 2-step tag. Got %x",
					val);
			dev_info(lp->dev, "Expected %x\n",
					cur_p->ptp_tx_ts_tag);
		}
	}

	if (lp->axienet_config->mactype != XAXIENET_10G_25G)
		val = axienet_txts_ior(lp, XAXIFIFO_TXTS_RXFD);

	time64 = sec * NS_PER_SEC + nsec;
	memset(shhwtstamps, 0, sizeof(struct skb_shared_hwtstamps));
	shhwtstamps->hwtstamp = ns_to_ktime(time64);
	if (lp->axienet_config->mactype != XAXIENET_10G_25G)
		skb_pull((struct sk_buff *)cur_p->ptp_tx_skb,
				AXIENET_TS_HEADER_LEN);

	skb_tstamp_tx((struct sk_buff *)cur_p->ptp_tx_skb, shhwtstamps);
	dev_kfree_skb_any((struct sk_buff *)cur_p->ptp_tx_skb);
	cur_p->ptp_tx_skb = 0;
}

/**
 * axienet_rx_hwtstamp - Read rx timestamp from hw and update it to the skbuff
 * @lp:		Pointer to axienet local structure
 * @skb:	Pointer to the sk_buff structure
 *
 * Return:	None.
 */
static void axienet_rx_hwtstamp(struct axienet_local *lp,
		struct sk_buff *skb)
{
	u32 sec = 0, nsec = 0, val;
	u64 time64;
	int err = 0;
	struct skb_shared_hwtstamps *shhwtstamps = skb_hwtstamps(skb);

	val = axienet_rxts_ior(lp, XAXIFIFO_TXTS_ISR);
	if (unlikely(!(val & XAXIFIFO_TXTS_INT_RC_MASK))) {
		dev_info(lp->dev, "Did't get FIFO rx interrupt %d\n", val);
		return;
	}

	val = axienet_rxts_ior(lp, XAXIFIFO_TXTS_RFO);
	if (!val)
		return;

	/* If FIFO is configured in cut through Mode we will get Rx complete
	 * interrupt even one byte is there in the fifo wait for the full packet
	 */
	err = readl_poll_timeout_atomic(lp->rx_ts_regs + XAXIFIFO_TXTS_RLR, val,
			((val & XAXIFIFO_TXTS_RXFD_MASK) >= 12),
			0, 1000000);
	if (err) {
		netdev_err(lp->ndev, "%s: Didn't get the full timestamp packet",
				__func__);
		return;
	}

	nsec = axienet_rxts_ior(lp, XAXIFIFO_TXTS_RXFD);
	sec  = axienet_rxts_ior(lp, XAXIFIFO_TXTS_RXFD);
	val = axienet_rxts_ior(lp, XAXIFIFO_TXTS_RXFD);

	if (lp->tstamp_config.rx_filter == HWTSTAMP_FILTER_ALL) {
		time64 = sec * NS_PER_SEC + nsec;
		shhwtstamps->hwtstamp = ns_to_ktime(time64);
	}
}
#endif

/**
 * axienet_start_xmit_done - Invoked once a transmit is completed by the
 * Axi DMA Tx channel.
 * @ndev:	Pointer to the net_device structure
 * @q:		Pointer to DMA queue structure
 *
 * This function is invoked from the Axi DMA Tx isr to notify the completion
 * of transmit operation. It clears fields in the corresponding Tx BDs and
 * unmaps the corresponding buffer so that CPU can regain ownership of the
 * buffer. It finally invokes "netif_wake_queue" to restart transmission if
 * required.
 */
void axienet_start_xmit_done(struct net_device *ndev,
		struct axienet_dma_q *q)
{
	u32 size = 0;
	u32 packets = 0;
	struct axienet_local *lp = netdev_priv(ndev);
	struct sk_buff *skb;

#ifdef CONFIG_AXIENET_HAS_MCDMA
	struct aximcdma_bd *cur_p;
#else
	struct axidma_bd *cur_p;
#endif
	unsigned int status = 0;

#ifdef CONFIG_AXIENET_HAS_MCDMA
	cur_p = &q->txq_bd_v[q->tx_bd_ci];
	status = cur_p->sband_stats;
#else
	cur_p = &q->tx_bd_v[q->tx_bd_ci];
	status = cur_p->status;
#endif

	//printk("%s--> chan_id: %hu\n", __func__, q->chan_id);
	while (status & XAXIDMA_BD_STS_COMPLETE_MASK) {
#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
		if (cur_p->ptp_tx_skb)
			axienet_tx_hwtstamp(lp, cur_p);
#endif
		if (cur_p->tx_desc_mapping == DESC_DMA_MAP_PAGE)
			dma_unmap_page(ndev->dev.parent, cur_p->phys,
					cur_p->cntrl &
					XAXIDMA_BD_CTRL_LENGTH_MASK,
					DMA_TO_DEVICE);
		else
			dma_unmap_single(ndev->dev.parent, cur_p->phys,
					cur_p->cntrl &
					XAXIDMA_BD_CTRL_LENGTH_MASK,
					DMA_TO_DEVICE);
		if (cur_p->tx_skb) {
			skb = ((struct sk_buff *)cur_p->tx_skb);
			//printk("Queue mapping: %hu\n", ((struct sk_buff *)cur_p->tx_skb)->queue_mapping);
			//dev_kfree_skb_irq((struct sk_buff *)cur_p->tx_skb);
			dev_kfree_skb_any(skb);
			//printk("Freed skb in %s\n", __func__);
		}
		cur_p->phys = 0;
		cur_p->app0 = 0;
		cur_p->app1 = 0;
		cur_p->app2 = 0;
		cur_p->app4 = 0;
		cur_p->status = 0;
		cur_p->tx_skb = 0;
#ifdef CONFIG_AXIENET_HAS_MCDMA
		cur_p->sband_stats = 0;
#endif

		size += status & XAXIDMA_BD_STS_ACTUAL_LEN_MASK;
		packets++;

		if (++q->tx_bd_ci >= lp->tx_bd_num)
			q->tx_bd_ci = 0;
#ifdef CONFIG_AXIENET_HAS_MCDMA
		cur_p = &q->txq_bd_v[q->tx_bd_ci];
		status = cur_p->sband_stats;
#else
		cur_p = &q->tx_bd_v[q->tx_bd_ci];
		status = cur_p->status;
#endif
	}

	ndev->stats.tx_packets += packets;
	ndev->stats.tx_bytes += size;
	q->tx_packets += packets;
	q->tx_bytes += size;

	/* Matches barrier in axienet_start_xmit */
	smp_mb();

	/* Fixme: With the existing multiqueue implementation
	 * in the driver it is difficult to get the exact queue info.
	 * We should wake only the particular queue
	 * instead of waking all ndev queues.
	 */
	netif_tx_wake_all_queues(ndev);
}

/**
 * axienet_check_tx_bd_space - Checks if a BD/group of BDs are currently busy
 * @q:		Pointer to DMA queue structure
 * @num_frag:	The number of BDs to check for
 *
 * Return: 0, on success
 *	    NETDEV_TX_BUSY, if any of the descriptors are not free
 *
 * This function is invoked before BDs are allocated and transmission starts.
 * This function returns 0 if a BD or group of BDs can be allocated for
 * transmission. If the BD or any of the BDs are not free the function
 * returns a busy status. This is invoked from axienet_start_xmit.
 */
static inline int axienet_check_tx_bd_space(struct axienet_dma_q *q,
		int num_frag)
{
	struct axienet_local *lp = q->lp;
#ifdef CONFIG_AXIENET_HAS_MCDMA
	struct aximcdma_bd *cur_p;

	if (CIRC_SPACE(q->tx_bd_tail, q->tx_bd_ci, lp->tx_bd_num) < (num_frag + 1))
		return NETDEV_TX_BUSY;

	cur_p = &q->txq_bd_v[(q->tx_bd_tail + num_frag) % lp->tx_bd_num];
	if (cur_p->sband_stats & XMCDMA_BD_STS_ALL_MASK)
		return NETDEV_TX_BUSY;
#else
	struct axidma_bd *cur_p;

	if (CIRC_SPACE(q->tx_bd_tail, q->tx_bd_ci, lp->tx_bd_num) < (num_frag + 1))
		return NETDEV_TX_BUSY;

	cur_p = &q->tx_bd_v[(q->tx_bd_tail + num_frag) % lp->tx_bd_num];
	if (cur_p->status & XAXIDMA_BD_STS_ALL_MASK)
		return NETDEV_TX_BUSY;
#endif
	return 0;
}

#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
/**
 * axienet_create_tsheader - Create timestamp header for tx
 * @q:		Pointer to DMA queue structure
 * @buf:	Pointer to the buf to copy timestamp header
 * @msg_type:	PTP message type
 *
 * Return:	None.
 */
static void axienet_create_tsheader(u8 *buf, u8 msg_type,
		struct axienet_dma_q *q)
{
	struct axienet_local *lp = q->lp;
#ifdef CONFIG_AXIENET_HAS_MCDMA
	struct aximcdma_bd *cur_p;
#else
	struct axidma_bd *cur_p;
#endif
	u64 val;
	u32 tmp;

#ifdef CONFIG_AXIENET_HAS_MCDMA
	cur_p = &q->txq_bd_v[q->tx_bd_tail];
#else
	cur_p = &q->tx_bd_v[q->tx_bd_tail];
#endif

	if (msg_type == TX_TS_OP_NOOP) {
		buf[0] = TX_TS_OP_NOOP;
	} else if (msg_type == TX_TS_OP_ONESTEP) {
		buf[0] = TX_TS_OP_ONESTEP;
		buf[1] = TX_TS_CSUM_UPDATE;
		buf[4] = TX_PTP_TS_OFFSET;
		buf[6] = TX_PTP_CSUM_OFFSET;
	} else {
		buf[0] = TX_TS_OP_TWOSTEP;
		buf[2] = cur_p->ptp_tx_ts_tag & 0xFF;
		buf[3] = (cur_p->ptp_tx_ts_tag >> 8) & 0xFF;
	}

	if (lp->axienet_config->mactype == XAXIENET_1G ||
			lp->axienet_config->mactype == XAXIENET_2_5G) {
		memcpy(&val, buf, AXIENET_TS_HEADER_LEN);
		swab64s(&val);
		memcpy(buf, &val, AXIENET_TS_HEADER_LEN);
	} else if (lp->axienet_config->mactype == XAXIENET_10G_25G) {
		memcpy(&tmp, buf, XXVENET_TS_HEADER_LEN);
		axienet_txts_iow(lp, XAXIFIFO_TXTS_TXFD, tmp);
		axienet_txts_iow(lp, XAXIFIFO_TXTS_TLR, XXVENET_TS_HEADER_LEN);
	}
}
#endif

#ifdef CONFIG_XILINX_TSN
static inline u16 get_tsn_queue(u8 pcp, u16 num_tc)
{
	u16 queue = 0;

	/* For 3 queue system, RE queue is 1 and ST queue is 2
	 * For 2 queue system, ST queue is 1. BE queue is always 0
	 */
	if (pcp == 4) {
		if (num_tc == 2)
			queue = 1;
		else
			queue = 2;
	} else if ((num_tc == 3) && (pcp == 2 || pcp == 3)) {
		queue = 1;
	}

	return queue;
}

static inline u16 tsn_queue_mapping(const struct sk_buff *skb, u16 num_tc)
{
	int queue = 0;
	u16 vlan_tci;
	u8 pcp;

	struct ethhdr *hdr = (struct ethhdr *)skb->data;
	u16 ether_type = ntohs(hdr->h_proto);

	if (unlikely(ether_type == ETH_P_8021Q)) {
		struct vlan_ethhdr *vhdr = (struct vlan_ethhdr *)skb->data;

		/* ether_type = ntohs(vhdr->h_vlan_encapsulated_proto); */

		vlan_tci = ntohs(vhdr->h_vlan_TCI);

		pcp = (vlan_tci & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;
		pr_debug("vlan_tci: %x\n", vlan_tci);
		pr_debug("pcp: %d\n", pcp);

		queue = get_tsn_queue(pcp, num_tc);
	}
	pr_debug("selected queue: %d\n", queue);
	return queue;
}
#endif

#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
static int axienet_skb_tstsmp(struct sk_buff **__skb, struct axienet_dma_q *q,
		struct net_device *ndev)
{
#ifdef CONFIG_AXIENET_HAS_MCDMA
	struct aximcdma_bd *cur_p;
#else
	struct axidma_bd *cur_p;
#endif
	struct axienet_local *lp = netdev_priv(ndev);
	struct sk_buff *old_skb = *__skb;
	struct sk_buff *skb = *__skb;

#ifdef CONFIG_AXIENET_HAS_MCDMA
	cur_p = &q->txq_bd_v[q->tx_bd_tail];
#else
	cur_p = &q->tx_bd_v[q->tx_bd_tail];
#endif

	if ((((lp->tstamp_config.tx_type == HWTSTAMP_TX_ONESTEP_SYNC) ||
					(lp->tstamp_config.tx_type == HWTSTAMP_TX_ON)) ||
				lp->eth_hasptp) && (lp->axienet_config->mactype !=
					XAXIENET_10G_25G)) {
		u8 *tmp;
		struct sk_buff *new_skb;

		if (skb_headroom(old_skb) < AXIENET_TS_HEADER_LEN) {
			new_skb =
				skb_realloc_headroom(old_skb,
						AXIENET_TS_HEADER_LEN);
			if (!new_skb) {
				dev_err(&ndev->dev, "failed to allocate new socket buffer\n");
				dev_kfree_skb_any(old_skb);
				return NETDEV_TX_BUSY;
			}

			/*  Transfer the ownership to the
			 *  new socket buffer if required
			 */
			if (old_skb->sk)
				skb_set_owner_w(new_skb, old_skb->sk);
			dev_kfree_skb_any(old_skb);
			*__skb = new_skb;
			skb = new_skb;
		}

		tmp = skb_push(skb, AXIENET_TS_HEADER_LEN);
		memset(tmp, 0, AXIENET_TS_HEADER_LEN);
		cur_p->ptp_tx_ts_tag++;

		if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
			if (lp->tstamp_config.tx_type ==
					HWTSTAMP_TX_ONESTEP_SYNC) {
				axienet_create_tsheader(tmp,
						TX_TS_OP_ONESTEP
						, q);
			} else {
				axienet_create_tsheader(tmp,
						TX_TS_OP_TWOSTEP
						, q);
				skb_shinfo(skb)->tx_flags
					|= SKBTX_IN_PROGRESS;
				cur_p->ptp_tx_skb =
					(unsigned long)skb_get(skb);
			}
		}
	} else if ((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) &&
			(lp->axienet_config->mactype == XAXIENET_10G_25G)) {
		cur_p->ptp_tx_ts_tag = (prandom_u32() &
				~XAXIFIFO_TXTS_TAG_MASK) + 1;
		dev_dbg(lp->dev, "tx_tag:[%04x]\n",
				cur_p->ptp_tx_ts_tag);
		if (lp->tstamp_config.tx_type == HWTSTAMP_TX_ONESTEP_SYNC) {
			axienet_create_tsheader(lp->tx_ptpheader,
					TX_TS_OP_ONESTEP, q);
		} else {
			axienet_create_tsheader(lp->tx_ptpheader,
					TX_TS_OP_TWOSTEP, q);
			skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
			cur_p->ptp_tx_skb = (phys_addr_t)skb_get(skb);
		}
	} else if (lp->axienet_config->mactype == XAXIENET_10G_25G) {
		dev_dbg(lp->dev, "tx_tag:NOOP\n");
		axienet_create_tsheader(lp->tx_ptpheader,
				TX_TS_OP_NOOP, q);
	}

	return NETDEV_TX_OK;
}
#endif

static int axienet_queue_xmit(struct sk_buff *skb,
		struct net_device *ndev, u16 map)
{
	u32 ii;
	u32 num_frag;
	u32 csum_start_off;
	u32 csum_index_off;
	dma_addr_t tail_p;
	struct axienet_local *lp = netdev_priv(ndev);
#ifdef CONFIG_AXIENET_HAS_MCDMA
	struct aximcdma_bd *cur_p;
#else
	struct axidma_bd *cur_p;
#endif
	unsigned long flags;
	struct axienet_dma_q *q;

	if (lp->axienet_config->mactype == XAXIENET_10G_25G) {
		/* Need to manually pad the small frames in case of XXV MAC
		 * because the pad field is not added by the IP. We must present
		 * a packet that meets the minimum length to the IP core.
		 * When the IP core is configured to calculate and add the FCS
		 * to the packet the minimum packet length is 60 bytes.
		 */
		if (eth_skb_pad(skb)) {
			ndev->stats.tx_dropped++;
			return NETDEV_TX_OK;
		}
	}

#ifdef CONFIG_XILINX_TSN
	if (unlikely(lp->is_tsn)) {
		map = tsn_queue_mapping(skb, lp->num_tc);
#ifdef CONFIG_XILINX_TSN_PTP
		const struct ethhdr *eth;

		eth = (struct ethhdr *)skb->data;
		/* check if skb is a PTP frame ? */
		if (eth->h_proto == htons(ETH_P_1588))
			return axienet_ptp_xmit(skb, ndev);
#endif
		if (lp->temac_no == XAE_TEMAC2) {
			dev_kfree_skb_any(skb);
			return NETDEV_TX_OK;
		}
	}
#endif
	num_frag = skb_shinfo(skb)->nr_frags;

	q = lp->dq[map];

#ifdef CONFIG_AXIENET_HAS_MCDMA
	cur_p = &q->txq_bd_v[q->tx_bd_tail];
#else
	cur_p = &q->tx_bd_v[q->tx_bd_tail];
#endif

	spin_lock_irqsave(&q->tx_lock, flags);
	if (axienet_check_tx_bd_space(q, num_frag)) {
		if (netif_queue_stopped(ndev)) {
			spin_unlock_irqrestore(&q->tx_lock, flags);
			return NETDEV_TX_BUSY;
		}

		netif_stop_queue(ndev);

		/* Matches barrier in axienet_start_xmit_done */
		smp_mb();

		/* Space might have just been freed - check again */
		if (axienet_check_tx_bd_space(q, num_frag)) {
			spin_unlock_irqrestore(&q->tx_lock, flags);
			return NETDEV_TX_BUSY;
		}

		netif_wake_queue(ndev);
	}

#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
	if (axienet_skb_tstsmp(&skb, q, ndev)) {
		spin_unlock_irqrestore(&q->tx_lock, flags);
		return NETDEV_TX_BUSY;
	}
#endif

	if (skb->ip_summed == CHECKSUM_PARTIAL && !lp->eth_hasnobuf &&
			(lp->axienet_config->mactype == XAXIENET_1G)) {
		if (lp->features & XAE_FEATURE_FULL_TX_CSUM) {
			/* Tx Full Checksum Offload Enabled */
			cur_p->app0 |= 2;
		} else if (lp->features & XAE_FEATURE_PARTIAL_RX_CSUM) {
			csum_start_off = skb_transport_offset(skb);
			csum_index_off = csum_start_off + skb->csum_offset;
			/* Tx Partial Checksum Offload Enabled */
			cur_p->app0 |= 1;
			cur_p->app1 = (csum_start_off << 16) | csum_index_off;
		}
	} else if (skb->ip_summed == CHECKSUM_UNNECESSARY &&
			!lp->eth_hasnobuf &&
			(lp->axienet_config->mactype == XAXIENET_1G)) {
		cur_p->app0 |= 2; /* Tx Full Checksum Offload Enabled */
	}

#ifdef CONFIG_AXIENET_HAS_MCDMA
	cur_p->cntrl = (skb_headlen(skb) | XMCDMA_BD_CTRL_TXSOF_MASK);
#else
	cur_p->cntrl = (skb_headlen(skb) | XAXIDMA_BD_CTRL_TXSOF_MASK);
#endif

	if (!q->eth_hasdre &&
			(((phys_addr_t)skb->data & 0x3) || num_frag > 0)) {
		skb_copy_and_csum_dev(skb, q->tx_buf[q->tx_bd_tail]);

		cur_p->phys = q->tx_bufs_dma +
			(q->tx_buf[q->tx_bd_tail] - q->tx_bufs);

#ifdef CONFIG_AXIENET_HAS_MCDMA
		cur_p->cntrl = skb_pagelen(skb) | XMCDMA_BD_CTRL_TXSOF_MASK;
#else
		cur_p->cntrl = skb_pagelen(skb) | XAXIDMA_BD_CTRL_TXSOF_MASK;
#endif
		goto out;
	} else {
		cur_p->phys = dma_map_single(ndev->dev.parent, skb->data,
				skb_headlen(skb), DMA_TO_DEVICE);
	}
	cur_p->tx_desc_mapping = DESC_DMA_MAP_SINGLE;

	for (ii = 0; ii < num_frag; ii++) {
		u32 len;
		skb_frag_t *frag;

		if (++q->tx_bd_tail >= lp->tx_bd_num)
			q->tx_bd_tail = 0;

#ifdef CONFIG_AXIENET_HAS_MCDMA
		cur_p = &q->txq_bd_v[q->tx_bd_tail];
#else
		cur_p = &q->tx_bd_v[q->tx_bd_tail];
#endif
		frag = &skb_shinfo(skb)->frags[ii];
		len = skb_frag_size(frag);
		cur_p->phys = skb_frag_dma_map(ndev->dev.parent, frag, 0, len,
				DMA_TO_DEVICE);
		cur_p->cntrl = len;
		cur_p->tx_desc_mapping = DESC_DMA_MAP_PAGE;
	}

out:
#ifdef CONFIG_AXIENET_HAS_MCDMA
	cur_p->cntrl |= XMCDMA_BD_CTRL_TXEOF_MASK;
	tail_p = q->tx_bd_p + sizeof(*q->txq_bd_v) * q->tx_bd_tail;
#else
	cur_p->cntrl |= XAXIDMA_BD_CTRL_TXEOF_MASK;
	tail_p = q->tx_bd_p + sizeof(*q->tx_bd_v) * q->tx_bd_tail;
#endif
	cur_p->tx_skb = (phys_addr_t)skb;
	cur_p->tx_skb = (phys_addr_t)skb;

	tail_p = q->tx_bd_p + sizeof(*q->tx_bd_v) * q->tx_bd_tail;
	/* Ensure BD write before starting transfer */
	wmb();

	/* Start the transfer */
#ifdef CONFIG_AXIENET_HAS_MCDMA
	axienet_dma_bdout(q, XMCDMA_CHAN_TAILDESC_OFFSET(q->chan_id),
			tail_p);
#else
	axienet_dma_bdout(q, XAXIDMA_TX_TDESC_OFFSET, tail_p);
#endif
	if (++q->tx_bd_tail >= lp->tx_bd_num)
		q->tx_bd_tail = 0;

	spin_unlock_irqrestore(&q->tx_lock, flags);

	return NETDEV_TX_OK;
}

/**
 * axienet_start_xmit - Starts the transmission.
 * @skb:	sk_buff pointer that contains data to be Txed.
 * @ndev:	Pointer to net_device structure.
 *
 * Return: NETDEV_TX_OK, on success
 *	    NETDEV_TX_BUSY, if any of the descriptors are not free
 *
 * This function is invoked from upper layers to initiate transmission. The
 * function uses the next available free BDs and populates their fields to
 * start the transmission. Additionally if checksum offloading is supported,
 * it populates AXI Stream Control fields with appropriate values.
 */
static int axienet_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	u16 map = skb_get_queue_mapping(skb); /* Single dma queue default*/
	return axienet_queue_xmit(skb, ndev, map);
}

/**
 * axienet_recv - Is called from Axi DMA Rx Isr to complete the received
 *		  BD processing.
 * @ndev:	Pointer to net_device structure.
 * @budget:	NAPI budget
 * @q:		Pointer to axienet DMA queue structure
 *
 * This function is invoked from the Axi DMA Rx isr(poll) to process the Rx BDs
 * It does minimal processing and invokes "netif_receive_skb" to complete
 * further processing.
 * Return: Number of BD's processed.
 */
static int axienet_recv(struct net_device *ndev, int budget,
		struct axienet_dma_q *q)
{
	u32 length;
	u32 csumstatus;
	u32 size = 0;
	u32 packets = 0;
	dma_addr_t tail_p = 0;
	struct axienet_local *lp = netdev_priv(ndev);
	struct sk_buff *skb, *new_skb;
#ifdef CONFIG_AXIENET_HAS_MCDMA
	struct aximcdma_bd *cur_p;
#else
	struct axidma_bd *cur_p;
#endif
	unsigned int numbdfree = 0;
#ifndef XILINX_MAC_DEBUG
	int ret;
#endif
	/* Get relevat BD status value */
	rmb();
#ifdef CONFIG_AXIENET_HAS_MCDMA
	cur_p = &q->rxq_bd_v[q->rx_bd_ci];
#else
	cur_p = &q->rx_bd_v[q->rx_bd_ci];
#endif
	//pr_info("Func: %s ***DEBUG**** cur_p->status : 0x%x\n", __func__, cur_p->status); 

	while ((numbdfree < budget) &&
			(cur_p->status & XAXIDMA_BD_STS_COMPLETE_MASK)) {
		new_skb = netdev_alloc_skb(ndev, lp->max_frm_size);
		if (!new_skb) {
			dev_err(lp->dev, "No memory for new_skb\n");
			break;
		}
#ifdef CONFIG_AXIENET_HAS_MCDMA
		tail_p = q->rx_bd_p + sizeof(*q->rxq_bd_v) * q->rx_bd_ci;
#else
		tail_p = q->rx_bd_p + sizeof(*q->rx_bd_v) * q->rx_bd_ci;
#endif

		dma_unmap_single(ndev->dev.parent, cur_p->phys,
				lp->max_frm_size,
				DMA_FROM_DEVICE);

		skb = (struct sk_buff *)(cur_p->sw_id_offset);

		if (lp->eth_hasnobuf ||
				(lp->axienet_config->mactype != XAXIENET_1G))
			length = cur_p->status & XAXIDMA_BD_STS_ACTUAL_LEN_MASK;
		else
			length = cur_p->app4 & 0x0000FFFF;

		skb_put(skb, length);
#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
		if (!lp->is_tsn) {
			if ((lp->tstamp_config.rx_filter == HWTSTAMP_FILTER_ALL ||
						lp->eth_hasptp) && (lp->axienet_config->mactype != XAXIENET_10G_25G)) {
				u32 sec, nsec;
				u64 time64;
				struct skb_shared_hwtstamps *shhwtstamps;

				if (lp->axienet_config->mactype == XAXIENET_1G ||
						lp->axienet_config->mactype == XAXIENET_2_5G) {
					/* The first 8 bytes will be the timestamp */
					memcpy(&sec, &skb->data[0], 4);
					memcpy(&nsec, &skb->data[4], 4);

					sec = cpu_to_be32(sec);
					nsec = cpu_to_be32(nsec);
				} else {
					/* The first 8 bytes will be the timestamp */
					memcpy(&nsec, &skb->data[0], 4);
					memcpy(&sec, &skb->data[4], 4);
				}

				/* Remove these 8 bytes from the buffer */
				skb_pull(skb, 8);
				time64 = sec * NS_PER_SEC + nsec;
				shhwtstamps = skb_hwtstamps(skb);
				shhwtstamps->hwtstamp = ns_to_ktime(time64);
			} else if (lp->axienet_config->mactype == XAXIENET_10G_25G) {
				axienet_rx_hwtstamp(lp, skb);
			}
		}
#endif

#ifndef XILINX_MAC_DEBUG

		if(q->chan_id == 3) {
			axienet_counter_packet_handler(lp, skb);

		} else {
			ret = axienet_emcdi_packet_handler(lp ,skb, q->chan_id);
			if( ret ) {
				//pr_info("%s:Not mcdi packet\n",__func__);

#endif
				skb->protocol = eth_type_trans(skb, ndev);
				/*skb_checksum_none_assert(skb);*/
				skb->ip_summed = CHECKSUM_NONE;

				/* if we're doing Rx csum offload, set it up */
				if (lp->features & XAE_FEATURE_FULL_RX_CSUM &&
						(lp->axienet_config->mactype == XAXIENET_1G) &&
						!lp->eth_hasnobuf) {
					csumstatus = (cur_p->app2 &
							XAE_FULL_CSUM_STATUS_MASK) >> 3;
					if ((csumstatus == XAE_IP_TCP_CSUM_VALIDATED) ||
							(csumstatus == XAE_IP_UDP_CSUM_VALIDATED)) {
						skb->ip_summed = CHECKSUM_UNNECESSARY;
					}
				} else if ((lp->features & XAE_FEATURE_PARTIAL_RX_CSUM) != 0 &&
						skb->protocol == htons(ETH_P_IP) &&
						skb->len > 64 && !lp->eth_hasnobuf &&
						(lp->axienet_config->mactype == XAXIENET_1G)) {
					skb->csum = be32_to_cpu(cur_p->app3 & 0xFFFF);
					skb->ip_summed = CHECKSUM_COMPLETE;
				}

				netif_receive_skb(skb);
#ifndef XILINX_MAC_DEBUG
			} else
				emcdi_packet++;
		}
#endif
		size += length;
		packets++;

		/* Ensure that the skb is completely updated
		 * prio to mapping the DMA
		 */
		wmb();

		cur_p->phys = dma_map_single(ndev->dev.parent, new_skb->data,
				lp->max_frm_size,
				DMA_FROM_DEVICE);
		cur_p->cntrl = lp->max_frm_size;
		cur_p->status = 0;
		cur_p->sw_id_offset = (phys_addr_t)new_skb;

		if (++q->rx_bd_ci >= lp->rx_bd_num)
			q->rx_bd_ci = 0;

		/* Get relevat BD status value */
		rmb();
#ifdef CONFIG_AXIENET_HAS_MCDMA
		cur_p = &q->rxq_bd_v[q->rx_bd_ci];
#else
		cur_p = &q->rx_bd_v[q->rx_bd_ci];
#endif
		numbdfree++;
	}

	ndev->stats.rx_packets += packets;
	ndev->stats.rx_bytes += size;
	q->rx_packets += packets;
	q->rx_bytes += size;

	if (tail_p) {
#ifdef CONFIG_AXIENET_HAS_MCDMA
		axienet_dma_bdout(q, XMCDMA_CHAN_TAILDESC_OFFSET(q->chan_id) +
				q->rx_offset, tail_p);
#else
		axienet_dma_bdout(q, XAXIDMA_RX_TDESC_OFFSET, tail_p);
#endif
	}

	return numbdfree;
}

/**
 * xaxienet_rx_poll - Poll routine for rx packets (NAPI)
 * @napi:	napi structure pointer
 * @quota:	Max number of rx packets to be processed.
 *
 * This is the poll routine for rx part.
 * It will process the packets maximux quota value.
 *
 * Return: number of packets received
 */
int xaxienet_rx_poll(struct napi_struct *napi, int quota)
{
	struct net_device *ndev = napi->dev;
	struct axienet_local *lp = netdev_priv(ndev);
	int work_done = 0;
	unsigned int status, cr;

	int map = napi - lp->napi;

	struct axienet_dma_q *q = lp->dq[map];
	//pr_info("Func: %s ***DEBUG**** i: %d, quota: %d\n", __func__, map, quota); 
	//pr_info("Func: %s ***DEBUG**** q->chan_id: %d, map: %d\n", __func__, q->chan_id, map); 

#ifdef CONFIG_AXIENET_HAS_MCDMA
	spin_lock(&q->rx_lock);
	status = axienet_dma_in32(q, XMCDMA_CHAN_SR_OFFSET(q->chan_id) +
			q->rx_offset);
	//pr_info("Func: %s ***DEBUG**** status: 0x%x\n", __func__, status); 

	while ((status & (XMCDMA_IRQ_IOC_MASK | XMCDMA_IRQ_DELAY_MASK)) &&
			(work_done < quota)) {
		axienet_dma_out32(q, XMCDMA_CHAN_SR_OFFSET(q->chan_id) +
				q->rx_offset, status);
		if (status & XMCDMA_IRQ_ERR_MASK) {
			dev_err(lp->dev, "Rx error 0x%x\n\r", status);
			break;
		}
		work_done += axienet_recv(lp->ndev, quota - work_done, q);
		//pr_info("Func: %s ***DEBUG**** buffer_desc freed: %d\n", __func__, work_done); 
		status = axienet_dma_in32(q, XMCDMA_CHAN_SR_OFFSET(q->chan_id) +
				q->rx_offset);
	}
	spin_unlock(&q->rx_lock);
#else
	spin_lock(&q->rx_lock);

	status = axienet_dma_in32(q, XAXIDMA_RX_SR_OFFSET);
	while ((status & (XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_DELAY_MASK)) &&
			(work_done < quota)) {
		axienet_dma_out32(q, XAXIDMA_RX_SR_OFFSET, status);
		if (status & XAXIDMA_IRQ_ERROR_MASK) {
			dev_err(lp->dev, "Rx error 0x%x\n\r", status);
			break;
		}
		work_done += axienet_recv(lp->ndev, quota - work_done, q);
		status = axienet_dma_in32(q, XAXIDMA_RX_SR_OFFSET);
	}
	spin_unlock(&q->rx_lock);
#endif

	if (work_done < quota) {
		napi_complete(napi);
#ifdef CONFIG_AXIENET_HAS_MCDMA
		/* Enable the interrupts again */
		cr = axienet_dma_in32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id) +
				XMCDMA_RX_OFFSET);
		cr |= (XMCDMA_IRQ_IOC_MASK | XMCDMA_IRQ_DELAY_MASK);
		axienet_dma_out32(q, XMCDMA_CHAN_CR_OFFSET(q->chan_id) +
				XMCDMA_RX_OFFSET, cr);
#else
		/* Enable the interrupts again */
		cr = axienet_dma_in32(q, XAXIDMA_RX_CR_OFFSET);
		cr |= (XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_DELAY_MASK);
		axienet_dma_out32(q, XAXIDMA_RX_CR_OFFSET, cr);
#endif
	}
#ifndef XILINX_MAC_DEBUG
	status = axienet_dma_in32(q, XMCDMA_SR_OFFSET);
	//pr_info("Func: %s ***DEBUG*** MCDMA_STATUS_REG: 0x%x\n", __func__, status);
#endif
	return work_done;
}

/**
 * axienet_eth_irq - Ethernet core Isr.
 * @irq:	irq number
 * @_ndev:	net_device pointer
 *
 * Return: IRQ_HANDLED if device generated a core interrupt, IRQ_NONE otherwise.
 *
 * Handle miscellaneous conditions indicated by Ethernet core IRQ.
 */
static irqreturn_t axienet_eth_irq(int irq, void *_ndev)
{
#ifdef XILINX_MAC_DEBUG
	struct net_device *ndev = _ndev;
	struct axienet_local *lp = netdev_priv(ndev);
	unsigned int pending;

	pending = axienet_ior(lp, XAE_IP_OFFSET);
	if (!pending)
		return IRQ_NONE;

	if (pending & XAE_INT_RXFIFOOVR_MASK)
		ndev->stats.rx_missed_errors++;

	if (pending & XAE_INT_RXRJECT_MASK)
		ndev->stats.rx_frame_errors++;

	axienet_iow(lp, XAE_IS_OFFSET, pending);
#endif
	return IRQ_HANDLED;
}
#ifdef XILINX_MAC_DEBUG
static int axienet_mii_init(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	int ret;

	/* Disable the MDIO interface till Axi Ethernet Reset is completed.
	 * When we do an Axi Ethernet reset, it resets the complete core
	 * including the MDIO. MDIO must be disabled before resetting
	 * and re-enabled afterwards.
	 * Hold MDIO bus lock to avoid MDIO accesses during the reset.
	 */

	mutex_lock(&lp->mii_bus->mdio_lock);
	ret = axienet_mdio_wait_until_ready(lp);
	if (ret < 0)
		return ret;
	axienet_mdio_disable(lp);
	axienet_device_reset(ndev);
	ret = axienet_mdio_enable(lp);
	ret = axienet_mdio_wait_until_ready(lp);
	mutex_unlock(&lp->mii_bus->mdio_lock);
	if (ret < 0)
		return ret;

	return 0;
}
#endif

/**
 * axienet_open - Driver open routine.
 * @ndev:	Pointer to net_device structure
 *
 * Return: 0, on success.
 *	    non-zero error value on failure
 *
 * This is the driver open routine. It calls phy_start to start the PHY device.
 * It also allocates interrupt service routines, enables the interrupt lines
 * and ISR handling. Axi Ethernet core is reset through Axi DMA core. Buffer
 * descriptors are initialized.
 */
static int axienet_open(struct net_device *ndev)
{
	int ret = 0, i = 0;
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q;
	u32 reg;
	struct phy_device *phydev = NULL;

	dev_dbg(&ndev->dev, "axienet_open()\n");

	if (lp->axienet_config->mactype == XAXIENET_10G_25G)
		axienet_device_reset(ndev);
#ifdef XILINX_MAC_DEBUG
	else
		ret = axienet_mii_init(ndev);
#endif
	if (ret < 0)
		return ret;

#ifdef XILINX_MAC_DEBUG
	if (lp->phy_node) {
		if (lp->phy_mode == XAE_PHY_TYPE_GMII) {
			phydev = of_phy_connect(lp->ndev, lp->phy_node,
					axienet_adjust_link, 0,
					PHY_INTERFACE_MODE_GMII);
		} else if (lp->phy_mode == XAE_PHY_TYPE_RGMII_2_0) {
			phydev = of_phy_connect(lp->ndev, lp->phy_node,
					axienet_adjust_link, 0,
					PHY_INTERFACE_MODE_RGMII_ID);
		} else if ((lp->axienet_config->mactype == XAXIENET_1G) ||
				(lp->axienet_config->mactype == XAXIENET_2_5G)) {
			phydev = of_phy_connect(lp->ndev, lp->phy_node,
					axienet_adjust_link,
					lp->phy_flags,
					lp->phy_interface);
		}

		if (!phydev)
			dev_err(lp->dev, "of_phy_connect() failed\n");
		else
			phy_start(phydev);
	}
#endif
	if (!lp->is_tsn || lp->temac_no == XAE_TEMAC1) {
		/* Enable tasklets for Axi DMA error handling */
		for_each_rx_dma_queue(lp, i) {
#ifdef CONFIG_AXIENET_HAS_MCDMA
			tasklet_init(&lp->dma_err_tasklet[i],
					axienet_mcdma_err_handler,
					(unsigned long)lp->dq[i]);
#else
			tasklet_init(&lp->dma_err_tasklet[i],
					axienet_dma_err_handler,
					(unsigned long)lp->dq[i]);
#endif

			/* Enable NAPI scheduling before enabling Axi DMA Rx IRQ, or you
			 * might run into a race condition; the RX ISR disables IRQ processing
			 * before scheduling the NAPI function to complete the processing.
			 * If NAPI scheduling is (still) disabled at that time, no more RX IRQs
			 * will be processed as only the NAPI function re-enables them!
			 */
			napi_enable(&lp->napi[i]);
		}
		for_each_tx_dma_queue(lp, i) {
			struct axienet_dma_q *q = lp->dq[i];
#ifdef CONFIG_AXIENET_HAS_MCDMA
			/* Enable interrupts for Axi MCDMA Tx */
			ret = request_irq(q->tx_irq, axienet_mcdma_tx_irq,
					IRQF_SHARED, ndev->name, ndev);
			//pr_info("Func: %s ***DEBUG**** request_irq_tx[%d] ret: %d\n", __func__, i, ret); 
			if (ret) {
				goto err_tx_irq;
			}
#else
			/* Enable interrupts for Axi DMA Tx */
			ret = request_irq(q->tx_irq, axienet_tx_irq,
					0, ndev->name, ndev);
			if (ret)
				goto err_tx_irq;
#endif
		}

		for_each_rx_dma_queue(lp, i) {
			struct axienet_dma_q *q = lp->dq[i];
#ifdef CONFIG_AXIENET_HAS_MCDMA
			/* Enable interrupts for Axi MCDMA Rx */
			ret = request_irq(q->rx_irq, axienet_mcdma_rx_irq,
					IRQF_SHARED, ndev->name, ndev);
			//pr_info("Func: %s ***DEBUG**** request_irq_rx[%d] ret: %d\n", __func__, i, ret); 
			if (ret) {
				goto err_rx_irq;
			}
#else
			/* Enable interrupts for Axi DMA Rx */
			ret = request_irq(q->rx_irq, axienet_rx_irq,
					0, ndev->name, ndev);
			if (ret)
				goto err_rx_irq;
#endif
		}
	}
#ifdef XILINX_MAC_DEBUG
#ifdef CONFIG_XILINX_TSN_PTP
	if (lp->is_tsn) {
		INIT_WORK(&lp->tx_tstamp_work, axienet_tx_tstamp);
		skb_queue_head_init(&lp->ptp_txq);

		lp->ptp_rx_hw_pointer = 0;
		lp->ptp_rx_sw_pointer = 0xff;

		axienet_iow(lp, PTP_RX_CONTROL_OFFSET, PTP_RX_PACKET_CLEAR);

		ret = request_irq(lp->ptp_rx_irq, axienet_ptp_rx_irq,
				0, "ptp_rx", ndev);
		if (ret)
			goto err_ptp_rx_irq;

		ret = request_irq(lp->ptp_tx_irq, axienet_ptp_tx_irq,
				0, "ptp_tx", ndev);
		if (ret)
			goto err_ptp_rx_irq;
	}
#endif

	if (lp->phy_mode == XXE_PHY_TYPE_USXGMII) {
		netdev_dbg(ndev, "RX reg: 0x%x\n",
				axienet_ior(lp, XXV_RCW1_OFFSET));
		/* USXGMII setup at selected speed */
		reg = axienet_ior(lp, XXV_USXGMII_AN_OFFSET);
		reg &= ~USXGMII_RATE_MASK;
		netdev_dbg(ndev, "usxgmii_rate %d\n", lp->usxgmii_rate);
		switch (lp->usxgmii_rate) {
			case SPEED_1000:
				reg |= USXGMII_RATE_1G;
				break;
			case SPEED_2500:
				reg |= USXGMII_RATE_2G5;
				break;
			case SPEED_10:
				reg |= USXGMII_RATE_10M;
				break;
			case SPEED_100:
				reg |= USXGMII_RATE_100M;
				break;
			case SPEED_5000:
				reg |= USXGMII_RATE_5G;
				break;
			case SPEED_10000:
				reg |= USXGMII_RATE_10G;
				break;
			default:
				reg |= USXGMII_RATE_1G;
		}
		reg |= USXGMII_FD;
		reg |= (USXGMII_EN | USXGMII_LINK_STS);
		axienet_iow(lp, XXV_USXGMII_AN_OFFSET, reg);
		reg |= USXGMII_AN_EN;
		axienet_iow(lp, XXV_USXGMII_AN_OFFSET, reg);
		/* AN Restart bit should be reset, set and then reset as per
		 * spec with a 1 ms delay for a raising edge trigger
		 */
		axienet_iow(lp, XXV_USXGMII_AN_OFFSET,
				reg & ~USXGMII_AN_RESTART);
		mdelay(1);
		axienet_iow(lp, XXV_USXGMII_AN_OFFSET,
				reg | USXGMII_AN_RESTART);
		mdelay(1);
		axienet_iow(lp, XXV_USXGMII_AN_OFFSET,
				reg & ~USXGMII_AN_RESTART);

		/* Check block lock bit to make sure RX path is ok with
		 * USXGMII initialization.
		 */
		err = readl_poll_timeout(lp->regs + XXV_STATRX_BLKLCK_OFFSET,
				reg, (reg & XXV_RX_BLKLCK_MASK),
				100, DELAY_OF_ONE_MILLISEC);
		if (err) {
			netdev_err(ndev, "%s: USXGMII Block lock bit not set",
					__func__);
			ret = -ENODEV;
			goto err_eth_irq;
		}

		err = readl_poll_timeout(lp->regs + XXV_USXGMII_AN_STS_OFFSET,
				reg, (reg & USXGMII_AN_STS_COMP_MASK),
				1000000, DELAY_OF_ONE_MILLISEC);
		if (err) {
			netdev_err(ndev, "%s: USXGMII AN not complete",
					__func__);
			ret = -ENODEV;
			goto err_eth_irq;
		}

		netdev_info(ndev, "USXGMII setup at %d\n", lp->usxgmii_rate);
	}

#endif
	/* Enable interrupts for Axi Ethernet core (if defined) */
	if (!lp->eth_hasnobuf && (lp->axienet_config->mactype == XAXIENET_1G)) {
		ret = request_irq(lp->eth_irq, axienet_eth_irq, IRQF_SHARED,
				ndev->name, ndev);
		if (ret)
			goto err_eth_irq;
	}

	netif_tx_start_all_queues(ndev);
	return 0;

err_eth_irq:
	while (i--) {
		q = lp->dq[i];
		free_irq(q->rx_irq, ndev);
	}
	i = lp->num_tx_queues;
err_rx_irq:
	while (i--) {
		q = lp->dq[i];
		free_irq(q->tx_irq, ndev);
	}
err_tx_irq:
	for_each_rx_dma_queue(lp, i)
		napi_disable(&lp->napi[i]);
#ifdef XILINX_MAC_DEBUG
	if (phydev)
		phy_disconnect(phydev);
	phydev = NULL;
#endif
#ifdef CONFIG_XILINX_TSN_PTP
err_ptp_rx_irq:
#endif
	for_each_rx_dma_queue(lp, i)
		tasklet_kill(&lp->dma_err_tasklet[i]);
	dev_err(lp->dev, "request_irq() failed\n");
	return ret;
}

/**
 * axienet_stop - Driver stop routine.
 * @ndev:	Pointer to net_device structure
 *
 * Return: 0, on success.
 *
 * This is the driver stop routine. It calls phy_disconnect to stop the PHY
 * device. It also removes the interrupt handlers and disables the interrupts.
 * The Axi DMA Tx/Rx BDs are released.
 */
static int axienet_stop(struct net_device *ndev)
{
	u32 cr, sr;
	int count;
	u32 i;
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q;

	dev_dbg(&ndev->dev, "axienet_close()\n");

	lp->axienet_config->setoptions(ndev, lp->options &
			~(XAE_OPTION_TXEN | XAE_OPTION_RXEN));

	if (!lp->is_tsn || lp->temac_no == XAE_TEMAC1) {
		for_each_tx_dma_queue(lp, i) {
			q = lp->dq[i];
			cr = axienet_dma_in32(q, XAXIDMA_RX_CR_OFFSET);
			cr &= ~(XAXIDMA_CR_RUNSTOP_MASK | XAXIDMA_IRQ_ALL_MASK);
			axienet_dma_out32(q, XAXIDMA_RX_CR_OFFSET, cr);

			cr = axienet_dma_in32(q, XAXIDMA_TX_CR_OFFSET);
			cr &= ~(XAXIDMA_CR_RUNSTOP_MASK | XAXIDMA_IRQ_ALL_MASK);
			axienet_dma_out32(q, XAXIDMA_TX_CR_OFFSET, cr);
#ifdef XILINX_MAC_ENABLE
			axienet_iow(lp, XAE_IE_OFFSET, 0);
#endif
			/* Give DMAs a chance to halt gracefully */
			sr = axienet_dma_in32(q, XAXIDMA_RX_SR_OFFSET);
			for (count = 0; !(sr & XAXIDMA_SR_HALT_MASK) && count < 5; ++count) {
				msleep(20);
				sr = axienet_dma_in32(q, XAXIDMA_RX_SR_OFFSET);
			}

			sr = axienet_dma_in32(q, XAXIDMA_TX_SR_OFFSET);
			for (count = 0; !(sr & XAXIDMA_SR_HALT_MASK) && count < 5; ++count) {
				msleep(20);
				sr = axienet_dma_in32(q, XAXIDMA_TX_SR_OFFSET);
			}

#ifdef XILINX_MAC_ENABLE
			/* Do a reset to ensure DMA is really stopped */
			if (lp->axienet_config->mactype != XAXIENET_10G_25G) {
				mutex_lock(&lp->mii_bus->mdio_lock);
				axienet_mdio_disable(lp);
			}
#endif
			__axienet_device_reset(q);

#ifdef XILINX_MAC_ENABLE
			if (lp->axienet_config->mactype != XAXIENET_10G_25G) {
				axienet_mdio_enable(lp);
				mutex_unlock(&lp->mii_bus->mdio_lock);
			}
#endif
			free_irq(q->tx_irq, ndev);
		}

		for_each_rx_dma_queue(lp, i) {
			q = lp->dq[i];
			netif_stop_queue(ndev);
			napi_disable(&lp->napi[i]);
			tasklet_kill(&lp->dma_err_tasklet[i]);
			free_irq(q->rx_irq, ndev);
		}
#ifdef CONFIG_XILINX_TSN_PTP
		if (lp->is_tsn) {
			free_irq(lp->ptp_tx_irq, ndev);
			free_irq(lp->ptp_rx_irq, ndev);
		}
#endif
		if ((lp->axienet_config->mactype == XAXIENET_1G) && !lp->eth_hasnobuf)
			free_irq(lp->eth_irq, ndev);

#ifdef XILINX_MAC_ENABLE
		if (ndev->phydev)
			phy_disconnect(ndev->phydev);
#endif
		if (lp->temac_no != XAE_TEMAC2)
			axienet_dma_bd_release(ndev);
	}
	return 0;
}

/**
 * axienet_change_mtu - Driver change mtu routine.
 * @ndev:	Pointer to net_device structure
 * @new_mtu:	New mtu value to be applied
 *
 * Return: Always returns 0 (success).
 *
 * This is the change mtu driver routine. It checks if the Axi Ethernet
 * hardware supports jumbo frames before changing the mtu. This can be
 * called only when the device is not up.
 */
static int axienet_change_mtu(struct net_device *ndev, int new_mtu)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (netif_running(ndev))
		return -EBUSY;

	if ((new_mtu + VLAN_ETH_HLEN +
				XAE_TRL_SIZE) > lp->rxmem)
		return -EINVAL;

	ndev->mtu = new_mtu;

	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/**
 * axienet_poll_controller - Axi Ethernet poll mechanism.
 * @ndev:	Pointer to net_device structure
 *
 * This implements Rx/Tx ISR poll mechanisms. The interrupts are disabled prior
 * to polling the ISRs and are enabled back after the polling is done.
 */
static void axienet_poll_controller(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	int i, ret;

	//pr_info("Func: %s ***DEBUG****\n", __func__); 
	for_each_tx_dma_queue(lp, i)
		disable_irq(lp->dq[i]->tx_irq);
	for_each_rx_dma_queue(lp, i)
		disable_irq(lp->dq[i]->rx_irq);

	for_each_rx_dma_queue(lp, i)
#ifdef CONFIG_AXIENET_HAS_MCDMA
		ret = axienet_mcdma_rx_irq(lp->dq[i]->rx_irq, ndev);
	//pr_info("Func: %s ***DEBUG**** irq_return: %d\n", __func__, ret); 
#else
	axienet_rx_irq(lp->dq[i]->rx_irq, ndev);
#endif
	for_each_tx_dma_queue(lp, i)
#ifdef CONFIG_AXIENET_HAS_MCDMA
		axienet_mcdma_tx_irq(lp->dq[i]->tx_irq, ndev);
	//pr_info("Func: %s ***DEBUG**** irq_return: %d\n", __func__, ret); 
#else
	axienet_tx_irq(lp->dq[i]->tx_irq, ndev);
#endif
	for_each_tx_dma_queue(lp, i)
		enable_irq(lp->dq[i]->tx_irq);
	for_each_rx_dma_queue(lp, i)
		enable_irq(lp->dq[i]->rx_irq);
}
#endif

#if defined(CONFIG_XILINX_AXI_EMAC_HWTSTAMP) || defined(CONFIG_XILINX_TSN_PTP)
/**
 *  axienet_set_timestamp_mode - sets up the hardware for the requested mode
 *  @lp: Pointer to axienet local structure
 *  @config: the hwtstamp configuration requested
 *
 * Return: 0 on success, Negative value on errors
 */
static int axienet_set_timestamp_mode(struct axienet_local *lp,
		struct hwtstamp_config *config)
{
	u32 regval;

#ifdef CONFIG_XILINX_TSN_PTP
	if (lp->is_tsn) {
		/* reserved for future extensions */
		if (config->flags)
			return -EINVAL;

		if (config->tx_type < HWTSTAMP_TX_OFF ||
				config->tx_type > HWTSTAMP_TX_ONESTEP_SYNC)
			return -ERANGE;

		lp->ptp_ts_type = config->tx_type;

		/* On RX always timestamp everything */
		switch (config->rx_filter) {
			case HWTSTAMP_FILTER_NONE:
				break;
			default:
				config->rx_filter = HWTSTAMP_FILTER_ALL;
		}
		return 0;
	}
#endif

	/* reserved for future extensions */
	if (config->flags)
		return -EINVAL;

	/* Read the current value in the MAC TX CTRL register */
	regval = axienet_ior(lp, XAE_TC_OFFSET);

	switch (config->tx_type) {
		case HWTSTAMP_TX_OFF:
			regval &= ~XAE_TC_INBAND1588_MASK;
			break;
		case HWTSTAMP_TX_ON:
			config->tx_type = HWTSTAMP_TX_ON;
			regval |= XAE_TC_INBAND1588_MASK;
			break;
		case HWTSTAMP_TX_ONESTEP_SYNC:
			config->tx_type = HWTSTAMP_TX_ONESTEP_SYNC;
			regval |= XAE_TC_INBAND1588_MASK;
			break;
		default:
			return -ERANGE;
	}

	if (lp->axienet_config->mactype != XAXIENET_10G_25G)
		axienet_iow(lp, XAE_TC_OFFSET, regval);

	/* Read the current value in the MAC RX RCW1 register */
	regval = axienet_ior(lp, XAE_RCW1_OFFSET);

	/* On RX always timestamp everything */
	switch (config->rx_filter) {
		case HWTSTAMP_FILTER_NONE:
			regval &= ~XAE_RCW1_INBAND1588_MASK;
			break;
		default:
			config->rx_filter = HWTSTAMP_FILTER_ALL;
			regval |= XAE_RCW1_INBAND1588_MASK;
	}

	if (lp->axienet_config->mactype != XAXIENET_10G_25G)
		axienet_iow(lp, XAE_RCW1_OFFSET, regval);

	return 0;
}

/**
 * axienet_set_ts_config - user entry point for timestamp mode
 * @lp: Pointer to axienet local structure
 * @ifr: ioctl data
 *
 * Set hardware to the requested more. If unsupported return an error
 * with no changes. Otherwise, store the mode for future reference
 *
 * Return: 0 on success, Negative value on errors
 */
static int axienet_set_ts_config(struct axienet_local *lp, struct ifreq *ifr)
{
	struct hwtstamp_config config;
	int err;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	err = axienet_set_timestamp_mode(lp, &config);
	if (err)
		return err;

	/* save these settings for future reference */
	memcpy(&lp->tstamp_config, &config, sizeof(lp->tstamp_config));

	return copy_to_user(ifr->ifr_data, &config,
			sizeof(config)) ? -EFAULT : 0;
}

/**
 * axienet_get_ts_config - return the current timestamp configuration
 * to the user
 * @lp: pointer to axienet local structure
 * @ifr: ioctl data
 *
 * Return: 0 on success, Negative value on errors
 */
static int axienet_get_ts_config(struct axienet_local *lp, struct ifreq *ifr)
{
	struct hwtstamp_config *config = &lp->tstamp_config;

	return copy_to_user(ifr->ifr_data, config,
			sizeof(*config)) ? -EFAULT : 0;
}
#endif

#ifdef XILINX_MAC_DEBUG
/* Ioctl MII Interface */
static int axienet_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
#if defined(CONFIG_XILINX_AXI_EMAC_HWTSTAMP) || defined(CONFIG_XILINX_TSN_PTP)
	struct axienet_local *lp = netdev_priv(dev);
#endif

	if (!netif_running(dev))
		return -EINVAL;

	switch (cmd) {
		case SIOCGMIIPHY:
		case SIOCGMIIREG:
		case SIOCSMIIREG:
			if (!dev->phydev)
				return -EOPNOTSUPP;
			return phy_mii_ioctl(dev->phydev, rq, cmd);
#if defined(CONFIG_XILINX_AXI_EMAC_HWTSTAMP) || defined(CONFIG_XILINX_TSN_PTP)
		case SIOCSHWTSTAMP:
			return axienet_set_ts_config(lp, rq);
		case SIOCGHWTSTAMP:
			return axienet_get_ts_config(lp, rq);
#endif
#ifdef CONFIG_XILINX_TSN_QBV
		case SIOCCHIOCTL:
			return axienet_set_schedule(dev, rq->ifr_data);
		case SIOC_GET_SCHED:
			return axienet_get_schedule(dev, rq->ifr_data);
#endif
#ifdef CONFIG_XILINX_TSN_QBR
		case SIOC_PREEMPTION_CFG:
			return axienet_preemption(dev, rq->ifr_data);
		case SIOC_PREEMPTION_CTRL:
			return axienet_preemption_ctrl(dev, rq->ifr_data);
		case SIOC_PREEMPTION_STS:
			return axienet_preemption_sts(dev, rq->ifr_data);
		case SIOC_PREEMPTION_COUNTER:
			return axienet_preemption_cnt(dev, rq->ifr_data);
#ifdef CONFIG_XILINX_TSN_QBV
		case SIOC_QBU_USER_OVERRIDE:
			return axienet_qbu_user_override(dev, rq->ifr_data);
		case SIOC_QBU_STS:
			return axienet_qbu_sts(dev, rq->ifr_data);
#endif
#endif

		default:
			return -EOPNOTSUPP;
	}
}
#endif

static const struct net_device_ops axienet_netdev_ops = {
	.ndo_open = axienet_open,
	.ndo_stop = axienet_stop,
	.ndo_start_xmit = axienet_start_xmit,
	.ndo_change_mtu	= axienet_change_mtu,
	.ndo_set_mac_address = netdev_set_mac_address,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_set_rx_mode = axienet_set_multicast_list,
#ifdef XILINX_MAC_DEBUG
	.ndo_do_ioctl = axienet_ioctl,
#endif
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = axienet_poll_controller,
#endif
};
#ifdef XILINX_MAC_DEBUG
/**
 * axienet_ethtools_get_drvinfo - Get various Axi Ethernet driver information.
 * @ndev:	Pointer to net_device structure
 * @ed:		Pointer to ethtool_drvinfo structure
 *
 * This implements ethtool command for getting the driver information.
 * Issue "ethtool -i ethX" under linux prompt to execute this function.
 */
static void axienet_ethtools_get_drvinfo(struct net_device *ndev,
		struct ethtool_drvinfo *ed)
{
	strlcpy(ed->driver, DRIVER_NAME, sizeof(ed->driver));
	strlcpy(ed->version, DRIVER_VERSION, sizeof(ed->version));
}

/**
 * axienet_ethtools_get_regs_len - Get the total regs length present in the
 *				   AxiEthernet core.
 * @ndev:	Pointer to net_device structure
 *
 * This implements ethtool command for getting the total register length
 * information.
 *
 * Return: the total regs length
 */
static int axienet_ethtools_get_regs_len(struct net_device *ndev)
{
	return sizeof(u32) * AXIENET_REGS_N;
}

/**
 * axienet_ethtools_get_regs - Dump the contents of all registers present
 *			       in AxiEthernet core.
 * @ndev:	Pointer to net_device structure
 * @regs:	Pointer to ethtool_regs structure
 * @ret:	Void pointer used to return the contents of the registers.
 *
 * This implements ethtool command for getting the Axi Ethernet register dump.
 * Issue "ethtool -d ethX" to execute this function.
 */
static void axienet_ethtools_get_regs(struct net_device *ndev,
		struct ethtool_regs *regs, void *ret)
{
	u32 *data = (u32 *)ret;
	size_t len = sizeof(u32) * AXIENET_REGS_N;
#ifdef XILINX_MAC_DEBUG
	struct axienet_local *lp = netdev_priv(ndev);
#endif
	regs->version = 0;
	regs->len = len;

	memset(data, 0, len);
	data[0] = axienet_ior(lp, XAE_RAF_OFFSET);
	data[1] = axienet_ior(lp, XAE_TPF_OFFSET);
	data[2] = axienet_ior(lp, XAE_IFGP_OFFSET);
	data[3] = axienet_ior(lp, XAE_IS_OFFSET);
	data[4] = axienet_ior(lp, XAE_IP_OFFSET);
	data[5] = axienet_ior(lp, XAE_IE_OFFSET);
	data[6] = axienet_ior(lp, XAE_TTAG_OFFSET);
	data[7] = axienet_ior(lp, XAE_RTAG_OFFSET);
	data[8] = axienet_ior(lp, XAE_UAWL_OFFSET);
	data[9] = axienet_ior(lp, XAE_UAWU_OFFSET);
	data[10] = axienet_ior(lp, XAE_TPID0_OFFSET);
	data[11] = axienet_ior(lp, XAE_TPID1_OFFSET);
	data[12] = axienet_ior(lp, XAE_PPST_OFFSET);
	data[13] = axienet_ior(lp, XAE_RCW0_OFFSET);
	data[14] = axienet_ior(lp, XAE_RCW1_OFFSET);
	data[15] = axienet_ior(lp, XAE_TC_OFFSET);
	data[16] = axienet_ior(lp, XAE_FCC_OFFSET);
	data[17] = axienet_ior(lp, XAE_EMMC_OFFSET);
	data[18] = axienet_ior(lp, XAE_RMFC_OFFSET);
	data[19] = axienet_ior(lp, XAE_MDIO_MC_OFFSET);
	data[20] = axienet_ior(lp, XAE_MDIO_MCR_OFFSET);
	data[21] = axienet_ior(lp, XAE_MDIO_MWD_OFFSET);
	data[22] = axienet_ior(lp, XAE_MDIO_MRD_OFFSET);
	data[23] = axienet_ior(lp, XAE_TEMAC_IS_OFFSET);
	data[24] = axienet_ior(lp, XAE_TEMAC_IP_OFFSET);
	data[25] = axienet_ior(lp, XAE_TEMAC_IE_OFFSET);
	data[26] = axienet_ior(lp, XAE_TEMAC_IC_OFFSET);
	data[27] = axienet_ior(lp, XAE_UAW0_OFFSET);
	data[28] = axienet_ior(lp, XAE_UAW1_OFFSET);
	data[29] = axienet_ior(lp, XAE_FMC_OFFSET);
	data[30] = axienet_ior(lp, XAE_AF0_OFFSET);
	data[31] = axienet_ior(lp, XAE_AF1_OFFSET);
	/* Support only single DMA queue */
	data[32] = axienet_dma_in32(lp->dq[0], XAXIDMA_TX_CR_OFFSET);
	data[33] = axienet_dma_in32(lp->dq[0], XAXIDMA_TX_SR_OFFSET);
	data[34] = axienet_dma_in32(lp->dq[0], XAXIDMA_TX_CDESC_OFFSET);
	data[35] = axienet_dma_in32(lp->dq[0], XAXIDMA_TX_TDESC_OFFSET);
	data[36] = axienet_dma_in32(lp->dq[0], XAXIDMA_RX_CR_OFFSET);
	data[37] = axienet_dma_in32(lp->dq[0], XAXIDMA_RX_SR_OFFSET);
	data[38] = axienet_dma_in32(lp->dq[0], XAXIDMA_RX_CDESC_OFFSET);
	data[39] = axienet_dma_in32(lp->dq[0], XAXIDMA_RX_TDESC_OFFSET);
}

static void axienet_ethtools_get_ringparam(struct net_device *ndev,
		struct ethtool_ringparam *ering)
{
	struct axienet_local *lp = netdev_priv(ndev);

	ering->rx_max_pending = RX_BD_NUM_MAX;
	ering->rx_mini_max_pending = 0;
	ering->rx_jumbo_max_pending = 0;
	ering->tx_max_pending = TX_BD_NUM_MAX;
	ering->rx_pending = lp->rx_bd_num;
	ering->rx_mini_pending = 0;
	ering->rx_jumbo_pending = 0;
	ering->tx_pending = lp->tx_bd_num;
}

static int axienet_ethtools_set_ringparam(struct net_device *ndev,
		struct ethtool_ringparam *ering)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (ering->rx_pending > RX_BD_NUM_MAX ||
			ering->rx_mini_pending ||
			ering->rx_jumbo_pending ||
			ering->rx_pending > TX_BD_NUM_MAX)
		return -EINVAL;

	if (netif_running(ndev))
		return -EBUSY;

	lp->rx_bd_num = ering->rx_pending;
	lp->tx_bd_num = ering->tx_pending;
	return 0;
}

/**
 * axienet_ethtools_get_pauseparam - Get the pause parameter setting for
 *				     Tx and Rx paths.
 * @ndev:	Pointer to net_device structure
 * @epauseparm:	Pointer to ethtool_pauseparam structure.
 *
 * This implements ethtool command for getting axi ethernet pause frame
 * setting. Issue "ethtool -a ethX" to execute this function.
 */
	static void
axienet_ethtools_get_pauseparam(struct net_device *ndev,
		struct ethtool_pauseparam *epauseparm)
{
	u32 regval;
	struct axienet_local *lp = netdev_priv(ndev);

	epauseparm->autoneg  = 0;
	regval = axienet_ior(lp, XAE_FCC_OFFSET);
	epauseparm->tx_pause = regval & XAE_FCC_FCTX_MASK;
	epauseparm->rx_pause = regval & XAE_FCC_FCRX_MASK;
}

/**
 * axienet_ethtools_set_pauseparam - Set device pause parameter(flow control)
 *				     settings.
 * @ndev:	Pointer to net_device structure
 * @epauseparm:	Pointer to ethtool_pauseparam structure
 *
 * This implements ethtool command for enabling flow control on Rx and Tx
 * paths. Issue "ethtool -A ethX tx on|off" under linux prompt to execute this
 * function.
 *
 * Return: 0 on success, -EFAULT if device is running
 */
	static int
axienet_ethtools_set_pauseparam(struct net_device *ndev,
		struct ethtool_pauseparam *epauseparm)
{
	u32 regval = 0;
	struct axienet_local *lp = netdev_priv(ndev);

	if (netif_running(ndev)) {
		netdev_err(ndev,
				"Please stop netif before applying configuration\n");
		return -EFAULT;
	}

	regval = axienet_ior(lp, XAE_FCC_OFFSET);
	if (epauseparm->tx_pause)
		regval |= XAE_FCC_FCTX_MASK;
	else
		regval &= ~XAE_FCC_FCTX_MASK;
	if (epauseparm->rx_pause)
		regval |= XAE_FCC_FCRX_MASK;
	else
		regval &= ~XAE_FCC_FCRX_MASK;
	axienet_iow(lp, XAE_FCC_OFFSET, regval);

	return 0;
}

/**
 * axienet_ethtools_get_coalesce - Get DMA interrupt coalescing count.
 * @ndev:	Pointer to net_device structure
 * @ecoalesce:	Pointer to ethtool_coalesce structure
 *
 * This implements ethtool command for getting the DMA interrupt coalescing
 * count on Tx and Rx paths. Issue "ethtool -c ethX" under linux prompt to
 * execute this function.
 *
 * Return: 0 always
 */
static int axienet_ethtools_get_coalesce(struct net_device *ndev,
		struct ethtool_coalesce *ecoalesce)
{
	u32 regval = 0;
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q;
	int i;

	for_each_rx_dma_queue(lp, i) {
		q = lp->dq[i];

		regval = axienet_dma_in32(q, XAXIDMA_RX_CR_OFFSET);
		ecoalesce->rx_max_coalesced_frames +=
			(regval & XAXIDMA_COALESCE_MASK)
			>> XAXIDMA_COALESCE_SHIFT;
	}
	for_each_tx_dma_queue(lp, i) {
		q = lp->dq[i];
		regval = axienet_dma_in32(q, XAXIDMA_TX_CR_OFFSET);
		ecoalesce->tx_max_coalesced_frames +=
			(regval & XAXIDMA_COALESCE_MASK)
			>> XAXIDMA_COALESCE_SHIFT;
	}
	return 0;
}

/**
 * axienet_ethtools_set_coalesce - Set DMA interrupt coalescing count.
 * @ndev:	Pointer to net_device structure
 * @ecoalesce:	Pointer to ethtool_coalesce structure
 *
 * This implements ethtool command for setting the DMA interrupt coalescing
 * count on Tx and Rx paths. Issue "ethtool -C ethX rx-frames 5" under linux
 * prompt to execute this function.
 *
 * Return: 0, on success, Non-zero error value on failure.
 */
static int axienet_ethtools_set_coalesce(struct net_device *ndev,
		struct ethtool_coalesce *ecoalesce)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (netif_running(ndev)) {
		netdev_err(ndev,
				"Please stop netif before applying configuration\n");
		return -EFAULT;
	}

	if ((ecoalesce->rx_coalesce_usecs) ||
			(ecoalesce->rx_coalesce_usecs_irq) ||
			(ecoalesce->rx_max_coalesced_frames_irq) ||
			(ecoalesce->tx_coalesce_usecs) ||
			(ecoalesce->tx_coalesce_usecs_irq) ||
			(ecoalesce->tx_max_coalesced_frames_irq) ||
			(ecoalesce->stats_block_coalesce_usecs) ||
			(ecoalesce->use_adaptive_rx_coalesce) ||
			(ecoalesce->use_adaptive_tx_coalesce) ||
			(ecoalesce->pkt_rate_low) ||
			(ecoalesce->rx_coalesce_usecs_low) ||
			(ecoalesce->rx_max_coalesced_frames_low) ||
			(ecoalesce->tx_coalesce_usecs_low) ||
			(ecoalesce->tx_max_coalesced_frames_low) ||
			(ecoalesce->pkt_rate_high) ||
			(ecoalesce->rx_coalesce_usecs_high) ||
			(ecoalesce->rx_max_coalesced_frames_high) ||
			(ecoalesce->tx_coalesce_usecs_high) ||
			(ecoalesce->tx_max_coalesced_frames_high) ||
			(ecoalesce->rate_sample_interval))
		return -EOPNOTSUPP;
	if (ecoalesce->rx_max_coalesced_frames)
		lp->coalesce_count_rx = ecoalesce->rx_max_coalesced_frames;
	if (ecoalesce->tx_max_coalesced_frames)
		lp->coalesce_count_tx = ecoalesce->tx_max_coalesced_frames;

	return 0;
}

#if defined(CONFIG_XILINX_AXI_EMAC_HWTSTAMP) || defined(CONFIG_XILINX_TSN_PTP)
/**
 * axienet_ethtools_get_ts_info - Get h/w timestamping capabilities.
 * @ndev:	Pointer to net_device structure
 * @info:	Pointer to ethtool_ts_info structure
 *
 * Return: 0, on success, Non-zero error value on failure.
 */
static int axienet_ethtools_get_ts_info(struct net_device *ndev,
		struct ethtool_ts_info *info)
{
	info->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;
	info->tx_types = (1 << HWTSTAMP_TX_OFF) | (1 << HWTSTAMP_TX_ON);
	info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
		(1 << HWTSTAMP_FILTER_ALL);
	info->phc_index = 0;

#ifdef CONFIG_XILINX_TSN_PTP
	info->phc_index = axienet_phc_index;
#endif
	return 0;
}
#endif

static const struct ethtool_ops axienet_ethtool_ops = {
	.get_drvinfo    = axienet_ethtools_get_drvinfo,
	.get_regs_len   = axienet_ethtools_get_regs_len,
	.get_regs       = axienet_ethtools_get_regs,
	.get_link       = ethtool_op_get_link,
	.get_ringparam	= axienet_ethtools_get_ringparam,
	.set_ringparam  = axienet_ethtools_set_ringparam,
	.get_pauseparam = axienet_ethtools_get_pauseparam,
	.set_pauseparam = axienet_ethtools_set_pauseparam,
	.get_coalesce   = axienet_ethtools_get_coalesce,
	.set_coalesce   = axienet_ethtools_set_coalesce,
#if defined(CONFIG_XILINX_AXI_EMAC_HWTSTAMP) || defined(CONFIG_XILINX_TSN_PTP)
	.get_ts_info    = axienet_ethtools_get_ts_info,
#endif
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
#ifdef CONFIG_AXIENET_HAS_MCDMA
	.get_sset_count	 = axienet_sset_count,
	.get_ethtool_stats = axienet_get_stats,
	.get_strings = axienet_strings,
#endif
};
#endif

#ifdef CONFIG_AXIENET_HAS_MCDMA
#ifndef XILINX_MAC_DEBUG
#if 0
static int __maybe_unused axienet_sdnet_probe(struct platform_device *pdev,
		struct axienet_local *lp)
{
	int i, ret = 0;
	struct device_node *node, *parent;
	struct platform_device *sd_pdev;
	pr_info("Func: %s ***ASH_DEBUG*** SDNET resource mapping\n", __func__, pdev->name);
	parent = of_get_parent(pdev->dev.of_node);        
	if (!parent) {
		pr_info("Func: %s ***ASH_DEBUG*** No parent node!!\n", __func__);
		return PTR_ERR(parent);
	} else
		pr_info("Func: %s ***ASH_DEBUG*** Parent node: %s \n", __func__, parent->name);
	lp->sdres = kmalloc(sizeof(struct resource), GFP_KERNEL);   
	node = of_get_child_by_name(parent, "sdnet");
	if (IS_ERR(node)) { 
		pr_info("Func: %s ***ASH_DEBUG*** sdnet res mapping failed!!\n", __func__);
		return PTR_ERR(node);
	} else {
		sd_pdev = of_find_device_by_node(node);
		pr_info("Func: %s ***ASH_DEBUG*** child_node: %s sd_pdev: %s num_res:%d\n", __func__, node->name, sd_pdev->name,
				sd_pdev->num_resources);
		of_node_put(node);
		ret = of_address_to_resource(node, 0, lp->sdres);
		pr_info("Func: %s ***ASH_DEBUG*** address_to_resource translation done\n", __func__);
		lp->sdnet_regs = devm_ioremap_resource(&sd_pdev->dev, lp->sdres);
		if (IS_ERR(lp->sdnet_regs)) {
			dev_err(&sd_pdev->dev, "ioremap failed for the sdnet\n");
			ret = PTR_ERR(lp->sdnet_regs);
			return ret;
		}
		pr_info("Func: %s ***ASH_DEBUG*** sdnet res mapping successful 0x%x!!\n", __func__, lp->sdnet_regs);
		iowrite32(0x00000000, lp->sdnet_regs + 0x1000);
		iowrite32(0x00000000, lp->sdnet_regs + 0x1800);
		iowrite32(0x0000042f, lp->sdnet_regs + 0x1000);
		iowrite32(0x930a0311, lp->sdnet_regs + 0x1800);
		iowrite32(0x211e5b98, lp->sdnet_regs + 0x0500);
		iowrite32(0x9911007c, lp->sdnet_regs + 0x1800);
		iowrite32(0xf6cd9ccc, lp->sdnet_regs + 0x0508);
	}
	return 0;
}
#endif
#endif

static int __maybe_unused axienet_mcdma_probe(struct platform_device *pdev,
		struct axienet_local *lp,
		struct net_device *ndev)
{
	int i, ret = 0;
	struct axienet_dma_q *q;
	struct device_node *np;
	struct resource dmares;
	const char *str;

	ret = of_property_count_strings(pdev->dev.of_node, "xlnx,channel-ids");
	if (ret < 0)
		return -EINVAL;

	for_each_rx_dma_queue(lp, i) {
		q = kzalloc(sizeof(*q), GFP_KERNEL);

		/* parent */
		q->lp = lp;
		lp->dq[i] = q;
		ret = of_property_read_string_index(pdev->dev.of_node,
				"xlnx,channel-ids", i,
				&str);
		ret = kstrtou16(str, 16, &q->chan_id);
		lp->qnum[i] = i;
		lp->chan_num[i] = q->chan_id;
	}

	np = of_parse_phandle(pdev->dev.of_node, "axistream-connected",
			0);
	if (IS_ERR(np)) {
		dev_err(&pdev->dev, "could not find DMA node\n");
		return ret;
	}

	ret = of_address_to_resource(np, 0, &dmares);
	if (ret) {
		dev_err(&pdev->dev, "unable to get DMA resource\n");
		return ret;
	}

	ret = of_property_read_u8(np, "xlnx,addrwidth", (u8 *)&lp->dma_mask);
	if (ret < 0 || lp->dma_mask < XAE_DMA_MASK_MIN ||
			lp->dma_mask > XAE_DMA_MASK_MAX) {
		dev_info(&pdev->dev, "missing/invalid xlnx,addrwidth property, using default\n");
		lp->dma_mask = XAE_DMA_MASK_MIN;
	}

	lp->mcdma_regs = devm_ioremap_resource(&pdev->dev, &dmares);
	if (IS_ERR(lp->mcdma_regs)) {
		dev_err(&pdev->dev, "iormeap failed for the dma\n");
		ret = PTR_ERR(lp->mcdma_regs);
		return ret;
	}

	axienet_mcdma_tx_probe(pdev, np, lp);
	axienet_mcdma_rx_probe(pdev, lp, ndev);

	return 0;
}
#endif

static int __maybe_unused axienet_dma_probe(struct platform_device *pdev,
		struct net_device *ndev)
{
	int i, ret;
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_dma_q *q;
	struct device_node *np = NULL;
	struct resource dmares;
#ifdef CONFIG_XILINX_TSN
	char dma_name[10];
#endif

	for_each_rx_dma_queue(lp, i) {
		q = kzalloc(sizeof(*q), GFP_KERNEL);

		/* parent */
		q->lp = lp;

		lp->dq[i] = q;
	}

	/* Find the DMA node, map the DMA registers, and decode the DMA IRQs */
	/* TODO handle error ret */
	for_each_rx_dma_queue(lp, i) {
		q = lp->dq[i];

		np = of_parse_phandle(pdev->dev.of_node, "axistream-connected",
				i);
		if (np) {
			ret = of_address_to_resource(np, 0, &dmares);
			if (ret >= 0) {
				q->dma_regs = devm_ioremap_resource(&pdev->dev,
						&dmares);
			} else {
				dev_err(&pdev->dev, "unable to get DMA resource for %pOF\n",
						np);
				return -ENODEV;
			}
			q->eth_hasdre = of_property_read_bool(np,
					"xlnx,include-dre");
			ret = of_property_read_u8(np, "xlnx,addrwidth",
					(u8 *)&lp->dma_mask);
			if (ret <  0 || lp->dma_mask < XAE_DMA_MASK_MIN ||
					lp->dma_mask > XAE_DMA_MASK_MAX) {
				dev_info(&pdev->dev, "missing/invalid xlnx,addrwidth property, using default\n");
				lp->dma_mask = XAE_DMA_MASK_MIN;
			}

		} else {
			dev_err(&pdev->dev, "missing axistream-connected property\n");
			return -EINVAL;
		}
	}

#ifdef CONFIG_XILINX_TSN
	if (lp->is_tsn) {
		for_each_rx_dma_queue(lp, i) {
			sprintf(dma_name, "dma%d_tx", i);
			lp->dq[i]->tx_irq = platform_get_irq_byname(pdev,
					dma_name);
			sprintf(dma_name, "dma%d_rx", i);
			lp->dq[i]->rx_irq = platform_get_irq_byname(pdev,
					dma_name);
			pr_info("lp->dq[%d]->tx_irq  %d\n", i,
					lp->dq[i]->tx_irq);
			pr_info("lp->dq[%d]->rx_irq  %d\n", i,
					lp->dq[i]->rx_irq);
		}
	} else {
#endif /* This should remove when axienet device tree irq comply to dma name */
		for_each_rx_dma_queue(lp, i) {
			lp->dq[i]->tx_irq = irq_of_parse_and_map(np, 0);
			lp->dq[i]->rx_irq = irq_of_parse_and_map(np, 1);
		}
#ifdef CONFIG_XILINX_TSN
	}
#endif

	of_node_put(np);

	for_each_rx_dma_queue(lp, i) {
		struct axienet_dma_q *q = lp->dq[i];

		spin_lock_init(&q->tx_lock);
		spin_lock_init(&q->rx_lock);
	}

	for_each_rx_dma_queue(lp, i) {
		netif_napi_add(ndev, &lp->napi[i], xaxienet_rx_poll,
				XAXIENET_NAPI_WEIGHT);
		//pr_info("Func: %s,*****DEBUG***** rx_dma_queue: %d", __func__, i);
	}
	return 0;
}

static int axienet_clk_init(struct platform_device *pdev,
		struct clk **axi_aclk, struct clk **axis_clk,
		struct clk **ref_clk, struct clk **tmpclk)
{
	int err;

	*tmpclk = NULL;

	/* The "ethernet_clk" is deprecated and will be removed sometime in
	 * the future. For proper clock usage check axiethernet binding
	 * documentation.
	 */
	*axi_aclk = devm_clk_get(&pdev->dev, "ethernet_clk");
	if (IS_ERR(*axi_aclk)) {
		if (PTR_ERR(*axi_aclk) != -ENOENT) {
			err = PTR_ERR(*axi_aclk);
			return err;
		}

		*axi_aclk = devm_clk_get(&pdev->dev, "s_axi_lite_clk");
		if (IS_ERR(*axi_aclk)) {
			if (PTR_ERR(*axi_aclk) != -ENOENT) {
				err = PTR_ERR(*axi_aclk);
				return err;
			}
			*axi_aclk = NULL;
		}

	} else {
		dev_warn(&pdev->dev, "ethernet_clk is deprecated and will be removed sometime in the future\n");
	}

	*axis_clk = devm_clk_get(&pdev->dev, "axis_clk");
	if (IS_ERR(*axis_clk)) {
		if (PTR_ERR(*axis_clk) != -ENOENT) {
			err = PTR_ERR(*axis_clk);
			return err;
		}
		*axis_clk = NULL;
	}

	*ref_clk = devm_clk_get(&pdev->dev, "ref_clk");
	if (IS_ERR(*ref_clk)) {
		if (PTR_ERR(*ref_clk) != -ENOENT) {
			err = PTR_ERR(*ref_clk);
			return err;
		}
		*ref_clk = NULL;
	}

	err = clk_prepare_enable(*axi_aclk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable axi_aclk/ethernet_clk (%d)\n", err);
		return err;
	}

	err = clk_prepare_enable(*axis_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable axis_clk (%d)\n", err);
		goto err_disable_axi_aclk;
	}

	err = clk_prepare_enable(*ref_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable ref_clk (%d)\n", err);
		goto err_disable_axis_clk;
	}

	return 0;

err_disable_axis_clk:
	clk_disable_unprepare(*axis_clk);
err_disable_axi_aclk:
	clk_disable_unprepare(*axi_aclk);

	return err;
}

static int axienet_dma_clk_init(struct platform_device *pdev)
{
	int err;
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct axienet_local *lp = netdev_priv(ndev);

	/* The "dma_clk" is deprecated and will be removed sometime in
	 * the future. For proper clock usage check axiethernet binding
	 * documentation.
	 */
	lp->dma_tx_clk = devm_clk_get(&pdev->dev, "dma_clk");
	if (IS_ERR(lp->dma_tx_clk)) {
		if (PTR_ERR(lp->dma_tx_clk) != -ENOENT) {
			err = PTR_ERR(lp->dma_tx_clk);
			return err;
		}

		lp->dma_tx_clk = devm_clk_get(&pdev->dev, "m_axi_mm2s_aclk");
		if (IS_ERR(lp->dma_tx_clk)) {
			if (PTR_ERR(lp->dma_tx_clk) != -ENOENT) {
				err = PTR_ERR(lp->dma_tx_clk);
				return err;
			}
			lp->dma_tx_clk = NULL;
		}
	} else {
		dev_warn(&pdev->dev, "dma_clk is deprecated and will be removed sometime in the future\n");
	}

	lp->dma_rx_clk = devm_clk_get(&pdev->dev, "m_axi_s2mm_aclk");
	if (IS_ERR(lp->dma_rx_clk)) {
		if (PTR_ERR(lp->dma_rx_clk) != -ENOENT) {
			err = PTR_ERR(lp->dma_rx_clk);
			return err;
		}
		lp->dma_rx_clk = NULL;
	}

	lp->dma_sg_clk = devm_clk_get(&pdev->dev, "m_axi_sg_aclk");
	if (IS_ERR(lp->dma_sg_clk)) {
		if (PTR_ERR(lp->dma_sg_clk) != -ENOENT) {
			err = PTR_ERR(lp->dma_sg_clk);
			return err;
		}
		lp->dma_sg_clk = NULL;
	}

	err = clk_prepare_enable(lp->dma_tx_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable tx_clk/dma_clk (%d)\n", err);
		return err;
	}

	err = clk_prepare_enable(lp->dma_rx_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable rx_clk (%d)\n", err);
		goto err_disable_txclk;
	}

	err = clk_prepare_enable(lp->dma_sg_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable sg_clk (%d)\n", err);
		goto err_disable_rxclk;
	}

	return 0;

err_disable_rxclk:
	clk_disable_unprepare(lp->dma_rx_clk);
err_disable_txclk:
	clk_disable_unprepare(lp->dma_tx_clk);

	return err;
}

static void axienet_clk_disable(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct axienet_local *lp = netdev_priv(ndev);
#ifdef XILINX_MAC_DEBUG
	clk_disable_unprepare(lp->dma_sg_clk);
	clk_disable_unprepare(lp->dma_tx_clk);
	clk_disable_unprepare(lp->dma_rx_clk);
	clk_disable_unprepare(lp->eth_sclk);
	clk_disable_unprepare(lp->eth_refclk);
	clk_disable_unprepare(lp->eth_dclk);
	clk_disable_unprepare(lp->aclk);
#endif
}

static int xxvenet_clk_init(struct platform_device *pdev,
		struct clk **axi_aclk, struct clk **axis_clk,
		struct clk **tmpclk, struct clk **dclk)
{
	int err = 0;
#ifdef XILINX_MAC_DEBUG
	*tmpclk = NULL;

	/* The "ethernet_clk" is deprecated and will be removed sometime in
	 * the future. For proper clock usage check axiethernet binding
	 * documentation.
	 */
	*axi_aclk = devm_clk_get(&pdev->dev, "ethernet_clk");
	if (IS_ERR(*axi_aclk)) {
		if (PTR_ERR(*axi_aclk) != -ENOENT) {
			err = PTR_ERR(*axi_aclk);
			return err;
		}

		*axi_aclk = devm_clk_get(&pdev->dev, "s_axi_aclk");
		if (IS_ERR(*axi_aclk)) {
			if (PTR_ERR(*axi_aclk) != -ENOENT) {
				err = PTR_ERR(*axi_aclk);
				return err;
			}
			*axi_aclk = NULL;
		}

	} else {
		dev_warn(&pdev->dev, "ethernet_clk is deprecated and will be removed sometime in the future\n");
	}

	*axis_clk = devm_clk_get(&pdev->dev, "rx_core_clk");
	if (IS_ERR(*axis_clk)) {
		if (PTR_ERR(*axis_clk) != -ENOENT) {
			err = PTR_ERR(*axis_clk);
			return err;
		}
		*axis_clk = NULL;
	}

	*dclk = devm_clk_get(&pdev->dev, "dclk");
	if (IS_ERR(*dclk)) {
		if (PTR_ERR(*dclk) != -ENOENT) {
			err = PTR_ERR(*dclk);
			return err;
		}
		*dclk = NULL;
	}

	err = clk_prepare_enable(*axi_aclk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable axi_clk/ethernet_clk (%d)\n", err);
		return err;
	}

	err = clk_prepare_enable(*axis_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable axis_clk (%d)\n", err);
		goto err_disable_axi_aclk;
	}

	err = clk_prepare_enable(*dclk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable dclk (%d)\n", err);
		goto err_disable_axis_clk;
	}

	return 0;

err_disable_axis_clk:
	clk_disable_unprepare(*axis_clk);
err_disable_axi_aclk:
	clk_disable_unprepare(*axi_aclk);
#endif
	return err;
}

static const struct axienet_config axienet_1g_config = {
	.mactype = XAXIENET_1G,
	.setoptions = axienet_setoptions,
	.clk_init = axienet_clk_init,
	.tx_ptplen = XAE_TX_PTP_LEN,
};

static const struct axienet_config axienet_2_5g_config = {
	.mactype = XAXIENET_2_5G,
	.setoptions = axienet_setoptions,
	.clk_init = axienet_clk_init,
	.tx_ptplen = XAE_TX_PTP_LEN,
};

static const struct axienet_config axienet_10g_config = {
	.mactype = XAXIENET_LEGACY_10G,
	.setoptions = axienet_setoptions,
	.clk_init = xxvenet_clk_init,
	.tx_ptplen = XAE_TX_PTP_LEN,
};

static const struct axienet_config axienet_10g25g_config = {
	.mactype = XAXIENET_10G_25G,
	.setoptions = xxvenet_setoptions,
	.clk_init = xxvenet_clk_init,
	.tx_ptplen = XXV_TX_PTP_LEN,
};

static const struct axienet_config axienet_usxgmii_config = {
	.mactype = XAXIENET_10G_25G,
	.setoptions = xxvenet_setoptions,
	.clk_init = xxvenet_clk_init,
	.tx_ptplen = 0,
};

/* Match table for of_platform binding */
static const struct of_device_id axienet_of_match[] = {
	{ .compatible = "xlnx,axi-ethernet-1.00.a", .data = &axienet_1g_config},
	{ .compatible = "xlnx,axi-ethernet-1.01.a", .data = &axienet_1g_config},
	{ .compatible = "xlnx,axi-ethernet-2.01.a", .data = &axienet_1g_config},
	{ .compatible = "xlnx,axi-2_5-gig-ethernet-1.0",
		.data = &axienet_2_5g_config},
	{ .compatible = "xlnx,ten-gig-eth-mac", .data = &axienet_10g_config},
	{ .compatible = "xlnx,xxv-ethernet-1.0",
		.data = &axienet_10g25g_config},
	{ .compatible = "xlnx,tsn-ethernet-1.00.a", .data = &axienet_1g_config},
	{ .compatible = "xlnx,xxv-usxgmii-ethernet-1.0",
		.data = &axienet_usxgmii_config},
	{},
};

MODULE_DEVICE_TABLE(of, axienet_of_match);

/**
 * axienet_probe - Axi Ethernet probe function.
 * @pdev:	Pointer to platform device structure.
 *
 * Return: 0, on success
 *	    Non-zero error value on failure.
 *
 * This is the probe routine for Axi Ethernet driver. This is called before
 * any other driver routines are invoked. It allocates and sets up the Ethernet
 * device. Parses through device tree and populates fields of
 * axienet_local. It registers the Ethernet device.
 */
static int axienet_probe(struct platform_device *pdev)
{
	int (*axienet_clk_init)(struct platform_device *pdev,
			struct clk **axi_aclk, struct clk **axis_clk,
			struct clk **ref_clk, struct clk **tmpclk) =
		axienet_clk_init;
	int i;
	int ret = 0;
#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
	struct device_node *np;
#endif
	struct axienet_local *lp;
	struct net_device *ndev;
	const void *mac_addr;
#ifdef XILINX_MAC_DEBUG
	struct resource *ethres;
#else
	struct platform_device *mcdma_dev;
	struct device_node *node, *parent, *dma_c;
#endif
	u32 value;
#ifndef XILINX_MAC_DEBUG
	int num_queues = XAE_MAX_QUEUES;
#else
	u16 num_queues = XAE_MAX_QUEUES;
#endif
	bool slave = false;
	bool is_tsn = false;

	//dev_info(&pdev->dev,"***ASH_DEBUG*** Probing AXIENET driver\n");
#ifndef XILINX_MAC_DEBUG
	parent = of_get_parent(pdev->dev.of_node);
	if (!parent) {
		//pr_info("Func: %s ***ASH_DEBUG*** No parent node!!\n", __func__);
		return PTR_ERR(parent);
	} else
		//pr_info("Func: %s ***ASH_DEBUG*** Parent node: %s \n", __func__, parent->name);
	node = of_get_child_by_name(parent, "axi_mcdma");
	if (IS_ERR(node)) {
		//pr_info("Func: %s ***ASH_DEBUG*** no mcdma node!!\n", __func__);
		return PTR_ERR(node);
	} else {	
		//pr_info("Func: %s ***ASH_DEBUG*** mcdma node is there: %s !!\n", __func__, node->name);
		mcdma_dev = of_find_device_by_node(node);
		if (!mcdma_dev) {
			dev_err(&pdev->dev, "unable to get DMA device details\n");
			return -1;
		} else {
			of_node_put(node);
			//pr_info("Func: %s ***ASH_DEBUG*** mcdma node: %s !!\n", __func__, mcdma_dev->name);
		}
	}
#endif
	is_tsn = of_property_read_bool(pdev->dev.of_node, "xlnx,tsn");
#ifndef XILINX_MAC_DEBUG
	dma_c = of_get_next_child(node, NULL);
	ret = of_property_read_u32(dma_c, "dma-channels", &num_queues);
	if(ret < 0) {
		//pr_info("Func: %s***ASH_DEBUG*** Error getting num_queues!!\n", __func__);
		num_queues = XAE_MAX_QUEUES;
	} else
		//pr_info("Func: %s***ASH_DEBUG*** num_queues: %d!!\n", __func__, num_queues);
#else
	ret = of_property_read_u16(pdev->dev.of_node, "xlnx,num-queues",
			&num_queues);
	//pr_info("Func: %s**DEBUG** xlnx,num-queues : %x\n", __func__, num_queues);
#endif
	if (ret) {
		if (!is_tsn) {
#ifndef CONFIG_AXIENET_HAS_MCDMA
			num_queues = 1;
#endif
		}
	}
#ifdef CONFIG_XILINX_TSN
	if (is_tsn && (num_queues < XAE_TSN_MIN_QUEUES ||
				num_queues > XAE_MAX_QUEUES)) {
		num_queues = XAE_MAX_QUEUES;
		//pr_info("Func: %s**DEBUG**inside_tsn num_queues: %d\n", __func__, num_queues);
	}
#endif

	//pr_info("Func: %s***ASH_DEBUG*** num_queues: %d\n", __func__, num_queues);
	ndev = alloc_etherdev_mq(sizeof(*lp), num_queues);
	if (!ndev)
		return -ENOMEM;

	platform_set_drvdata(pdev, ndev);

	SET_NETDEV_DEV(ndev, &pdev->dev);
	ndev->flags &= ~IFF_MULTICAST;  /* clear multicast */
	ndev->features = NETIF_F_SG;
	ndev->netdev_ops = &axienet_netdev_ops;
#ifdef XILINX_MAC_DEBUG
	ndev->ethtool_ops = &axienet_ethtool_ops;
#endif
	/* MTU range: 64 - 9000 */
	ndev->min_mtu = 64;
	ndev->max_mtu = XAE_JUMBO_MTU;
	lp = netdev_priv(ndev);
	lp->ndev = ndev;
	lp->dev = &pdev->dev;
	lp->options = XAE_OPTION_DEFAULTS;
	lp->num_tx_queues = num_queues;
	lp->num_rx_queues = num_queues;
	lp->is_tsn = is_tsn;
	lp->rx_bd_num = RX_BD_NUM_DEFAULT;
	lp->tx_bd_num = TX_BD_NUM_DEFAULT;

#ifdef CONFIG_XILINX_TSN
	ret = of_property_read_u16(pdev->dev.of_node, "xlnx,num-tc",
			&lp->num_tc);
	if (ret || (lp->num_tc != 2 && lp->num_tc != 3))
		lp->num_tc = XAE_MAX_TSN_TC;
#endif

#ifdef XILINX_MAC_DEBUG
	/* Map device registers */
	ethres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lp->regs = devm_ioremap_resource(&pdev->dev, ethres);
	if (IS_ERR(lp->regs)) {
		ret = PTR_ERR(lp->regs);
		goto free_netdev;
	}
#ifdef CONFIG_XILINX_TSN
	slave = of_property_read_bool(pdev->dev.of_node,
			"xlnx,tsn-slave");
	if (slave)
		lp->temac_no = XAE_TEMAC2;
	else
		lp->temac_no = XAE_TEMAC1;
#endif
	lp->regs_start = ethres->start;

	/* Setup checksum offload, but default to off if not specified */
	lp->features = 0;
#endif
	if (pdev->dev.of_node) {
		const struct of_device_id *match;

		match = of_match_node(axienet_of_match, pdev->dev.of_node);
		if (match && match->data) {
			lp->axienet_config = match->data;
			axienet_clk_init = lp->axienet_config->clk_init;
		}
	}

	ret = of_property_read_u32(pdev->dev.of_node, "xlnx,txcsum", &value);
	if (!ret) {
		dev_info(&pdev->dev, "TX_CSUM %d\n", value);

		switch (value) {
			case 1:
				lp->csum_offload_on_tx_path =
					XAE_FEATURE_PARTIAL_TX_CSUM;
				lp->features |= XAE_FEATURE_PARTIAL_TX_CSUM;
				/* Can checksum TCP/UDP over IPv4. */
				ndev->features |= NETIF_F_IP_CSUM | NETIF_F_SG;
				break;
			case 2:
				lp->csum_offload_on_tx_path =
					XAE_FEATURE_FULL_TX_CSUM;
				lp->features |= XAE_FEATURE_FULL_TX_CSUM;
				/* Can checksum TCP/UDP over IPv4. */
				ndev->features |= NETIF_F_IP_CSUM | NETIF_F_SG;
				break;
			default:
				lp->csum_offload_on_tx_path = XAE_NO_CSUM_OFFLOAD;
		}
	}
	ret = of_property_read_u32(pdev->dev.of_node, "xlnx,rxcsum", &value);
	if (!ret) {
		dev_info(&pdev->dev, "RX_CSUM %d\n", value);

		switch (value) {
			case 1:
				lp->csum_offload_on_rx_path =
					XAE_FEATURE_PARTIAL_RX_CSUM;
				lp->features |= XAE_FEATURE_PARTIAL_RX_CSUM;
				break;
			case 2:
				lp->csum_offload_on_rx_path =
					XAE_FEATURE_FULL_RX_CSUM;
				lp->features |= XAE_FEATURE_FULL_RX_CSUM;
				break;
			default:
				lp->csum_offload_on_rx_path = XAE_NO_CSUM_OFFLOAD;
		}
	}
	/* For supporting jumbo frames, the Axi Ethernet hardware must have
	 * a larger Rx/Tx Memory. Typically, the size must be large so that
	 * we can enable jumbo option and start supporting jumbo frames.
	 * Here we check for memory allocated for Rx/Tx in the hardware from
	 * the device-tree and accordingly set flags.
	 */
	of_property_read_u32(pdev->dev.of_node, "xlnx,rxmem", &lp->rxmem);

	/* The phy_mode is optional but when it is not specified it should not
	 *  be a value that alters the driver behavior so set it to an invalid
	 *  value as the default.
	 */
	lp->phy_mode = ~0;
	of_property_read_u32(pdev->dev.of_node, "xlnx,phy-type", &lp->phy_mode);

	/* Set default USXGMII rate */
	lp->usxgmii_rate = SPEED_1000;
	of_property_read_u32(pdev->dev.of_node, "xlnx,usxgmii-rate",
			&lp->usxgmii_rate);

	lp->eth_hasnobuf = of_property_read_bool(pdev->dev.of_node,
			"xlnx,eth-hasnobuf");
	lp->eth_hasptp = of_property_read_bool(pdev->dev.of_node,
			"xlnx,eth-hasptp");

#ifdef XILINX_MAC_DEBUG
	if ((lp->axienet_config->mactype == XAXIENET_1G) && !lp->eth_hasnobuf)
		lp->eth_irq = platform_get_irq(pdev, 0);

#ifdef CONFIG_XILINX_AXI_EMAC_HWTSTAMP
	if (!lp->is_tsn) {
		struct resource txtsres, rxtsres;

		/* Find AXI Stream FIFO */
		np = of_parse_phandle(pdev->dev.of_node, "axififo-connected",
				0);
		if (IS_ERR(np)) {
			dev_err(&pdev->dev, "could not find TX Timestamp FIFO\n");
			ret = PTR_ERR(np);
			goto free_netdev;
		}

		ret = of_address_to_resource(np, 0, &txtsres);
		if (ret) {
			dev_err(&pdev->dev,
					"unable to get Tx Timestamp resource\n");
			goto free_netdev;
		}

		lp->tx_ts_regs = devm_ioremap_resource(&pdev->dev, &txtsres);
		if (IS_ERR(lp->tx_ts_regs)) {
			dev_err(&pdev->dev, "could not map Tx Timestamp regs\n");
			ret = PTR_ERR(lp->tx_ts_regs);
			goto free_netdev;
		}

		if (lp->axienet_config->mactype == XAXIENET_10G_25G) {
			np = of_parse_phandle(pdev->dev.of_node,
					"xlnx,rxtsfifo", 0);
			if (IS_ERR(np)) {
				dev_err(&pdev->dev,
						"couldn't find rx-timestamp FIFO\n");
				ret = PTR_ERR(np);
				goto free_netdev;
			}

			ret = of_address_to_resource(np, 0, &rxtsres);
			if (ret) {
				dev_err(&pdev->dev,
						"unable to get rx-timestamp resource\n");
				goto free_netdev;
			}

			lp->rx_ts_regs = devm_ioremap_resource(&pdev->dev,
					&rxtsres);
			if (IS_ERR(lp->rx_ts_regs)) {
				dev_err(&pdev->dev,
						"couldn't map rx-timestamp regs\n");
				ret = PTR_ERR(lp->rx_ts_regs);
				goto free_netdev;
			}
			lp->tx_ptpheader = devm_kzalloc(&pdev->dev,
					XXVENET_TS_HEADER_LEN,
					GFP_KERNEL);
		}

		of_node_put(np);
	}
#endif
#endif
	if (!slave) {
#ifdef CONFIG_AXIENET_HAS_MCDMA
		ret = axienet_mcdma_probe(pdev, lp, ndev);
#if 0
#ifndef XILINX_MAC_DEBUG
		ret = axienet_sdnet_probe(pdev, lp);
		if (ret != 0) {
			dev_err(&pdev->dev,
					"***ASH_DEBUG*** couldn't map sdnet regs\n");
			if(lp->sdres) {
				kfree(lp->sdres);
				lp->sdres = NULL;
			}
		} else
			dev_info(&pdev->dev,"***ASH_DEBUG*** mapped sdnet regs\n");
#endif
#endif
#else
		ret = axienet_dma_probe(pdev, ndev);
#endif
		if (ret) {
			pr_err("Getting DMA resource failed\n");
			goto free_netdev;
		} else
			dev_info(&pdev->dev,
					"mapped mcdma regs\n");

		if (dma_set_mask_and_coherent(lp->dev, DMA_BIT_MASK(lp->dma_mask)) != 0) {
			dev_warn(&pdev->dev, "default to %d-bit dma mask\n", XAE_DMA_MASK_MIN);
			if (dma_set_mask_and_coherent(lp->dev, DMA_BIT_MASK(XAE_DMA_MASK_MIN)) != 0) {
				dev_err(&pdev->dev, "dma_set_mask_and_coherent failed, aborting\n");
				goto free_netdev;
			}
		}

		ret = axienet_dma_clk_init(pdev);
		if (ret) {
			if (ret != -EPROBE_DEFER)
				dev_err(&pdev->dev, "DMA clock init failed %d\n", ret);
			goto free_netdev;
		}
	}
#ifdef XILINX_MAC_DEBUG
	ret = axienet_clk_init(pdev, &lp->aclk, &lp->eth_sclk,
			&lp->eth_refclk, &lp->eth_dclk);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Ethernet clock init failed %d\n", ret);
		goto err_disable_clk;
	}

	lp->eth_irq = platform_get_irq(pdev, 0);
	/* Check for Ethernet core IRQ (optional) */
	if (lp->eth_irq <= 0)
		dev_info(&pdev->dev, "Ethernet core IRQ not defined\n");
#endif
	/* Retrieve the MAC address */
	mac_addr = of_get_mac_address(pdev->dev.of_node);
	if (IS_ERR(mac_addr)) {
		dev_warn(&pdev->dev, "could not find MAC address property: %ld\n",
				PTR_ERR(mac_addr));
		mac_addr = NULL;
	}
	axienet_set_mac_address(ndev, mac_addr);

	lp->coalesce_count_rx = XAXIDMA_DFT_RX_THRESHOLD;
	lp->coalesce_count_tx = XAXIDMA_DFT_TX_THRESHOLD;

#ifdef XILINX_MAC_DEBUG
	ret = of_get_phy_mode(pdev->dev.of_node);
	if (ret < 0)
		dev_warn(&pdev->dev, "couldn't find phy i/f\n");
	lp->phy_interface = ret;
	if (lp->phy_mode == XAE_PHY_TYPE_1000BASE_X)
		lp->phy_flags = XAE_PHY_TYPE_1000BASE_X;

	lp->phy_node = of_parse_phandle(pdev->dev.of_node, "phy-handle", 0);
	if (lp->phy_node) {
		lp->clk = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(lp->clk)) {
			dev_warn(&pdev->dev, "Failed to get clock: %ld\n",
					PTR_ERR(lp->clk));
			lp->clk = NULL;
		} else {
			ret = clk_prepare_enable(lp->clk);
			if (ret) {
				dev_err(&pdev->dev, "Unable to enable clock: %d\n",
						ret);
				goto free_netdev;
			}
		}

		ret = axienet_mdio_setup(lp);
		if (ret)
			dev_warn(&pdev->dev,
					"error registering MDIO bus: %d\n", ret);
	}
#endif
#ifdef CONFIG_AXIENET_HAS_MCDMA
	/* Create sysfs file entries for the device */
	ret = axeinet_mcdma_create_sysfs(&lp->dev->kobj);
	if (ret < 0) {
		dev_err(lp->dev, "unable to create sysfs entries\n");
		return ret;
	}
#endif

	ret = register_netdev(lp->ndev);
	if (ret) {
		dev_err(lp->dev, "register_netdev() error (%i)\n", ret);
#ifdef XILINX_MAC_DEBUG
		axienet_mdio_teardown(lp);
#endif
		goto err_disable_clk;
	}
#ifdef XILINX_MAC_DEBUG
#ifdef CONFIG_XILINX_TSN_PTP
	if (lp->is_tsn) {
		lp->ptp_rx_irq = platform_get_irq_byname(pdev, "ptp_rx");

		lp->ptp_tx_irq = platform_get_irq_byname(pdev, "ptp_tx");

		lp->qbv_irq = platform_get_irq_byname(pdev, "qbv_irq");

		pr_debug("ptp RX irq: %d\n", lp->ptp_rx_irq);
		pr_debug("ptp TX irq: %d\n", lp->ptp_tx_irq);
		pr_debug("qbv_irq: %d\n", lp->qbv_irq);

		spin_lock_init(&lp->ptp_tx_lock);

		if (lp->temac_no == XAE_TEMAC1) {
			axienet_ptp_timer_probe((lp->regs + XAE_RTC_OFFSET),
					pdev);

			/* enable VLAN */
			lp->options |= XAE_OPTION_VLAN;
			axienet_setoptions(lp->ndev, lp->options);
#ifdef CONFIG_XILINX_TSN_QBV
			axienet_qbv_init(ndev);
#endif
		}
	}
#endif
#endif
#ifndef XILINX_MAC_DEBUG
	ndev->needed_headroom += NLMSG_HDRLEN;
#if 0
	lp->skbq = kmalloc(sizeof(struct sk_buff_head), GFP_KERNEL);
	if (!lp->skbq) {
		pr_err("%s: cannot allocate skb_buff_head\n", __func__);
		goto err_disable_clk;
	}
	skb_queue_head_init(lp->skbq);

	for (i = 0; i < SKB_QUEUE_LEN; i++) {
		lp->skbuff[i] = netdev_alloc_skb(ndev, PKT_BUFF_SZ);
		if (!lp->skbuff[i]) 
			goto err_skb_queue;
		else
			skb_queue_tail(lp->skbq, lp->skbuff[i]);
	}
#endif
	lp->granted_q = kmalloc(sizeof(struct sk_buff_head), GFP_KERNEL);
	if (!lp->granted_q) {
		pr_err("%s: cannot allocate skb_buff_head\n", __func__);
		goto err_disable_clk;
	}
	skb_queue_head_init(lp->granted_q);

	lp->grant_work_q = create_workqueue("counter_q");
	if(!lp->grant_work_q)
		goto err_work;
	//Register the new netlink family
	ret = u25_netlink_init();
	if (ret != 0) {
		goto err_genl;
	}
	printk("Generic Netlink Family registered\n");

//	INIT_DELAYED_WORK(&lp->grant_work, efx_mae_stats_send);
//	queue_delayed_work(lp->grant_work_q, &lp->grant_work, msecs_to_jiffies(120000));
#endif
	return 0;
#ifndef XILINX_MAC_DEBUG
err_genl:
	destroy_workqueue(lp->grant_work_q);
err_work:
	printk("An error occured while creating netlink socket\n");
	skb_queue_purge(lp->granted_q);
	kfree(lp->granted_q);
err_skb_queue:
	skb_queue_purge(lp->skbq);
	kfree(lp->skbq);
#endif
err_disable_clk:
	axienet_clk_disable(pdev);
free_netdev:
	free_netdev(ndev);

	return ret;
}

static int axienet_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct axienet_local *lp = netdev_priv(ndev);
	int i, ret;
#ifndef XILINX_MAC_DEBUG
	//Unregister the family
	ret = u25_netlink_exit();
	if(ret != 0) {
		printk("Unregister family %i\n",ret);
	}
	printk("Generic Netlink Family unregistered.\n");
	if(lp->granted_q) {
		skb_queue_purge(lp->granted_q);
		kfree(lp->granted_q);
	}
	if(lp->skbq) {
		skb_queue_purge(lp->skbq);
		kfree(lp->skbq);
	}
	flush_workqueue(lp->grant_work_q);
	destroy_workqueue(lp->grant_work_q);
#endif

	if (!lp->is_tsn || lp->temac_no == XAE_TEMAC1) {
		for_each_rx_dma_queue(lp, i)
			netif_napi_del(&lp->napi[i]);
	}
#ifdef CONFIG_XILINX_TSN_PTP
	axienet_ptp_timer_remove(lp->timer_priv);
#ifdef CONFIG_XILINX_TSN_QBV
	axienet_qbv_remove(ndev);
#endif
#endif
	unregister_netdev(ndev);
	axienet_clk_disable(pdev);

#ifdef XILINX_MAC_DEBUG
	if (lp->mii_bus)
		axienet_mdio_teardown(lp);
#endif

	if (lp->clk)
		clk_disable_unprepare(lp->clk);

#ifdef CONFIG_AXIENET_HAS_MCDMA
	axeinet_mcdma_remove_sysfs(&lp->dev->kobj);
#endif
#ifdef XILINX_MAC_DEBUG
	of_node_put(lp->phy_node);
	lp->phy_node = NULL;
#endif
	free_netdev(ndev);

	return 0;
}

static void axienet_shutdown(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);

	rtnl_lock();
	netif_device_detach(ndev);

	if (netif_running(ndev))
		dev_close(ndev);

	rtnl_unlock();
}

static struct platform_driver axienet_driver = {
	.probe = axienet_probe,
	.remove = axienet_remove,
	.shutdown = axienet_shutdown,
	.driver = {
		.name = "xilinx",
		.of_match_table = axienet_of_match,
	},
};

module_platform_driver(axienet_driver);

MODULE_DESCRIPTION("Xilinx Axi Ethernet driver");
MODULE_AUTHOR("Xilinx");
MODULE_LICENSE("GPL");
