/****************************************************************************
 * Encapsulated MCDI Interface for Xilinx U25N Accelerator Card.
 * Copyright 2021 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/rtc.h>
#include "net_driver.h"
#include "nic.h"
#include "efx_common.h"
#include "efx_devlink.h"
#include "io.h"
#include "farch_regs.h"
#include "mcdi_pcol.h"
#include "aoe.h"
#include "mae.h"
#include "emcdi.h"
#include "rx_common.h"
#include "logger.h"

struct efx_emcdi_blocking_data {
	struct kref ref;
	bool done;
	wait_queue_head_t wq;
	int rc;
	efx_dword_t *outbuf;
	size_t outlen;
	size_t outlen_actual;
};

struct efx_emcdi_copy_buffer {
	_MCDI_DECLARE_BUF(buffer, MCDI_CTL_SDU_LEN_MAX);
};

static int efx_emcdi_cmd_start_or_queue_ext(struct efx_emcdi_iface *emcdi,
		struct efx_emcdi_cmd *cmd,
		struct efx_emcdi_copy_buffer *copybuf);
static void efx_emcdi_remove_cmd(struct efx_emcdi_iface *emcdi,
		struct efx_emcdi_cmd *cmd,
		struct list_head *cleanup_list);
#ifdef CONFIG_SFC_MCDI_LOGGING
extern bool mcdi_logging_default;
#define LOG_LINE_MAX (1024 - 32)
#endif

static void _efx_emcdi_display_error(struct efx_nic *efx, unsigned int cmd,
		size_t inlen, int raw, int arg, int rc)
{
	netif_cond_dbg(efx, hw, efx->net_dev,
			rc == -EPERM || efx_nic_hw_unavailable(efx), err,
			"eMCDI command 0x%x inlen %d failed rc=%d (raw=%d) arg=%d\n",
			cmd, (int)inlen, rc, raw, arg);
}

static uint8_t efx_emcdi_get_header_type(uint8_t emcdi_type)
{
	switch(emcdi_type) {
		case EMCDI_TYPE_MAE:
			return EMCDI_HEADER_TYPE_CONTROL;
		case EMCDI_TYPE_IPSEC:
			return EMCDI_HEADER_TYPE_IPSEC;
		/* Switch case for Image Upgrade */
		case EMCDI_TYPE_IMG:
			return EMCDI_HEADER_TYPE_IMG;
		case EMCDI_TYPE_CONTROLLER:
			return EMCDI_HEADER_TYPE_CONTROLLER;
		case EMCDI_TYPE_FLASH_UPGRADE:
			return EMCDI_HEADER_TYPE_FLASH_UPGRADE;
		case EMCDI_TYPE_LOGGER:
                        return EMCDI_HEADER_TYPE_LOGGER;
		case EMCDI_TYPE_FIREWALL:
			return EMCDI_HEADER_TYPE_FIREWALL;
		default:
			return EMCDI_HEADER_TYPE_CONTROL;
	}
	return -1;
}

static uint8_t efx_emcdi_get_emcdi_type(uint8_t header_type)
{
	switch(header_type) {
		case EMCDI_HEADER_TYPE_CONTROL:
		case EMCDI_HEADER_TYPE_COUNTER:
		case EMCDI_HEADER_TYPE_COUNTER_ACK:
			return EMCDI_TYPE_MAE;
		case EMCDI_HEADER_TYPE_IPSEC:
			return EMCDI_TYPE_IPSEC;
		/* Switch case for Image Upgrade */
		case EMCDI_HEADER_TYPE_IMG:
			return EMCDI_TYPE_IMG;
		case EMCDI_HEADER_TYPE_CONTROLLER:
			return EMCDI_TYPE_CONTROLLER;
		case EMCDI_HEADER_TYPE_FLASH_UPGRADE:
			return EMCDI_TYPE_FLASH_UPGRADE;
		case EMCDI_HEADER_TYPE_LOGGER:
                        return EMCDI_TYPE_LOGGER;
		case EMCDI_HEADER_TYPE_FIREWALL:
			return EMCDI_TYPE_FIREWALL;
		default:
			return -1;
	}
	return -1;
}

static bool efx_emcdi_flushed(struct efx_emcdi_iface *emcdi, bool ignore_cleanups)
{
	bool flushed;

	spin_lock_bh(&emcdi->iface_lock);
	flushed = list_empty(&emcdi->cmd_list) &&
		(ignore_cleanups || !emcdi->outstanding_cleanups);
	spin_unlock_bh(&emcdi->iface_lock);
	return flushed;
}

/* Wait for outstanding eMCDI commands to complete. */
void efx_emcdi_wait_for_cleanup(struct efx_emcdi_iface *emcdi)
{
	wait_event(emcdi->cmd_complete_wq,
			efx_emcdi_flushed(emcdi, false));
}

static bool efx_emcdi_check_timeout(struct efx_emcdi_cmd *cmd)
{
	return time_after(jiffies, cmd->started + EMCDI_RPC_TIMEOUT);
}

static bool efx_emcdi_cmd_cancelled(struct efx_emcdi_cmd *cmd)
{
	return cmd->state == EMCDI_STATE_RUNNING_CANCELLED;
}

static void efx_emcdi_cmd_release(struct kref *ref)
{
	kfree(container_of(ref, struct efx_emcdi_cmd, ref));
}

static void efx_emcdi_blocking_data_release(struct kref *ref)
{
	kfree(container_of(ref, struct efx_emcdi_blocking_data, ref));
}

static void efx_emcdi_process_cleanup_list(struct efx_emcdi_iface *emcdi, 
		struct list_head *cleanup_list)
{
	unsigned int cleanups = 0;

