/****************************************************************************
 * U25 Mport Implementation for Xilinx U25N Accelerator Card.
 * Copyright 2021 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef U25_H
#define U25_H

#define U25_MPORT_TO_VLAN(mport)	(mport ^ 0x800)
#define U25_VLAN_TO_MPORT(vlan)		(vlan ^ 0x800)

#define U25_MPORT_ID_COUNTER		0xFD
#define U25_MPORT_ID_CONTROL		0xFE

#define U25_MODE_LEGACY			0
#define U25_MODE_SWITCHDEV		1
#define U25_MODE_BOOTSTRAP		2

int efx_u25_probe(struct efx_nic *efx);
void efx_u25_remove(struct efx_nic *efx);
void efx_u25_start_reps(struct efx_nic *efx);
void efx_u25_stop_reps(struct efx_nic *efx);
int efx_u25_rx_packet(struct efx_channel *channel, bool *is_from_network);
int efx_u25_rx_get_mports(struct efx_rx_buffer *rx_buf,
	uint32_t *outer_mport, uint32_t *inner_mport);

#endif /* U25_H */
