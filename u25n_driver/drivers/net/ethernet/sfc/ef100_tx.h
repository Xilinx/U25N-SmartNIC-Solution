/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2019 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_EF100_TX_H
#define EFX_EF100_TX_H

#include "net_driver.h"
#include "efx_common.h"

int ef100_tx_probe(struct efx_tx_queue *tx_queue);
int ef100_tx_init(struct efx_tx_queue *tx_queue);
void ef100_tx_write(struct efx_tx_queue *tx_queue);
void ef100_notify_tx_desc(struct efx_tx_queue *tx_queue);
unsigned int ef100_tx_max_skb_descs(struct efx_nic *efx);

void ef100_ev_tx(struct efx_channel *channel, const efx_qword_t *p_event);

int ef100_enqueue_skb(struct efx_tx_queue *tx_queue, struct sk_buff *skb);
int __ef100_enqueue_skb(struct efx_tx_queue *tx_queue, struct sk_buff *skb,
			struct efx_vfrep *efv);
#endif
