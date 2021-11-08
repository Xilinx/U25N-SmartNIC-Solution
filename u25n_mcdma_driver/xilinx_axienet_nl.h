// SPDX-License-Identifier: GPL-2.0-only
/*
 * EMCDI framework support in Xilinx Axi Ethernet device driver
 * Copyright (c) 2021  Xilinx, Inc. All rights reserved.
 *
 * This is a driver for the  Xilinx Axi Ethernet which is used in the Virtex6
 * and Spartan6.
 */

#include <net/netlink.h>
#include <net/sock.h>

#define NL_TYPE_CAM 1
#define NL_TYPE_ACK 3
#define NL_TYPE_PID 5
#define NL_TYPE_LOGS 7

int u25_netlink_init(void);
int u25_netlink_exit(void);