	while (!list_empty(cleanup_list)) {
		struct efx_emcdi_cmd *cmd =
			list_first_entry(cleanup_list,
					struct efx_emcdi_cmd, cleanup_list);
		list_del(&cmd->cleanup_list);
		kref_put(&cmd->ref, efx_emcdi_cmd_release);
		++cleanups;
	}

	if (cleanups) {
		bool all_done;

		spin_lock_bh(&emcdi->iface_lock);
		EFX_WARN_ON_PARANOID(cleanups > emcdi->outstanding_cleanups);
		all_done = (emcdi->outstanding_cleanups -= cleanups) == 0;
		spin_unlock_bh(&emcdi->iface_lock);
		if (all_done)
			wake_up(&emcdi->cmd_complete_wq);
	}
}

/* try to advance to commands */
static void efx_emcdi_start_or_queue(struct efx_emcdi_iface *emcdi,
		bool allow_retry,
		struct efx_emcdi_copy_buffer *copybuf)
{
	struct efx_emcdi_cmd *cmd, *tmp;

	list_for_each_entry_safe(cmd, tmp, &emcdi->cmd_list, list)
		if (cmd->state == EMCDI_STATE_QUEUED ||
				(cmd->state == EMCDI_STATE_RETRY && allow_retry))
			efx_emcdi_cmd_start_or_queue_ext(emcdi, cmd, copybuf);
}

static bool efx_emcdi_complete_cmd(struct efx_emcdi_iface *emcdi,
		struct efx_emcdi_cmd *cmd, uint8_t *data,
		struct efx_emcdi_copy_buffer *copybuf,
		struct list_head *cleanup_list)
{
	efx_dword_t *outbuf = copybuf ? copybuf->buffer : NULL;
	size_t resp_hdr_len = 0, resp_data_len = 0;
	efx_dword_t *hdr = (efx_dword_t *) data;
	unsigned int respseq, respcmd, error;
	bool completed = false;
	int rc = -ETIMEDOUT;

	/* ensure the command can't go away before this function returns */
	kref_get(&cmd->ref);

	if (!data)
		goto out;

	respseq = EFX_DWORD_FIELD(hdr[0], MCDI_HEADER_SEQ);
	respcmd = EFX_DWORD_FIELD(hdr[0], MCDI_HEADER_CODE);
	error = EFX_DWORD_FIELD(hdr[0], MCDI_HEADER_ERROR);

	if (respcmd != MC_CMD_V2_EXTN) {
		resp_hdr_len = 4;
		resp_data_len = EFX_DWORD_FIELD(hdr[0], MCDI_HEADER_DATALEN);
	} else {
		respcmd = EFX_DWORD_FIELD(hdr[1], MC_CMD_V2_EXTN_IN_EXTENDED_CMD);
		resp_hdr_len = 8;
		resp_data_len =
			EFX_DWORD_FIELD(hdr[1], MC_CMD_V2_EXTN_IN_ACTUAL_LEN);
	}

	data += resp_hdr_len;

	if (error && resp_data_len == 0) {
		rc = -EIO;
	} else if (!outbuf) {
		rc = -ENOMEM;
	} else {
		if (WARN_ON_ONCE(error && resp_data_len < 4))
			resp_data_len = 4;

		memcpy(outbuf, data, resp_data_len);
#ifdef CONFIG_SFC_MCDI_LOGGING
		if (emcdi->logging_enabled) {
			char *buf = emcdi->logging_buffer; /* page-sized */
			const efx_dword_t *frags[] = { hdr, outbuf};
			size_t frag_len[] = { resp_hdr_len, resp_data_len};
			const efx_dword_t *frag;
			int bytes = 0;
			int i, j;
			unsigned int dcount = 0;
			/* Header length should always be a whole number of dwords,
			 * so scream if it's not.
			 */
			WARN_ON_ONCE(resp_hdr_len % 4);
			for (j = 0; j < ARRAY_SIZE(frags); j++) {
				frag = frags[j];
				for (i = 0;
						i < frag_len[j] / 4;
						i++) {
					/* Do not exceeed the internal printk limit.
					 * The string before that is just over 70 bytes.
					 */
					if ((bytes + 75) > LOG_LINE_MAX) {
						netif_info(emcdi->efx, hw, emcdi->efx->net_dev,
								"EMCDI RPC RESP:%s \\\n", buf);
						dcount = 0;
						bytes = 0;
					}
					bytes += snprintf(buf + bytes,
							LOG_LINE_MAX - bytes, " %08x",
							le32_to_cpu(frag[i].u32[0]));
					dcount++;
				}
			}
			netif_info(emcdi->efx, hw, emcdi->efx->net_dev, "EMCDI RPC RESP:%s\n", buf);
		}
#endif
		if (error) {
			int err_arg = 0;
			rc = EFX_DWORD_FIELD(outbuf[0], EFX_DWORD_0);
#ifdef WITH_MCDI_V2
			if (resp_data_len >= MC_CMD_ERR_ARG_OFST + 4) {
				hdr = (efx_dword_t *) (data + MC_CMD_ERR_ARG_OFST);
				err_arg = EFX_DWORD_VAL(hdr[0]);
			}
#endif
			_efx_emcdi_display_error(emcdi->efx, cmd->cmd,
					cmd->inlen, rc, err_arg,
					efx_mcdi_errno(emcdi->efx, rc));
			rc = efx_mcdi_errno(emcdi->efx, rc);
			rc = -EIO;
		} else {
			rc = 0;
		}
	}

out:
	emcdi->pending = NULL;

