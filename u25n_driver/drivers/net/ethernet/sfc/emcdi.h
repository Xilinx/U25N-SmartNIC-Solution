/****************************************************************************
 * Encapsulated MCDI Interface for Xilinx U25N Accelerator Card.
 * Copyright 2021 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_EMCDI_H
#define EFX_EMCDI_H

#include <linux/mutex.h>
#include <linux/kref.h>

/*typedefs and macros*/
#define EMCDI_RPC_TIMEOUT                       (1 * HZ)
#define EMCDI_ACQUIRE_TIMEOUT                   (EMCDI_RPC_TIMEOUT  * 3)
#define EMCDI_MAX_RETRY                         3
#define MAX_EMCDI_SEQUENCE_NUMBER               0xffff /*as it is a running counter it will rotate*/
#define MAX_EMCDI_PACKET_LEN                    0x44c /*Assuming MCDI v2 maximum length is 0x400*/

#define EMCDI_HEADER_TYPE_CONTROL	        0
#define EMCDI_HEADER_TYPE_COUNTER               1
#define EMCDI_HEADER_TYPE_COUNTER_ACK           2
#define EMCDI_HEADER_TYPE_IPSEC                 3
#define EMCDI_HEADER_TYPE_FIREWALL		4
#define EMCDI_HEADER_TYPE_IPSEC_COUNTER         10
#define EMCDI_HEADER_TYPE_IPSEC_COUNTER_ACK     7

#define EMCDI_HEADER_TYPE_FLASH_UPGRADE         5
#define EMCDI_HEADER_TYPE_CONTROLLER            6
#define EMCDI_HEADER_TYPE_IMG                   8
#define EMCDI_HEADER_TYPE_LOGGER                9

/**
 * enum efx_mcdi_cmd_state - State for an individual eMCDI command
 * @MCDI_STATE_QUEUED: Command not started
 * @MCDI_STATE_RETRY: Command was submitted and no response received.
 *                    Command will be retried once another command returns 
 *                    or when timeout happens.
 * @MCDI_STATE_RUNNING: Command was accepted and is running.
 */
enum efx_emcdi_cmd_state {
	/* waiting to run */
	EMCDI_STATE_QUEUED,
	/* we tried to run, but the command  is not reached PS
	*/
	EMCDI_STATE_RETRY,
	/* the command is running */
	EMCDI_STATE_RUNNING,
	EMCDI_STATE_RUNNING_CANCELLED,
	/* processing of this command has completed.
	 * used to break races between contexts.
	 */
	EMCDI_STATE_FINISHED,
};

/*Function Prototype*/
int efx_emcdi_init_channel(struct efx_nic *efx);
void efx_emcdi_fini_channel(struct efx_nic *efx);
int efx_emcdi_init(struct efx_nic *efx, uint8_t type);
void efx_emcdi_fini(struct efx_nic *efx, uint8_t type);
int efx_emcdi_rpc(struct efx_nic *efx, unsigned int cmd,
		const efx_dword_t *inbuf, size_t inlen,
		efx_dword_t *outbuf, size_t outlen,
		size_t *outlen_actual, uint8_t type);
int efx_emcdi_rpc_send_counter_ack(struct efx_nic *efx, const efx_dword_t *inbuf,
		size_t inlen, uint16_t seq_no);
typedef void efx_emcdi_sync_completer(struct efx_nic *efx,
		unsigned long cookie, int rc,
		efx_dword_t *outbuf,
		size_t outlen_actual);

/*structure definitions*/
/**
 * struct efx_emcdi_cmd - An outstanding eMCDI command
 * @ref: the kernel reference variable
 * @cleanup_list: The data for this entry in a cleanup list 
 * @list: The data for this entry in emcdi->cmd_list
 * @work: The work item for this command, queued in emcdi->workqueue
 * @emcdi: The emcdi_iface for this command
 * @state: The state of this command
 * @inlen: inbuf length
 * @inbuf: Input buffer
 * @seq: Sequence number
 * @started: jiffies when the eMCDI started
 * @cookie: Context for completion function
 * @completer: Completion function
 * @handle: command handle
 * @cmd: Command number
 * @rc: return value
 * @retry: retry count
 * @outlen: outbuf length
 * @outbuf: output buffer
 */
