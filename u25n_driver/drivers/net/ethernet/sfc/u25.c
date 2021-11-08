/****************************************************************************
 * U25 Mport Implementation for Xilinx U25N Accelerator Card.
 * Copyright 2021 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "net_driver.h"
#include "rx_common.h"
#include "nic.h"
#include "mae.h"
#include "emcdi.h"
#include "ipsec.h"
#include "u25_rep.h"


int efx_u25_rx_get_mports(struct efx_rx_buffer *rx_buf,
	uint32_t *outer_mport, uint32_t *inner_mport)
{
	uint16_t outer_vlan, inner_vlan;
	u8 *eh = efx_rx_buf_va(rx_buf);
	struct emcdi_hdr *hdr;

	//pkt_hex_dump(eh, "efx_u25_rx_get_mports");

	hdr = (struct emcdi_hdr *)eh;
	if ((hdr->h_outer_vlan_proto != htons(ETH_P_8021Q)) ||
		(hdr->h_inner_vlan_proto != htons(ETH_P_8021Q)))
		return -EINVAL;


	outer_vlan = ntohs(hdr->h_outer_vlan_TCI) & 0xfff;
	inner_vlan = ntohs(hdr->h_inner_vlan_TCI) & 0xfff;

	*outer_mport = U25_VLAN_TO_MPORT(outer_vlan);
	*inner_mport = U25_VLAN_TO_MPORT(inner_vlan);

	//pr_info("--> outer_vlan: %x, inner_vlan: %x<--", outer_vlan, inner_vlan);
	//pr_info("--> outer_mport: %x, inner_mport: %x<--", *outer_mport, *inner_mport);

	if (((*inner_mport == U25_MPORT_ID_CONTROL) ||
			(*inner_mport == U25_MPORT_ID_COUNTER)) &&
			(hdr->h_vlan_encapsulated_proto != htons(ETH_P_802_EX1)))
		return -EINVAL;

	return 0;
}

static void efx_u25_remove_vlans_from_network(struct efx_nic *efx, 
		struct efx_rx_buffer *rx_buf)
{
	u8 *eh = efx_rx_buf_va(rx_buf);

	memcpy(eh + (2 * ETH_ALEN), eh + (2 * ETH_ALEN) + (2 * VLAN_HLEN),
			rx_buf->len - (2 * VLAN_HLEN));
	rx_buf->len -= (2 * VLAN_HLEN);
}

int efx_u25_rx_packet(struct efx_channel *channel, bool *is_from_network)
{
	struct efx_rx_buffer *rx_buf =
		efx_rx_buffer(&channel->rx_queue, channel->rx_pkt_index);
	struct efx_ef10_nic_data *nic_data = channel->efx->nic_data;
	struct efx_nic *efx = channel->efx;
	uint32_t outer_mport, inner_mport;
	const char *reason;
	int rc = -EINVAL;

	if (!nic_data->have_pf_mport) {
		reason = "pf mport is not allocated";
		if (net_ratelimit())
			netif_err(efx, drv, efx->net_dev, "Unexpected packet (%s rc %d)\n",
				reason, rc);
		return rc;
	}

	rc = efx_u25_rx_get_mports(rx_buf, &outer_mport, &inner_mport);
	if (rc) {
#if 0
		reason = "Two VLAN tags are expected. Some issue in FPGA";
		if (net_ratelimit())
			netif_err(efx, drv, efx->net_dev, "Unexpected packet (%s rc %d)\n",
				reason, rc);
#endif
		return rc;
	}

	if (outer_mport == nic_data->pf_mport) {
		if (inner_mport == nic_data->base_mport) {
			efx_u25_remove_vlans_from_network(efx, rx_buf);
			*is_from_network = true;
			return 0;
		} else if ((inner_mport == U25_MPORT_ID_CONTROL) ||
			(inner_mport == U25_MPORT_ID_COUNTER)) {
			*is_from_network = false;
			return 0;
		} else {
			struct net_device *rep_dev = efx_u25_find_vfrep_by_mport(efx,
					inner_mport);
			if (rep_dev && rep_dev->flags & IFF_UP) {
				efx_u25_vfrep_rx_packet(netdev_priv(rep_dev),
						rx_buf);
				return 0;
			} else {
				rc =-EINVAL;
				reason = "Representor netdev not found/not UP";
				if (net_ratelimit())
					netif_err(efx, drv, efx->net_dev, "Unexpected packet (%s rc %d)\n",
						reason, rc);
				return rc;
			}
		}
	} else {
		rc = -EINVAL;
		reason = "invalid PF mport";
		if (net_ratelimit())
			netif_err(efx, drv, efx->net_dev, "Unexpected packet (%s rc %d)\n",
				reason, rc);
		return rc;
	}
}

static void efx_u25_get_base_mport(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	u32 id;

	/* Construct mport selector for "physical network port" */
	efx_mae_mport_wire(efx, &id);
	nic_data->base_mport = id & 0xfff;
	nic_data->have_mport = true;

	/* Construct mport selector for "calling PF" */
	efx_mae_mport_uplink(efx, &id);
	nic_data->pf_mport = id & 0xfff;
	nic_data->have_pf_mport = true;
}

int efx_u25_probe(struct efx_nic *efx)
{
        struct efx_ef10_nic_data *nic_data = efx->nic_data;
	int rc;

	if (efx->type->is_vf) {
		// TODO -
		return 0;
	}

	if (efx_is_u25(efx)) {
		rc = efx_init_struct_tc(efx);
		if (rc)
			return rc;
	}

	spin_lock_init(&nic_data->vf_reps_lock);
	INIT_LIST_HEAD(&nic_data->udp_tunnel_list);

	efx_u25_get_base_mport(efx);

	//TODO - send normal MCDI to enable encapsulated MCDI via MDIO
	nic_data->mode = U25_MODE_LEGACY;

	rc = efx_ipsec_init(efx);
	if (rc)
		return rc;

	return 0;
}

void efx_u25_remove(struct efx_nic *efx)
{
	if (efx->type->is_vf) {
		// TODO -
		return;
	}

	efx_ipsec_fini(efx);

	//TODO -
}