	if (efx_emcdi_cmd_cancelled(cmd)) {
		list_del(&cmd->list);
		kref_put(&cmd->ref, efx_emcdi_cmd_release);
		completed = true;
	} else if (rc == MC_CMD_ERR_QUEUE_FULL) {
		cmd->state = EMCDI_STATE_RETRY;
	} else {
		cmd->rc = rc;
		cmd->outbuf = outbuf;
		cmd->outlen = outbuf ? resp_data_len : 0;
		cmd->completer(emcdi->efx, cmd->cookie, cmd->rc,
				cmd->outbuf, cmd->outlen);
		efx_emcdi_remove_cmd(emcdi, cmd, cleanup_list);
		completed = true;
	}

	efx_emcdi_start_or_queue(emcdi, rc != MC_CMD_ERR_QUEUE_FULL,
			NULL);

	/* wake up anyone waiting for flush */
	wake_up(&emcdi->cmd_complete_wq);

	kref_put(&cmd->ref, efx_emcdi_cmd_release);

	return completed;
}

static void efx_emcdi_process_message(struct efx_nic *efx, uint8_t *data,
		uint8_t type, uint16_t seq_num)
{
	struct efx_emcdi_copy_buffer *copybuf =
		kmalloc(sizeof(struct efx_emcdi_copy_buffer), GFP_ATOMIC);
	struct efx_emcdi_iface *emcdi = efx_emcdi(efx, type);
	struct efx_emcdi_cmd *cmd;
	LIST_HEAD(cleanup_list);

	if (!emcdi->enabled) {
		netif_err(efx, hw, efx->net_dev,
				"eMCDI response unexpected tx type 0x%x or type not enabled\n",
				type);
		return;
	}

	if (seq_num != emcdi->prev_seq) {
		netif_err(efx, hw, efx->net_dev,
				"eMCDI response unexpected tx seq 0x%x\n",
				seq_num);
		/* this could theoretically just be a race between command
		 * time out and processing the completion event,  so while not
		 * a good sign, it'd be premature to attempt any recovery.
		 */
		return;
	}

	spin_lock(&emcdi->iface_lock);
	cmd = emcdi->pending;
	if (cmd) {
		kref_get(&cmd->ref);
		if (efx_emcdi_complete_cmd(emcdi, cmd, data, copybuf, &cleanup_list))
			if (cancel_delayed_work(&cmd->work))
				kref_put(&cmd->ref, efx_emcdi_cmd_release);
		kref_put(&cmd->ref, efx_emcdi_cmd_release);
	} else {
		netif_err(efx, hw, efx->net_dev,
				"eMCDI response unexpected, command not found\n");
	}
	spin_unlock(&emcdi->iface_lock);

	efx_emcdi_process_cleanup_list(emcdi, &cleanup_list);

	kfree(copybuf);
}

/* We always swallow the packet, whether successful or not, since it's not
 * a network packet and shouldn't ever be forwarded to the stack
 */
static bool efx_emcdi_rx(struct efx_channel *channel)
{
	struct efx_rx_buffer *rx_buf =
		efx_rx_buffer(&channel->rx_queue, channel->rx_pkt_index);
	struct efx_ef10_nic_data *nic_data = channel->efx->nic_data;
	struct efx_nic *efx = channel->efx;
	uint32_t outer_mport, inner_mport;
	u8 *data = efx_rx_buf_va(rx_buf);
	struct emcdi_hdr *hdr;
	const char *reason;
	uint8_t type;
	u16 seq_num;
	int rc;
	unsigned int resp_cmd;

	rc = efx_u25_rx_get_mports(rx_buf, &outer_mport, &inner_mport);
	if (rc) {
		reason = "eMCDI header is invalid";
		goto fail;
	}

	if (!nic_data->have_pf_mport) {
		rc = -EINVAL;
		reason = "PF mport not allocated";
		goto fail;
	}

	/* validate counter packet. */
	if ((outer_mport != nic_data->pf_mport) ||
			(inner_mport != U25_MPORT_ID_CONTROL)) {
		rc = -EINVAL;
		reason = "mport id is invalid";
		goto fail;
	}

	hdr = (struct emcdi_hdr *) data;
	type = efx_emcdi_get_emcdi_type(hdr->type);
	if(type < 0) {
		rc = -EINVAL;
		reason = "emcdi header type id is invalid";
		goto fail;
	}
	seq_num = ntohs(hdr->seq_num);
	
	data = data + sizeof (struct emcdi_hdr);

	if(type == EMCDI_TYPE_LOGGER) {
                efx_dword_t *hdr = (efx_dword_t *) data;
                resp_cmd = EFX_DWORD_FIELD(hdr[0], MCDI_HEADER_CODE);
                if (resp_cmd == MC_CMD_V2_EXTN)
                        resp_cmd = EFX_DWORD_FIELD(hdr[1], MC_CMD_V2_EXTN_IN_EXTENDED_CMD);
                if(resp_cmd == MC_CMD_STOP_LOGGER || resp_cmd == MC_CMD_START_REQUEST_LOGGER) {
                         efx_emcdi_process_message(efx, data, type, seq_num);
		} else
                         efx_emcdi_process_logs_message(efx, data, (rx_buf->len - sizeof(struct emcdi_hdr)));

        } else
		efx_emcdi_process_message(efx, data, type, seq_num);

	goto out;
fail:
	if (net_ratelimit())
		netif_err(efx, drv, efx->net_dev,
				"choked on eMCDI packet (%s rc %d)\n",
				reason, rc);
out:
	efx_free_rx_buffers(&channel->rx_queue, rx_buf, 1);
	channel->rx_pkt_n_frags = 0;
	return true;
}

