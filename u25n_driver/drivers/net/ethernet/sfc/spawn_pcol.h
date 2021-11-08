/****************************************************************************
 * Auto spwaning application in PS for Xilinx U25N Accelerator Card.
 * Copyright 2021 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef SPAWN_PCOL_H
#define SPAWN_PCOL_H

#define MC_CMD_SPAWN_IMGUP (0x253)
#define MC_CMD_SPAWN_IMGUP_IN_LEN (0)

#define MC_CMD_STOP_IMGUP (0x254)
#define MC_CMD_STOP_IMGUP_IN_LEN (0)

#define MC_CMD_SPAWN_FLASHUPGRADE (0x255)
#define MC_CMD_SPAWN_FLASHUPGRADE_IN_LEN (0)

#define MC_CMD_STOP_FLASHUPGRADE (0x256)
#define MC_CMD_STOP_FLASHUPGRADE_IN_LEN (0)

#define MC_CMD_MAE_LAUNCH_APPLICATION 0x180
#define MC_CMD_KILL_APPLICATION 0x181
#endif
