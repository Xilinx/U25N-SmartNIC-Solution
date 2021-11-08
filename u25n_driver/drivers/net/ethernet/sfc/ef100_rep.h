/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2019 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

/* Handling for ef100 representor netdevs */
#ifndef EF100_REP_H
#define EF100_REP_H

/* Forward declaration needed by nic.h for efx.h */
struct efx_vfrep;

#include "net_driver.h"
#include "nic.h"

int efx_ef100_vfrep_create(struct efx_nic *efx, unsigned int i);
void efx_ef100_vfrep_destroy(struct efx_nic *efx, unsigned int i);

/* Returns the representor netdevice corresponding to a VF m-port, or NULL
 * @mport is an m-port label, *not* an m-port ID!
 */
struct net_device *efx_ef100_find_vfrep_by_mport(struct efx_nic *efx, u16 mport);

void efx_ef100_vfrep_rx_packet(struct efx_vfrep *efv, struct efx_rx_buffer *rx_buf);

extern const struct net_device_ops efx_ef100_vfrep_netdev_ops;
#endif /* EF10_REP_H */
