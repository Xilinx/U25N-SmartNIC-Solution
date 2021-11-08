/****************************************************************************
 * EMCDI Capability Check for Xilinx U25N Accelerator Card.
 * Copyright 2021 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "tc.h"
#include "nic.h"
#include "mcdi.h"
#include "emcdicaps.h"
#include "mc_driver_pcol.h"

#define BITSHIFT(num)			(1ULL << (num))

#define efx_check_emcdi_cap(cap, field)\
	(!!((cap) & (BITSHIFT(MC_CMD_GET_CAPABILITIES_V7_OUT_##field##_LBN))))

int check_emcdi_cap(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_CAPABILITIES_V7_OUT_LEN);
	int rc;
	size_t outlen;
	uint32_t data_caps3;

	BUILD_BUG_ON(MC_CMD_GET_CAPABILITIES_IN_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_CAPABILITIES, NULL, 0, 
			outbuf, sizeof(outbuf), &outlen);
	if (rc) {
		netif_err(efx, drv, efx->net_dev, "RPC failed\n");
		goto fail;
	}
	if (outlen < MC_CMD_GET_CAPABILITIES_V7_OUT_LEN) {
		rc = -EIO;
		netif_err(efx, drv, efx->net_dev, "Outlen does not match\n");
		goto fail;
	}
	data_caps3 = MCDI_DWORD(outbuf, GET_CAPABILITIES_V7_OUT_FLAGS3);
	if (!efx_check_emcdi_cap(data_caps3, ENCAPSULATED_MCDI_SUPPORTED)) {
		rc = -EOPNOTSUPP;
		goto fail;
	}

	return rc;
fail:
	netif_err(efx, drv, efx->net_dev, "Unable to get eMCDI capabilities\n");
	return rc;
}
