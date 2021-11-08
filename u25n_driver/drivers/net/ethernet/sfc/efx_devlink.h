/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2019 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */
#ifndef _EFX_DEVLINK_H
#define _EFX_DEVLINK_H
#include "net_driver.h"

enum efx_info_type {
	EFX_INFO_TYPE_DRIVER,
	EFX_INFO_TYPE_MCFW,
	EFX_INFO_TYPE_SUCFW,
	EFX_INFO_TYPE_NMCFW,
	EFX_INFO_TYPE_CMCFW,
	EFX_INFO_TYPE_FPGA,
	EFX_INFO_TYPE_BOARD_ID,
	EFX_INFO_TYPE_BOARD_REV,
	EFX_INFO_TYPE_SERIAL,
	EFX_INFO_TYPE_MAX,
};

int efx_probe_devlink(struct efx_nic *efx);
void efx_fini_devlink(struct efx_nic *efx);
void efx_devlink_sriov_configure(struct efx_nic *efx);

void efx_devlink_dump_version(void *info, enum efx_info_type type,
			      const char *buf);
int efx_devlink_eswitch_mode_set(struct devlink *devlink,
                uint16_t mode, struct netlink_ext_ack *extack);
#if !defined(EFX_USE_KCOMPAT) || defined(EFX_HAVE_DEVLINK)
#ifdef CONFIG_NET_DEVLINK
#include <net/devlink.h>

/* This is the private data we have in struct devlink */
struct efx_devlink {
	struct efx_nic *efx;
	struct devlink_port dl_port;
	uint16_t eswitch_mode;
	uint32_t vf_count;
};

struct devlink_port *efx_get_devlink_port(struct net_device *dev);
#endif
#endif	/* EFX_HAVE_DEVLINK */

#endif	/* _EFX_DEVLINK_H */
