/****************************************************************************
 * U25 Representor Implementation for Xilinx U25N Accelerator Card.
 * Copyright 2021 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

/* Handling for u25 representor netdevs */
#ifndef U25_REP_H
#define U25_REP_H

#include "net_driver.h"
#include "nic.h"
#include "efx_common.h"

int efx_u25_vfrep_create(struct efx_nic *efx, unsigned int i);
void efx_u25_vfrep_destroy(struct efx_nic *efx, unsigned int i);
int efx_u25_vf_filter_insert(struct efx_nic *efx, unsigned int i);

/* Returns the representor netdevice owning a dynamic m-port, or NULL */
struct net_device *efx_u25_find_vfrep_by_mport(struct efx_nic *efx, u16 mport);

void efx_u25_vfrep_rx_packet(struct efx_vfrep *efv, struct efx_rx_buffer *rx_buf);
void pkt_hex_dump(uint8_t *data, char *func);
extern const struct net_device_ops efx_u25_vfrep_netdev_ops;
#endif /* U25_REP_H */