static void efx_emcdi_handle_no_channel(struct efx_nic *efx)
{
	netif_warn(efx, drv, efx->net_dev,
			"Encapsulated MCDI require MSI-X and 1 additional interrupt vector.\n");
}

static int efx_emcdi_probe_channel(struct efx_channel *channel)
{
	struct efx_nic *efx = channel->efx;

	if(!efx->emcdi)
		return -ENOMEM;

	efx->emcdi->channel = channel;

	spin_lock_init(&efx->emcdi->emcdi_tx_lock);
	return 0;
}

static int efx_emcdi_start_channel(struct efx_channel *channel)
{
	struct efx_ef10_nic_data *nic_data = channel->efx->nic_data;
	struct efx_nic *efx = channel->efx;
	//u8 mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

	if (nic_data->have_pf_mport) {
		int rc;
		struct efx_filter_spec spec;
		u16 outer_vlan = U25_MPORT_TO_VLAN(nic_data->pf_mport);
		u16 inner_vlan = U25_MPORT_TO_VLAN(U25_MPORT_ID_CONTROL);

		efx_filter_init_rx(&spec, EFX_FILTER_PRI_REQUIRED, 0,
				efx_rx_queue_index(efx_channel_get_rx_queue(channel)));

		spec.match_flags |= EFX_FILTER_MATCH_LOC_MAC;
		eth_broadcast_addr(spec.loc_mac);
		//ether_addr_copy(spec.loc_mac, mac);
		spec.match_flags |= EFX_FILTER_MATCH_OUTER_VID;
		spec.outer_vid = htons(outer_vlan);
		spec.match_flags |= EFX_FILTER_MATCH_INNER_VID;
		spec.inner_vid = htons(inner_vlan);

		rc = efx_filter_insert_filter(efx, &spec, true);
		if (rc < 0)
			netif_warn(efx, drv, efx->net_dev,
					"Failed to add filter, rc=%d.\n", rc);
		else
			nic_data->rxfilter_emcdi = rc;
	}

	return 0;
}

static void efx_emcdi_stop_channel(struct efx_channel *channel)
{
	struct efx_ef10_nic_data *nic_data = channel->efx->nic_data;
	struct efx_nic *efx = channel->efx;

	if (nic_data->have_pf_mport)
		efx_filter_remove_id_safe(efx, EFX_FILTER_PRI_REQUIRED,
				nic_data->rxfilter_emcdi);
}

static void efx_emcdi_remove_channel(struct efx_channel *channel)
{
	struct efx_nic *efx = channel->efx;

	if(efx->emcdi)
		efx->emcdi->channel = NULL;
		
}

static void efx_emcdi_get_channel_name(struct efx_channel *channel,
		char *buf, size_t len)
{
	snprintf(buf, len, "%s-emcdi", channel->efx->name);
}

static const struct efx_channel_type efx_emcdi_channel_type = {
	.handle_no_channel      = efx_emcdi_handle_no_channel,
	.pre_probe              = efx_emcdi_probe_channel,
	.start                  = efx_emcdi_start_channel,
	.stop                   = efx_emcdi_stop_channel,
	.post_remove            = efx_emcdi_remove_channel,
	.get_name               = efx_emcdi_get_channel_name,
	/* no copy operation; there is no need to reallocate this channel */
	.receive_raw            = efx_emcdi_rx,
	.keep_eventq            = true,
	.hide_tx                = true,
};

int efx_emcdi_init_channel(struct efx_nic *efx)
{
	efx->emcdi = kzalloc(sizeof(*efx->emcdi), GFP_KERNEL);
	if (!efx->emcdi)
		return -ENOMEM;

	/*New Channel for Encapsulated MCDI*/
	efx->extra_channel_type[EFX_EXTRA_CHANNEL_EMCDI] = &efx_emcdi_channel_type;
	return 0;
}

void efx_emcdi_fini_channel(struct efx_nic *efx)
{
	if (efx->emcdi)
		kfree(efx->emcdi);
}

/* Encapsulated MCDI Initilization.
*/
int efx_emcdi_init(struct efx_nic *efx, uint8_t type)
{
	struct efx_emcdi_iface *emcdi;
	int rc = -ENOMEM, rc1;
	char name[32];

	if (!efx->emcdi->channel)
		return -ENETDOWN;

	emcdi = efx_emcdi(efx, type);
	if (emcdi->enabled)
		return 0;

	emcdi->efx = efx;
#ifdef CONFIG_SFC_MCDI_LOGGING
	emcdi->logging_buffer = kmalloc(LOG_LINE_MAX, GFP_ATOMIC);
	if (!emcdi->logging_buffer)
		return -ENOMEM;
	emcdi->logging_enabled = mcdi_logging_default;
#endif

	spin_lock_init(&emcdi->iface_lock);
	INIT_LIST_HEAD(&emcdi->cmd_list);
	init_waitqueue_head(&emcdi->cmd_complete_wq);

	snprintf(name, 32, "emcdi_queue_%d", type);
	emcdi->work_queue = create_workqueue(name);
	if (!emcdi->work_queue) {
		goto fail;
	}
	emcdi->type = type;
	emcdi->enabled = true;
#if 1
	if(emcdi->type == EMCDI_TYPE_MAE) {
		rc1 = efx_mae_start_counters(efx, efx->emcdi->channel);
		if (rc1)
			netif_warn(efx, drv, efx->net_dev,
                           "Failed to start MAE counters streaming, rc=%d.\n",
                           rc1);
		snprintf(name, 32, "counter_queue_%d", type);
		efx->cnt_queue = create_workqueue(name);
		if (!efx->cnt_queue) {
			goto fail;
		}
	}
#endif

	return 0;
fail:
#ifdef CONFIG_SFC_MCDI_LOGGING
	kfree(emcdi->logging_buffer);
#endif
	return rc;
}