struct efx_emcdi_cmd {
	struct kref ref;
	struct list_head list;
	struct list_head cleanup_list;
	struct delayed_work work;
	struct efx_emcdi_iface *emcdi;
	enum efx_emcdi_cmd_state state;
	size_t inlen;
	const efx_dword_t *inbuf;
	u16 seq;
	unsigned long started;
	unsigned long cookie;
	efx_emcdi_sync_completer *completer;
	unsigned int handle;
	unsigned int cmd;
	int rc;
	u8  retry;
	size_t outlen;
	efx_dword_t *outbuf;
};

#define MAX_EMCDI_TYPES			10
enum efx_emcdi_types {
	EMCDI_TYPE_MAE,
	EMCDI_TYPE_IPSEC,
	EMCDI_TYPE_FIREWALL,
	EMCDI_TYPE_FLASH_UPGRADE = 5,
	EMCDI_TYPE_CONTROLLER, /* Spawns App */
	EMCDI_TYPE_IMG,
	EMCDI_TYPE_LOGGER = 9,
};

/**
 * struct efx_emcdi_iface - Encapsulated MCDI protocol context
 * @efx: The associated NIC
 * @type: eMCDI type
 * @enabled: interface initialized or not
 * @iface_lock: Serialise access to this structure
 * @outstanding_cleanups: pending commands for cleanup count
 * @cmd_list: List of outstanding and running commands
 * @workqueue: Workqueue used for delayed processing
 * @cmd_complete_wq: Waitqueue for command completion
 * @prev_seq: The last used sequence number
 * @prev_handle: last used command handle
 * @pending: The running command
 * @logging_enabled: Whether to trace eMCDI
 * @logging_buffer: Buffer that may be used to build eMCDI tracing messages
 */
struct efx_emcdi_iface {
	struct efx_nic *efx;
	uint8_t type;
	bool enabled;
	spinlock_t iface_lock;
	unsigned int outstanding_cleanups;
	struct list_head cmd_list;
	struct workqueue_struct *work_queue;
	struct work_struct work;
	wait_queue_head_t cmd_complete_wq;
	u16 prev_seq;
	unsigned int prev_handle;
	struct efx_emcdi_cmd *pending;
#ifdef CONFIG_SFC_MCDI_LOGGING
	bool logging_enabled;
	char *logging_buffer;
#endif
};

/**
 * struct efx_emcdi_data - extra state for NICs that implement EMCDI
 * @iface: Interface/protocol state
 * @channel: eMCDI channel
 */
struct efx_emcdi_data {
	struct efx_emcdi_iface iface[MAX_EMCDI_TYPES];
	struct efx_channel *channel;
	spinlock_t emcdi_tx_lock;
	struct workqueue_struct *logger_wq;
	struct logger_info *logger;
};

struct emcdi_hdr {
	unsigned char   h_dest[ETH_ALEN];
	unsigned char   h_source[ETH_ALEN];
	__be16          h_outer_vlan_proto;
	__be16          h_outer_vlan_TCI;
	__be16          h_inner_vlan_proto;
	__be16          h_inner_vlan_TCI;
	__be16          h_vlan_encapsulated_proto;
	unsigned char   type;
	unsigned char   reserved;
	__be16          seq_num;
} __attribute__((packed));

static inline struct efx_emcdi_iface *efx_emcdi(struct efx_nic *efx, uint8_t type)
{
	return efx->emcdi ? &efx->emcdi->iface[type] : NULL;
}

#endif /* EFX_EMCDI_H */
