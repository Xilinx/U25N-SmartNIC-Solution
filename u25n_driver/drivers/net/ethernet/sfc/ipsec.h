/****************************************************************************
 * IPSEC Offload framework for Xilinx U25N Accelerator Card.
 * Copyright 2021 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef IPSEC_H
#define IPSEC_H
#include "mcdi_pcol_ipsec.h"

int efx_ipsec_ioctl(struct efx_nic *efx, struct ifreq *ifr, int cmd);
int efx_ipsec_init(struct efx_nic *efx);
void efx_ipsec_fini(struct efx_nic *efx);

#endif
