/****************************************************************************
 * FPGA Capability for Xilinx U25N Accelerator Card.
 * Copyright 2021 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef FPGACAPS_H
#define FPGACAPS_H
struct fpgacaps {
	bool mae;
	bool mac;
	uint32_t nvram_id;
};

int get_fpga_vers(struct efx_nic *efx, char *version);

int get_fpga_caps(struct efx_nic *efx, struct fpgacaps *fpga);

int reset_fpga(struct efx_nic *efx);

int select_fpga_nvram(struct efx_nic *efx, struct fpgacaps *fpga);

int get_fpga_nvram(struct efx_nic *efx, struct fpgacaps *fpga);	
#endif
