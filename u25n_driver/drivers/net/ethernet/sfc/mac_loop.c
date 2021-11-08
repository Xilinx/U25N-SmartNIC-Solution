/****************************************************************************
 * MAC Loop functionality  for Xilinx U25N Accelerator Card.
 * Copyright 2021 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "nic.h"
#include "efx.h"
#include "enum.h"
#include "debugfs.h"
#include "mac_loop.h"
#include "selftest.h"
#include "efx_common.h"
#include "net_driver.h"
#include "mcdi_port_common.h"

#define BITSHIFT(num)	(1ULL << (num))
#define check_loopback_cap(field, mode)\
	(!!((field) & BITSHIFT(mode)))

static int efx_mcdi_get_loopback_modes(struct efx_nic *efx, u64 *loopback_modes)
{
        MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_LOOPBACK_MODES_OUT_V2_LEN);
        size_t outlen;
        int rc;

        rc = efx_mcdi_rpc(efx, MC_CMD_GET_LOOPBACK_MODES, NULL, 0, 
			outbuf, sizeof(outbuf), &outlen);
        if (rc)
                goto ret;

        if (outlen < (MC_CMD_GET_LOOPBACK_MODES_OUT_V2_25G_OFST +
                      MC_CMD_GET_LOOPBACK_MODES_OUT_V2_25G_LEN)) {
                rc = -EIO;
                goto ret;
	}

        *loopback_modes = MCDI_QWORD(outbuf, GET_LOOPBACK_MODES_OUT_V2_25G);
        return 0;
ret:
        netif_err(efx, hw, efx->net_dev, "Failed to get supported loopback mode rc=%d\n", rc);
        return rc;
}

static int efx_mac_set_loopback(struct efx_nic *efx, u32 caps, u32 mode)
{
	u32 prev_mode;
	int rc;

	prev_mode = efx->loopback_mode;
	if (prev_mode == mode)
		return 0;

	efx->loopback_mode = mode;
	rc = efx_mcdi_set_link(efx, caps, efx_get_mcdi_phy_flags(efx), 
			efx->loopback_mode, false, SET_LINK_SEQ_IGNORE);
	if (rc)
		netif_err(efx, drv, efx->net_dev, "Failed to set loopback mode\n");
	else
		netif_info(efx, drv, efx->net_dev, "Loopback mode: [%s]\n", LOOPBACK_MODE(efx));

	return rc;
}

int efx_mac_loopback(struct efx_nic *efx, bool set_loopback)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_LINK_OUT_V2_LEN);
	u64 loopback_modes;
	u32 current_mode;
	u32 caps;
	int rc;

	/* Read initial link advertisement */
	BUILD_BUG_ON(MC_CMD_GET_LINK_IN_LEN != 0);
	rc = efx_mcdi_rpc(efx, MC_CMD_GET_LINK, NULL, 0, outbuf, sizeof(outbuf), NULL);
	if (rc)
		goto ret;

	caps = MCDI_DWORD(outbuf, GET_LINK_OUT_V2_CAP);
	rc = efx_mcdi_get_loopback_modes(efx, &loopback_modes);
	current_mode = MCDI_DWORD(outbuf, GET_LINK_OUT_V2_LOOPBACK_MODE);

	if (set_loopback) {
		if (!check_loopback_cap(loopback_modes, LOOPBACK_AOE_INT_NEAR)) {
			netif_err(efx, drv, efx->net_dev, "Unsupported loopback mode\n");
			rc = -EOPNOTSUPP;
			goto ret;
		}
		rc = efx_mac_set_loopback(efx, caps, LOOPBACK_AOE_INT_NEAR);
		if (rc)
			goto ret;
	} else {
		rc = efx_mac_set_loopback(efx, caps, LOOPBACK_NONE);
		if (rc)
			goto ret;
	}
ret:
	return rc;
}