void efx_emcdi_fini(struct efx_nic *efx, uint8_t type)
{
	struct efx_emcdi_iface *emcdi;
	int rc;

	emcdi = efx_emcdi(efx, type);
	if (!emcdi->enabled)
		return;
#if 1
	if (type == EMCDI_TYPE_MAE) {
		destroy_workqueue(efx->cnt_queue);
		rc = efx_mae_stop_counters(efx, efx->emcdi->channel);
		if (rc)
			netif_warn(efx, drv, efx->net_dev,
					"Failed to stop MAE counters streaming, rc=%d.\n",
					rc);
	}
#endif
	efx_emcdi_wait_for_cleanup(emcdi);
#ifdef CONFIG_SFC_MCDI_LOGGING
	kfree(emcdi->logging_buffer);
#endif

	destroy_workqueue(emcdi->work_queue);
	emcdi->enabled = false;
}

static void _efx_emcdi_remove_cmd(struct efx_emcdi_iface *emcdi,
		struct efx_emcdi_cmd *cmd,
		struct list_head *cleanup_list)
{
	/* if cancelled, the completers have already been called */
	if (efx_emcdi_cmd_cancelled(cmd))
		return;

	if (cmd->completer) {
		list_add_tail(&cmd->cleanup_list, cleanup_list);
		++emcdi->outstanding_cleanups;
		kref_get(&cmd->ref);
	}
}

static void efx_emcdi_remove_cmd(struct efx_emcdi_iface *emcdi,
		struct efx_emcdi_cmd *cmd,
		struct list_head *cleanup_list)
{
	list_del(&cmd->list);
	_efx_emcdi_remove_cmd(emcdi, cmd, cleanup_list);
	cmd->state = EMCDI_STATE_FINISHED;
	kref_put(&cmd->ref, efx_emcdi_cmd_release);
	if (list_empty(&emcdi->cmd_list))
		wake_up(&emcdi->cmd_complete_wq);
}

static void efx_emcdi_timeout_cmd(struct efx_emcdi_iface *emcdi,
		struct efx_emcdi_cmd *cmd,
		struct list_head *cleanup_list)
{
	struct efx_nic *efx = emcdi->efx;

	netif_err(efx, drv, efx->net_dev,
			"eMCDI command 0x%x inlen %zu state %d timed out after %u ms\n",
			cmd->cmd, cmd->inlen, cmd->state,
			jiffies_to_msecs(jiffies - cmd->started));

	cmd->state = EMCDI_STATE_RETRY;
	if (++cmd->retry < EMCDI_MAX_RETRY) {
		efx_emcdi_start_or_queue(emcdi, true, NULL);
	} else {
		cmd->rc = -ETIMEDOUT;
		emcdi->pending = NULL;
		efx_emcdi_remove_cmd(emcdi, cmd, cleanup_list);
	}
}

static void efx_emcdi_send_func(struct efx_nic *efx, struct sk_buff *skb)
{
	struct efx_tx_queue *tx_queue;

	tx_queue = efx->select_tx_queue(efx->emcdi->channel, skb);
	if (tx_queue) {
		efx_enqueue_skb(tx_queue, skb);
		/* If netdev_xmit_more() was true in enqueue_skb() then our
		 * queue will be waiting for the next packet to push the
		 * doorbell. Since the next packet might not be coming this
		 * way (if it doesn't need a timestamp) we need to push it
		 * directly.
		 */
		efx_nic_push_buffers(tx_queue);
	} else {
		WARN_ONCE(1, "EMCDI tx queue not found\n");
		dev_kfree_skb_any(skb);
	}
}

