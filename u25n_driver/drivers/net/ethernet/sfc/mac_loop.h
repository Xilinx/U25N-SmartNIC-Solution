/****************************************************************************
 * MAC Loop functionality  for Xilinx U25N Accelerator Card.
 * Copyright 2021 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef MAC_LOOP
#define MAC_LOOP

int efx_mac_loopback(struct efx_nic *efx, bool set);

#endif
