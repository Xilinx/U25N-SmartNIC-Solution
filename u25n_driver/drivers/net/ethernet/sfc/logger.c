/****************************************************************************
 * PS Logging Implementation for Xilinx U25N Accelerator Card.
 * Copyright 2021 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#include "mae.h"
#include "emcdi.h"
#include "logger.h"
#include "mcdi_pcol_mae.h"
#include "efx_devlink.h"

bool check_logger_is_running(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_LOGGER_APP_LAUNCH_OUT_LEN);
	size_t outlen;
        int rc;
        u32 status;
	struct logger_info *logs;
	logs = kzalloc(sizeof(struct logger_info), GFP_USER);
        if (!logs)
                return false;
        efx->emcdi->logger = logs;

	rc = efx_emcdi_rpc(efx, MC_CMD_LOGGER_APP_LAUNCH, NULL, 0, outbuf, sizeof(outbuf), &outlen, EMCDI_TYPE_CONTROLLER);
        if (rc) {
                netif_err(efx, drv, efx->net_dev, "Failed to start LOGGER app\n");
		goto fail;
        }
        if (outlen < sizeof(outbuf)) {
		pr_info("Func:%s outlen: %ld\n", __func__, outlen);
                goto fail;
	}
	
	status = MCDI_DWORD(outbuf, LOGGER_APP_LAUNCH_OUT_STATUS);
	if( !status ) {
		pr_info("%s:Func:status is zero\n", __func__);
		goto fail;
	}

	logs->pf_id = MCDI_DWORD(outbuf, LOGGER_APP_LAUNCH_OUT_PF_ID);

	return true;
fail:
	logs->pf_id = 0xffff;
	efx->emcdi->logger = NULL;
	kfree(logs);
	return false;
}

void logger_init(struct efx_nic *efx ,struct logger_info *logs)
{
	efx->emcdi->logger_wq = create_workqueue("logger_workq");
        if(!efx->emcdi->logger_wq) {
                pr_err("%s:Failed to create logger work queue\n", __func__);
                return;
        }
	
        spin_lock_init(&logs->logger_lock);
        mutex_init(&logs->logger_mutex);
        rwlock_init(&logs->logger_rwlock);
	
        INIT_WORK(&logs->logger_work, logs_write_to_file);
        logs->logger_q = kmalloc(sizeof(struct sk_buff_head), GFP_KERNEL);
        if (!logs->logger_q) {
                pr_err("%s: cannot allocate skb_buff_head\n", __func__);
                return;
        }
	skb_queue_head_init(logs->logger_q);

	logs->fp = filp_open("/var/log/ps_dmesg.txt", O_WRONLY | O_CREAT | O_LARGEFILE | O_APPEND, 0644);
        if (logs->fp == NULL) 
        	printk("%s:Logs file creation failed\n", __func__);

}

void logger_deinit(struct efx_nic *efx)
{
	 struct logger_info *logs;
	 logs = efx->emcdi->logger;
	 if(logs->logger_q) {
                skb_queue_purge(logs->logger_q);
                kfree(logs->logger_q);
        }
	filp_close(logs->fp, NULL);
	flush_work(&logs->logger_work);
        flush_workqueue(efx->emcdi->logger_wq);
        destroy_workqueue(efx->emcdi->logger_wq);
        efx->emcdi->logger = NULL;
	kfree(logs);
}

int efx_emcdi_start_request_log(struct efx_nic *efx)
{
	int rc;
	rc = efx_emcdi_rpc(efx, MC_CMD_START_REQUEST_LOGGER, NULL, 0, NULL, 0, NULL, EMCDI_TYPE_LOGGER);
        if (rc) {
                netif_err(efx, drv, efx->net_dev, "Failed to send request for logs\n");
        }
	
	return rc;
}

void logs_write_to_file(struct work_struct *work)
{
        struct logger_info *logger = container_of(work, struct logger_info, logger_work);
        struct sk_buff *skb;
        void *data;
        mm_segment_t fs;

	spin_lock_bh(&logger->logger_lock);

        if(skb_queue_len(logger->logger_q) != 0 ) {
		skb = skb_dequeue(logger->logger_q);
		spin_unlock_bh(&logger->logger_lock);
                if (!skb)
                        return;
                data = skb->data;
                fs = get_fs();
                set_fs(KERNEL_DS);
                write_lock(&logger->logger_rwlock);
                if (kernel_write(logger->fp, data, strlen((char *)data), &logger->fp->f_pos) < 0) {
                        write_unlock(&logger->logger_rwlock);
                        return;
                } else {
                        set_fs(fs);
                }
		write_unlock(&logger->logger_rwlock);
		kfree_skb(skb);
        } else
		spin_unlock_bh(&logger->logger_lock);
}

int efx_emcdi_process_logs_message(struct efx_nic *efx, uint8_t *data, uint8_t length)
{
	struct sk_buff *skb;
	struct logger_info *logs;

	skb = alloc_skb(PKT_BUFF_SZ, GFP_KERNEL);
        if (skb == NULL) {
		pr_err("%s: cannot allocate skb for logs\n", __func__);
                return -ENOMEM;
        }
	
	logs = efx->emcdi->logger;
	memcpy(skb->data , data, length);
	spin_lock(&logs->logger_lock);
        skb_queue_tail(logs->logger_q, skb);
	spin_unlock(&logs->logger_lock);
        queue_work(efx->emcdi->logger_wq, &logs->logger_work);

	return 0;
}

int efx_emcdi_stop_logs(struct efx_nic *efx)
{
	int rc;
    	rc = efx_emcdi_rpc(efx, MC_CMD_STOP_LOGGER, NULL, 0, NULL, 0, NULL, EMCDI_TYPE_LOGGER);
	if (rc) {
                netif_err(efx, drv, efx->net_dev, "Failed to stop logs\n");
        }

	return rc;
}