static int efx_emcdi_send_request(struct efx_emcdi_iface *emcdi,
		struct efx_emcdi_cmd *cmd)
{
	u8 emcdi_src_mac_addr[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
	//u8 emcdi_dst_mac_addr[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
	size_t hdr_len = (emcdi->efx->type->mcdi_max_ver == 1) ? 4 : 8;
	struct efx_ef10_nic_data *nic_data = emcdi->efx->nic_data;
	struct efx_nic *efx = emcdi->efx;
	struct emcdi_hdr *emcdi_hdr;
	size_t inlen = cmd->inlen;
	struct sk_buff *skb;
	efx_dword_t *hdr;

	if (!emcdi->enabled)
		return -ENOMEM;
	if (!nic_data->have_pf_mport) {
		/* pf mport is not allocated */
		return -ENOMEM;
	}

	emcdi->prev_seq = cmd->seq;
	emcdi->pending = cmd;
	cmd->started = jiffies;

	/* Allocate an SKB to store the headers */
	skb = netdev_alloc_skb(efx->net_dev, MAX_EMCDI_PACKET_LEN);
	if (unlikely(skb == NULL)) {
		atomic_inc(&efx->n_rx_noskb_drops);
		return -ENOMEM;
	}
	skb_reserve(skb, hdr_len + sizeof(struct emcdi_hdr));
	skb_put(skb, inlen);

	memcpy(skb->data, cmd->inbuf, cmd->inlen);

	skb_push(skb, hdr_len);
	hdr = (efx_dword_t *) skb->data;
	if (efx->type->mcdi_max_ver == 1) {
		/* MCDI v1 */
		EFX_POPULATE_DWORD_7(hdr[0],
				MCDI_HEADER_RESPONSE, 0,
				MCDI_HEADER_RESYNC, 1,
				MCDI_HEADER_CODE, cmd->cmd,
				MCDI_HEADER_DATALEN, inlen,
				MCDI_HEADER_SEQ, 0, /* keeping 0 for emcdi */
				MCDI_HEADER_XFLAGS, 0,
				MCDI_HEADER_NOT_EPOCH, 0);
	} else {
		/* MCDI v2 */
		BUG_ON(inlen > MCDI_CTL_SDU_LEN_MAX_V2);
		EFX_POPULATE_DWORD_7(hdr[0],
				MCDI_HEADER_RESPONSE, 0,
				MCDI_HEADER_RESYNC, 1,
				MCDI_HEADER_CODE, MC_CMD_V2_EXTN,
				MCDI_HEADER_DATALEN, 0,
				MCDI_HEADER_SEQ, 0, /* keeping 0 for emcdi */
				MCDI_HEADER_XFLAGS, 0,
				MCDI_HEADER_NOT_EPOCH, 0);
		EFX_POPULATE_DWORD_2(hdr[1],
				MC_CMD_V2_EXTN_IN_EXTENDED_CMD, cmd->cmd,
				MC_CMD_V2_EXTN_IN_ACTUAL_LEN, inlen);
	}

	/* Fill emcdi header. */
	skb_push(skb, sizeof(struct emcdi_hdr));
	emcdi_hdr = (struct emcdi_hdr *) skb->data;
	eth_broadcast_addr(emcdi_hdr->h_dest);
	//ether_addr_copy(emcdi_hdr->h_dest, emcdi_dst_mac_addr);
	ether_addr_copy(emcdi_hdr->h_source, emcdi_src_mac_addr);
	emcdi_hdr->h_outer_vlan_proto = htons(ETH_P_8021Q);
	emcdi_hdr->h_outer_vlan_TCI = htons(U25_MPORT_TO_VLAN(nic_data->pf_mport));
	emcdi_hdr->h_inner_vlan_proto = htons(ETH_P_8021Q);
	emcdi_hdr->h_inner_vlan_TCI = htons(U25_MPORT_TO_VLAN(U25_MPORT_ID_CONTROL));
	emcdi_hdr->h_vlan_encapsulated_proto = htons(ETH_P_802_EX1);
	emcdi_hdr->type = efx_emcdi_get_header_type(emcdi->type);
	emcdi_hdr->reserved = 0;
	emcdi_hdr->seq_num = htons(cmd->seq);

#ifdef CONFIG_SFC_MCDI_LOGGING
	if (emcdi->logging_enabled) {
		size_t frag_len[] = { hdr_len, round_up(cmd->inlen, 4), sizeof(struct emcdi_hdr)};
		const efx_dword_t *frags[] = { hdr, cmd->inbuf , (efx_dword_t *)emcdi_hdr};
		char *buf = emcdi->logging_buffer; /* page-sized */
		const efx_dword_t *frag;
		unsigned int dcount = 0;
		int bytes = 0;
		int i, j;

		/* Header length should always be a whole number of dwords,
		 * so scream if it's not.
		 */
		WARN_ON_ONCE(hdr_len % 4);

		for (j = 0; j < ARRAY_SIZE(frags); j++) {
			frag = frags[j];
			for (i = 0;
					i < frag_len[j] / 4;
					i++) {
				/* Do not exceeed the internal printk limit.
				 * The string before that is just over 70 bytes.
				 */
				if ((bytes + 75) > LOG_LINE_MAX) {
					netif_info(efx, hw, efx->net_dev,
							"EMCDI RPC REQ:%s \\\n", buf);
					dcount = 0;
					bytes = 0;
				}
				bytes += snprintf(buf + bytes,
						LOG_LINE_MAX - bytes, " %08x",
						le32_to_cpu(frag[i].u32[0]));
				dcount++;
			}
		}

		netif_info(efx, hw, efx->net_dev, "EMCDI RPC REQ:%s\n", buf);
	}
#endif
	/* Debug: Transmit queue timed out
	 * Changed from netif_tx_lock to spin_unlock_bh and then to spin_lock
	 */
	spin_lock(&efx->emcdi->emcdi_tx_lock);
	efx_emcdi_send_func(efx, skb);
	spin_unlock(&efx->emcdi->emcdi_tx_lock);
	return 0;
}

static u16 efx_emcdi_get_seq(struct efx_emcdi_iface *emcdi)
{
	return emcdi->prev_seq + 1;
}

static int efx_emcdi_cmd_start_or_queue_ext(struct efx_emcdi_iface *emcdi,
		struct efx_emcdi_cmd *cmd,
		struct efx_emcdi_copy_buffer *copybuf)
{
	int rc;

	if (!emcdi->pending) {
		cmd->seq = efx_emcdi_get_seq(emcdi);
		rc = efx_emcdi_send_request(emcdi, cmd);
		if (rc)
			return rc;

		cmd->state = EMCDI_STATE_RUNNING;
		kref_get(&cmd->ref);
		queue_delayed_work(emcdi->work_queue, &cmd->work,
				EMCDI_RPC_TIMEOUT);
	} else if(cmd->state == EMCDI_STATE_RETRY) {
		rc = efx_emcdi_send_request(emcdi, cmd);
		if (rc)
			return rc;

		kref_get(&cmd->ref);
		queue_delayed_work(emcdi->work_queue, &cmd->work,
				EMCDI_RPC_TIMEOUT);

	} else {
		cmd->state = EMCDI_STATE_QUEUED;
	}

