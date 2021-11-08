/****************************************************************************
 * FPGA Capability for Xilinx U25N Accelerator Card.
 * Copyright 2021 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/slab.h>
#include <linux/types.h>
#include <stdbool.h>

#include "tc.h"
#include "nic.h"
#include "mcdi.h"
#include "fpgacaps.h"
#include "mc_driver_pcol.h"

#define BITSHIFT(num)	(1ULL << (num))

#define check_field(cap, field)\
	(!!((cap) & (BITSHIFT(MC_CMD_FPGA_OP_GET_CAPABILITIES_OUT_##field##_LBN))))

/*
 * Get the FPGA design version.
 */
int get_fpga_vers(struct efx_nic *efx, char *version)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_FPGA_OP_GET_VERSION_OUT_LEN(4));
	MCDI_DECLARE_BUF(inbuf, MC_CMD_FPGA_OP_GET_VERSION_IN_LEN);
	size_t outlen;
	int rc;

	if (efx->type->is_vf)
		return -EOPNOTSUPP;

	MCDI_SET_DWORD(inbuf, FPGA_OP_GET_VERSION_IN_OP, MC_CMD_FPGA_IN_OP_GET_VERSION);
	rc = efx_mcdi_rpc(efx, MC_CMD_FPGA, inbuf, sizeof(inbuf), 
			outbuf, sizeof(outbuf), &outlen);
	if (rc) {
		netif_err(efx, drv, efx->net_dev, "RPC failed\n");
		goto fail;
	}

	if (!((outlen > MC_CMD_FPGA_OP_GET_VERSION_OUT_LENMIN) && 
			(outlen < MC_CMD_FPGA_OP_GET_VERSION_OUT_LENMAX))) {
		rc = -EIO;
		netif_err(efx, drv, efx->net_dev, "Length does not match\n");
		goto fail;
	}
	memcpy(version, MCDI_PTR(outbuf, FPGA_OP_GET_VERSION_OUT_VERSION), MC_CMD_FPGA_OP_GET_VERSION_OUT_LEN(4));
	return rc;
fail:
	netif_err(efx, drv, efx->net_dev, "Unable to get FPGA version\n");
	return rc;
}

/*
 * Get the FPGA capabilities (External MAC and MAE)
 */
int get_fpga_caps(struct efx_nic *efx, struct fpgacaps *fpga)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_FPGA_OP_GET_CAPABILITIES_OUT_LEN);
	MCDI_DECLARE_BUF(inbuf, MC_CMD_FPGA_OP_GET_CAPABILITIES_IN_LEN);
	size_t outlen;
	uint32_t caps;
	int rc;

	if (efx->type->is_vf)
		return -EOPNOTSUPP;

	MCDI_SET_DWORD(inbuf, FPGA_OP_GET_CAPABILITIES_IN_OP, MC_CMD_FPGA_IN_OP_GET_CAPABILITIES);
	rc = efx_mcdi_rpc(efx, MC_CMD_FPGA, inbuf, sizeof(inbuf), 
			outbuf, sizeof(outbuf), &outlen);
	if (rc) {
		netif_err(efx, drv, efx->net_dev, "RPC failed\n");
		goto fail;
	}

	if (outlen < MC_CMD_FPGA_OP_GET_CAPABILITIES_OUT_CAPABILITIES_LEN) {
		rc = -EIO;
		netif_err(efx, drv, efx->net_dev, "Length does not match\n");
		goto fail;
	}
	caps = MCDI_DWORD(outbuf, FPGA_OP_GET_CAPABILITIES_OUT_CAPABILITIES);

	fpga->mac = (check_field(caps, MAC) ? true : false);
	fpga->mae = (check_field(caps, MAE) ? true : false);
	return rc;
fail:
	netif_err(efx, drv, efx->net_dev, "Unable to get FPGA capabilities\n");
	return rc;
}

/*
 * Get the FPGA flash used currently
 */
int get_fpga_nvram(struct efx_nic *efx, struct fpgacaps *fpga)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_FPGA_OP_GET_ACTIVE_FLASH_OUT_LEN);
	MCDI_DECLARE_BUF(inbuf, MC_CMD_FPGA_OP_GET_ACTIVE_FLASH_IN_LEN);
	size_t outlen;
	int rc;

	if (efx->type->is_vf)
		return -EOPNOTSUPP;

	MCDI_SET_DWORD(inbuf, FPGA_OP_GET_ACTIVE_FLASH_IN_OP, MC_CMD_FPGA_IN_OP_GET_ACTIVE_FLASH);
	rc = efx_mcdi_rpc(efx, MC_CMD_FPGA, inbuf, sizeof(inbuf), 
			outbuf, sizeof(outbuf), &outlen);
	if (rc) {
		netif_err(efx, drv, efx->net_dev, "RPC failed\n");
		goto fail;
	}

	if (outlen < MC_CMD_FPGA_OP_GET_ACTIVE_FLASH_OUT_LEN) {
		netif_err(efx, drv, efx->net_dev, "Length does not match\n");
		goto fail;
	}
	fpga->nvram_id = MCDI_DWORD(outbuf, FPGA_OP_GET_ACTIVE_FLASH_OUT_FLASH_ID);
	return rc;
fail:
	netif_err(efx, drv, efx->net_dev, "Unable to get read NVRAM ID of FPGA\n");
	return rc;
}

/*
 * Select the FPGA flash
 */
int select_fpga_nvram(struct efx_nic *efx, struct fpgacaps *fpga)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_FPGA_OP_SELECT_FLASH_IN_LEN);
	int rc;

	if (!(efx_is_u25(efx)) || (efx->type->is_vf))
		return -EOPNOTSUPP;

	MCDI_SET_DWORD(inbuf, FPGA_OP_SELECT_FLASH_IN_OP, MC_CMD_FPGA_IN_OP_SELECT_FLASH);
	MCDI_SET_DWORD(inbuf, FPGA_OP_SELECT_FLASH_IN_FLASH_ID, fpga->nvram_id);

	rc = efx_mcdi_rpc(efx, MC_CMD_FPGA, inbuf, sizeof(inbuf), NULL, 0, NULL);
	if (rc) {
		netif_err(efx, drv, efx->net_dev, "RPC failed\n");
		goto fail;
	}
	return rc;
fail:
	netif_err(efx, drv, efx->net_dev, "Unable to get select NVRAM of FPGA\n");
	return rc;
}

/*
 * Resets the FPGA
 */
int reset_fpga(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_FPGA_OP_RESET_IN_LEN);
	int rc;

	if (!(efx_is_u25(efx)) || (efx->type->is_vf))
		return -EOPNOTSUPP;

	MCDI_SET_DWORD(inbuf, FPGA_OP_RESET_IN_OP, MC_CMD_FPGA_IN_OP_RESET);
	rc = efx_mcdi_rpc(efx, MC_CMD_FPGA, inbuf, sizeof(inbuf), NULL, 0, NULL);
	if (rc) {
		netif_err(efx, drv, efx->net_dev, "RPC failed\n");
		goto fail;
	}
	return rc;
fail:
	netif_err(efx, drv, efx->net_dev, "Unable to reset FPGA\n");
	return rc;
}
