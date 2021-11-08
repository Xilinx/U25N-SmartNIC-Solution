/****************************************************************************
 * Link status for Xilinx U25N Accelerator Card.
 * Copyright 2021 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "tc.h"
#include "nic.h"
#include "mcdi.h"
#include "linkmode.h"
#include "mc_driver_pcol.h"

int efx_ext_mae_get_link(struct efx_nic *efx, uint8_t *mode)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_EXTERNAL_MAE_GET_LINK_MODE_OUT_LEN);
	int rc;
	size_t outlen;

	rc = efx_mcdi_rpc(efx, MC_CMD_EXTERNAL_MAE_GET_LINK_MODE, NULL, 0,
			outbuf, sizeof(outbuf), &outlen);
	if (rc) {
		netif_err(efx, drv, efx->net_dev, "Error in RPC\n");
		return rc;
	}
	if (outlen < MC_CMD_EXTERNAL_MAE_GET_LINK_MODE_OUT_LEN)
		return -EIO;
	
	*mode = MCDI_DWORD(outbuf, EXTERNAL_MAE_GET_LINK_MODE_OUT_MODE);
	
	return rc;
}

int efx_ext_mae_set_link(struct efx_nic *efx, uint8_t mode)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_EXTERNAL_MAE_SET_LINK_MODE_IN_LEN);
	int rc;

	MCDI_SET_DWORD(inbuf, EXTERNAL_MAE_SET_LINK_MODE_IN_MODE, mode);
	rc = efx_mcdi_rpc(efx, MC_CMD_EXTERNAL_MAE_SET_LINK_MODE, inbuf, 
			sizeof(inbuf), NULL, 0, NULL);
	
	if (rc) {
		netif_err(efx, drv, efx->net_dev, "Error in RPC\n");
		return rc;
	}
	return rc;
}