	return 0;
}

static void efx_emcdi_cmd_work(struct work_struct *context)
{
	struct efx_emcdi_copy_buffer *copybuf =
		kmalloc(sizeof(struct efx_emcdi_copy_buffer), GFP_KERNEL);
	struct efx_emcdi_cmd *cmd =
#if !defined(EFX_USE_KCOMPAT) || !defined(EFX_NEED_WORK_API_WRAPPERS)
		container_of(context, struct efx_emcdi_cmd, work.work);
#else
	container_of(context, struct efx_emcdi_cmd, work);
#endif
	struct efx_emcdi_iface *emcdi = cmd->emcdi;
	LIST_HEAD(cleanup_list);

	spin_lock_bh(&emcdi->iface_lock);

	if (cmd->state == EMCDI_STATE_FINISHED) {
		/* The command is done and this is a race between the
		 * completion in another thread and the work item running.
		 * All processing been done, so just release it.
		 */
		spin_unlock_bh(&emcdi->iface_lock);
		kref_put(&cmd->ref, efx_emcdi_cmd_release);
		kfree(copybuf);
		return;
	}

	if (efx_emcdi_check_timeout(cmd)) {
		efx_emcdi_timeout_cmd(emcdi, cmd, &cleanup_list);
	} else {
		kref_get(&cmd->ref);
		queue_delayed_work(emcdi->work_queue, &cmd->work,
				EMCDI_RPC_TIMEOUT);
	}

	spin_unlock_bh(&emcdi->iface_lock);

	kref_put(&cmd->ref, efx_emcdi_cmd_release);

	efx_emcdi_process_cleanup_list(emcdi, &cleanup_list);

	kfree(copybuf);
}

static int efx_emcdi_check_supported(struct efx_nic *efx,
		unsigned int cmd, size_t inlen)
{
	/*TODO:Need to add a check for emcdi support*/
	return 0;
}

static int efx_emcdi_rpc_sync_internal(struct efx_nic *efx,
		struct efx_emcdi_cmd *cmd, unsigned int *handle, uint8_t type)
{
	struct efx_emcdi_iface *emcdi = efx_emcdi(efx, type);
	struct efx_emcdi_copy_buffer *copybuf;
	int rc;

	rc = efx_emcdi_check_supported(efx, cmd->cmd, cmd->inlen);
	if (rc) {
		kref_put(&cmd->ref, efx_emcdi_cmd_release);
		return rc;
	}
	if (!emcdi) {
		kref_put(&cmd->ref, efx_emcdi_cmd_release);
		return -ENETDOWN;
	}

	copybuf = kmalloc(sizeof(struct efx_emcdi_copy_buffer), GFP_KERNEL);

	cmd->emcdi = emcdi;
	INIT_DELAYED_WORK(&cmd->work, efx_emcdi_cmd_work);
	INIT_LIST_HEAD(&cmd->list);
	INIT_LIST_HEAD(&cmd->cleanup_list);
	cmd->rc = 0;
	cmd->outbuf = NULL;
	cmd->outlen = 0;

	spin_lock_bh(&emcdi->iface_lock);

	cmd->handle = emcdi->prev_handle++;
	if (handle)
		*handle = cmd->handle;

	list_add_tail(&cmd->list, &emcdi->cmd_list);
	rc = efx_emcdi_cmd_start_or_queue_ext(emcdi, cmd, copybuf);
	if (rc) {
		list_del(&cmd->list);
		kref_put(&cmd->ref, efx_emcdi_cmd_release);
	}

	spin_unlock_bh(&emcdi->iface_lock);

	kfree(copybuf);

	return rc;
}

static void _efx_emcdi_cancel_cmd(struct efx_emcdi_iface *emcdi,
		unsigned int handle,
		struct list_head *cleanup_list)
{
	struct efx_nic *efx = emcdi->efx;
	struct efx_emcdi_cmd *cmd;

