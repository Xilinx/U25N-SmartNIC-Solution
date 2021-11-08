/****************************************************************************
 * Link status for Xilinx U25N Accelerator Card.
 * Copyright 2021 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef LINKMODE_H
#define LINKMODE_H

int efx_ext_mae_get_link(struct efx_nic *efx, uint8_t *mode);

int efx_ext_mae_set_link(struct efx_nic *efx, uint8_t mode);
#endif
