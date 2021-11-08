/****************************************************************************
 * PS Logging Implementation for Xilinx U25N Accelerator Card.
 * Copyright 2021 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef LOGGER_H
#define LOGGER_H

#define PKT_BUFF_SZ   1024

struct logger_info {
        struct file *fp;
        struct work_struct logger_work;
        struct sk_buff_head *logger_q;
        spinlock_t logger_lock;
        struct mutex logger_mutex;
        rwlock_t logger_rwlock;
        uint32_t pf_id;
};

bool check_logger_is_running(struct efx_nic *efx );
void logger_init(struct efx_nic *efx ,struct logger_info *logs);
void logger_deinit(struct efx_nic *efx);
int efx_emcdi_start_request_log(struct efx_nic *efx);
int efx_emcdi_process_logs_message(struct efx_nic *efx, uint8_t *data, uint8_t length);
int efx_emcdi_stop_logs(struct efx_nic *efx);
void logs_write_to_file(struct work_struct *work);

#endif /* LOGGER_H */