	list_for_each_entry(cmd, &emcdi->cmd_list, list)
		if (cmd->handle == handle) {
			switch (cmd->state) {
				case EMCDI_STATE_QUEUED:
				case EMCDI_STATE_RETRY:
					netif_dbg(efx, drv, efx->net_dev,
							"command %#x inlen %zu cancelled in queue\n",
							cmd->cmd, cmd->inlen);
					/* if not yet running, properly cancel it */
					cmd->rc = -EPIPE;
					efx_emcdi_remove_cmd(emcdi, cmd, cleanup_list);
					break;
				case EMCDI_STATE_RUNNING:
					netif_dbg(efx, drv, efx->net_dev,
							"command %#x inlen %zu cancelled after sending\n",
							cmd->cmd, cmd->inlen);
					cmd->rc = -EPIPE;
					_efx_emcdi_remove_cmd(emcdi, cmd, cleanup_list);
					cmd->state = EMCDI_STATE_RUNNING_CANCELLED;
					break;
				case EMCDI_STATE_RUNNING_CANCELLED:
					netif_warn(efx, drv, efx->net_dev,
							"command %#x inlen %zu double cancelled\n",
							cmd->cmd, cmd->inlen);
					break;
				case EMCDI_STATE_FINISHED:
				default:
					/* invalid state? */
					WARN_ON(1);
			}
			break;
		}
}

static void efx_emcdi_cancel_cmd(struct efx_emcdi_iface *emcdi, unsigned int handle)
{
	LIST_HEAD(cleanup_list);

	spin_lock_bh(&emcdi->iface_lock);
	emcdi->pending = NULL;
	_efx_emcdi_cancel_cmd(emcdi, handle, &cleanup_list);
	spin_unlock_bh(&emcdi->iface_lock);
	efx_emcdi_process_cleanup_list(emcdi, &cleanup_list);
}

static void efx_emcdi_rpc_completer(struct efx_nic *efx, unsigned long cookie,
		int rc, efx_dword_t *outbuf,
		size_t outlen_actual)
{
	struct efx_emcdi_blocking_data *wait_data =
		(struct efx_emcdi_blocking_data *)cookie;

	wait_data->rc = rc;
	memcpy(wait_data->outbuf, outbuf,
			min(outlen_actual, wait_data->outlen));
	wait_data->outlen_actual = outlen_actual;
	smp_wmb();
	wait_data->done = true;
	wake_up(&wait_data->wq);
}


static int efx_emcdi_rpc_sync(struct efx_nic *efx, unsigned int cmd,
		const efx_dword_t *inbuf, size_t inlen,
		efx_dword_t *outbuf, size_t outlen,
		size_t *outlen_actual, uint8_t type)
{
	struct efx_emcdi_iface *emcdi = efx_emcdi(efx, type);
	struct efx_emcdi_blocking_data *wait_data;
	struct efx_emcdi_cmd *cmd_item;
	unsigned int handle;
	int rc;

	if (outlen_actual)
		*outlen_actual = 0;

	if (!efx->emcdi->channel)
		return -ENETDOWN;
	if (!netif_running(efx->net_dev) || !emcdi->enabled)
		return -ENETDOWN;
	if (!efx->link_state.up)
		return -ENETDOWN;

	wait_data = kmalloc(sizeof(*wait_data), GFP_KERNEL);
	if (!wait_data)
		return -ENOMEM;

	cmd_item = kmalloc(sizeof(*cmd_item), GFP_KERNEL);
	if (!cmd_item) {
		kfree(wait_data);
		return -ENOMEM;
	}

	kref_init(&wait_data->ref);
	wait_data->done = false;
	init_waitqueue_head(&wait_data->wq);
	wait_data->outbuf = outbuf;
	wait_data->outlen = outlen;

	kref_init(&cmd_item->ref);
	cmd_item->cookie = (unsigned long) wait_data;
	cmd_item->completer = &efx_emcdi_rpc_completer;
	cmd_item->cmd = cmd;
	cmd_item->inlen = inlen;
	cmd_item->inbuf = inbuf;

	/* Claim an extra reference for the completer to put. */
	kref_get(&wait_data->ref);
	rc = efx_emcdi_rpc_sync_internal(efx, cmd_item, &handle, type);
	if (rc) {
		netif_err(efx, drv, efx->net_dev,
				"eMCDI command 0x%x inlen %zu failed (sync)\n",
				cmd, inlen);
		kref_put(&wait_data->ref, efx_emcdi_blocking_data_release);
		goto out;
	}

	if (!wait_event_timeout(wait_data->wq, wait_data->done,
				(EMCDI_MAX_RETRY * EMCDI_RPC_TIMEOUT)) &&
			!wait_data->done) {
		netif_err(efx, drv, efx->net_dev,
				"eMCDI command 0x%x inlen %zu timed out (sync)\n",
				cmd, inlen);

		efx_emcdi_cancel_cmd(emcdi, handle);

		wait_data->rc = -ETIMEDOUT;
		wait_data->outlen_actual = 0;
	}

	if (outlen_actual)
		*outlen_actual = wait_data->outlen_actual;
	rc = wait_data->rc;

out:
	kref_put(&wait_data->ref, efx_emcdi_blocking_data_release);
	return rc;
}
#if 0
int efx_emcdi_rpc_send_counter_ack(struct efx_nic *efx, const efx_dword_t *inbuf,
		size_t inlen, uint16_t seq_no)
{
	u8 emcdi_src_mac_addr[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	struct emcdi_hdr *emcdi_hdr;
	struct sk_buff *skb;

	if (!netif_running(efx->net_dev))
		return -ENETDOWN;

	skb = netdev_alloc_skb(efx->net_dev, MAX_EMCDI_PACKET_LEN);
	if (unlikely(skb == NULL)) {
		atomic_inc(&efx->n_rx_noskb_drops);
		return -ENOMEM;
	}
	skb_reserve(skb, sizeof(struct emcdi_hdr));
	skb_put(skb, inlen);

	memcpy(skb->data, inbuf, inlen);

	skb_push(skb, sizeof(struct emcdi_hdr));
	emcdi_hdr = (struct emcdi_hdr *) skb->data;
	eth_broadcast_addr(emcdi_hdr->h_dest);
	ether_addr_copy(emcdi_hdr->h_source, emcdi_src_mac_addr);
	emcdi_hdr->h_outer_vlan_proto = htons(ETH_P_8021Q);
	emcdi_hdr->h_outer_vlan_TCI = htons(U25_MPORT_TO_VLAN(nic_data->pf_mport));
	emcdi_hdr->h_inner_vlan_proto = htons(ETH_P_8021Q);
	emcdi_hdr->h_inner_vlan_TCI = htons(U25_MPORT_TO_VLAN(U25_MPORT_ID_COUNTER));
	emcdi_hdr->h_vlan_encapsulated_proto = htons(ETH_P_802_EX1);
	emcdi_hdr->type = EMCDI_HEADER_TYPE_COUNTER_ACK;
	emcdi_hdr->reserved = 0;
	emcdi_hdr->seq_num = htons(seq_no);

	efx_emcdi_send_func(efx, skb);
	return 0;
}
#endif
int efx_emcdi_rpc(struct efx_nic *efx, unsigned int cmd,
		const efx_dword_t *inbuf, size_t inlen,
		efx_dword_t *outbuf, size_t outlen,
		size_t *outlen_actual, uint8_t type)
{
	return efx_emcdi_rpc_sync(efx, cmd, inbuf, inlen, outbuf, outlen,
			outlen_actual, type);
}
