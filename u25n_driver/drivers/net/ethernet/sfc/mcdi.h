/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2008-2017 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_MCDI_H
#define EFX_MCDI_H

#include <linux/mutex.h>
#include <linux/kref.h>

/**
 * enum efx_mcdi_mode - MCDI transaction mode
 * @MCDI_MODE_POLL: poll for MCDI completion, until timeout
 * @MCDI_MODE_EVENTS: wait for an mcdi_event.  On timeout, poll once
 * @MCDI_MODE_FAIL: we think MCDI is dead, so fail-fast all calls
 */
enum efx_mcdi_mode {
	MCDI_MODE_POLL,
	MCDI_MODE_EVENTS,
	MCDI_MODE_FAIL,
};

/* On older firmwares there is only a single thread on the MC, so even
 * the shortest operation can be blocked for some time by an operation
 * requested by a different function.
 * See bug61269 for further discussion.
 *
 * On newer firmwares that support multithreaded MCDI commands we extend
 * the timeout for commands we know can run longer.
 */
#define MCDI_RPC_TIMEOUT       (10 * HZ)
#define MCDI_RPC_LONG_TIMEOUT  (60 * HZ)
#define MCDI_RPC_POST_RST_TIME (10 * HZ)

/**
 * enum efx_mcdi_cmd_state - State for an individual MCDI command
 * @MCDI_STATE_QUEUED: Command not started
 * @MCDI_STATE_RETRY: Command was submitted and MC rejected with no resources.
 *                    Command will be retried once another command returns.
 * @MCDI_STATE_PROXY: Command needs authenticating with proxy auth. Will be sent
 *                    again after a PROXY_COMPLETE event.
 * @MCDI_STATE_RUNNING: Command was accepted and is running.
 * @MCDI_STATE_ABORT: Command has been completed or aborted. Used to resolve
 *		      race between completion in another threads and the worker.
 */
enum efx_mcdi_cmd_state {
	/* waiting to run */
	MCDI_STATE_QUEUED,
	/* we tried to run, but the MC said we have too many outstanding
	 * commands
	 */
	MCDI_STATE_RETRY,
	/* we sent the command and the MC is waiting for proxy auth */
	MCDI_STATE_PROXY,
	/* the command is running */
	MCDI_STATE_RUNNING,
	/* state was PROXY but the issuer cancelled the command */
	MCDI_STATE_PROXY_CANCELLED,
	/* the command is running but the issuer cancelled the command */
	MCDI_STATE_RUNNING_CANCELLED,
	/* processing of this command has completed.
	 * used to break races between contexts.
	 */
	MCDI_STATE_FINISHED,
};

typedef void efx_mcdi_async_completer(struct efx_nic *efx,
				      unsigned long cookie, int rc,
				      efx_dword_t *outbuf,
				      size_t outlen_actual);

/**
 * struct efx_mcdi_cmd - An outstanding MCDI command
 * @ref: Reference count. There will be one reference if the command is
 *	in the mcdi_iface cmd_list, another if it's on a cleanup list,
 *	and a third if it's queued in the work queue.
 * @list: The data for this entry in mcdi->cmd_list
 * @cleanup_list: The data for this entry in a cleanup list
 * @work: The work item for this command, queued in mcdi->workqueue
 * @mcdi: The mcdi_iface for this command
 * @state: The state of this command
 * @inlen: inbuf length
 * @inbuf: Input buffer
 * @quiet: Whether to silence errors
 * @polled: Whether this command is polled or evented
 * @reboot_seen: Whether a reboot has been seen during this command,
 *	to prevent duplicates
 * @seq: Sequence number
 * @bufid: Buffer ID from the NIC implementation
 * @started: Jiffies this command was started at
 * @cookie: Context for completion function
 * @completer: Completion function
 * @cmd: Command number
 * @proxy_handle: Handle if this command was proxied
 */
struct efx_mcdi_cmd {
	struct kref ref;
	struct list_head list;
	struct list_head cleanup_list;
	struct delayed_work work;
	struct efx_mcdi_iface *mcdi;
	enum efx_mcdi_cmd_state state;
	size_t inlen;
	const efx_dword_t *inbuf;
	bool quiet;
	bool polled;
	bool reboot_seen;
	u8 seq;
	u8 bufid;
	unsigned long started;
	unsigned long cookie;
	efx_mcdi_async_completer *atomic_completer;
	efx_mcdi_async_completer *completer;
	unsigned int handle;
	unsigned int cmd;
	int rc;
	size_t outlen;
	efx_dword_t *outbuf;
	u32 proxy_handle;
	/* followed by inbuf data if necessary */
};

/**
 * struct efx_mcdi_iface - MCDI protocol context
 * @efx: The associated NIC
 * @iface_lock: Serialise access to this structure
 * @cmd_list: List of outstanding and running commands
 * @workqueue: Workqueue used for delayed processing
 * @outstanding_cleanups: Count of cleanups
 * @cmd_complete_wq: Waitqueue for command completion
 * @db_held_by: Command the MC doorbell is in use by
 * @seq_held_by: Command each sequence number is in use by
 * @prev_seq: The last used sequence number
 * @prev_handle: The last used command handle
 * @mode: Poll for mcdi completion, or wait for an mcdi_event
 * @new_epoch: Indicates start of day or start of MC reboot recovery
 * @logging_buffer: Buffer that may be used to build MCDI tracing messages
 * @logging_enabled: Whether to trace MCDI
 */
struct efx_mcdi_iface {
	struct efx_nic *efx;
	spinlock_t iface_lock;
	unsigned int outstanding_cleanups;
	struct list_head cmd_list;
	struct workqueue_struct *workqueue;
	wait_queue_head_t cmd_complete_wq;
	struct efx_mcdi_cmd *db_held_by;
	struct efx_mcdi_cmd *seq_held_by[16];
	unsigned int prev_handle;
	enum efx_mcdi_mode mode;
	u8 prev_seq;
	bool new_epoch;
#ifdef CONFIG_SFC_MCDI_LOGGING
	bool logging_enabled;
	char *logging_buffer;
#endif
};

struct efx_mcdi_mon {
	struct efx_buffer dma_buf;
	struct mutex update_lock;
	unsigned long last_update;
#if defined(EFX_USE_KCOMPAT) && defined(EFX_HAVE_HWMON_CLASS_DEVICE)
	struct class_device *device;
#else
	struct device *device;
#endif
	struct efx_mcdi_mon_attribute *attrs;
	unsigned int n_attrs;
};

/**
 * struct efx_mcdi_data - extra state for NICs that implement MCDI
 * @iface: Interface/protocol state
 * @hwmon: Hardware monitor state
 * @fn_flags: Flags for this function, as returned by %MC_CMD_DRV_ATTACH.
 */
struct efx_mcdi_data {
	struct efx_mcdi_iface iface;
#ifdef CONFIG_SFC_MCDI_MON
	struct efx_mcdi_mon hwmon;
#endif
	u32 fn_flags;
};

static inline struct efx_mcdi_iface *efx_mcdi(struct efx_nic *efx)
{
	return efx->mcdi ? &efx->mcdi->iface : NULL;
}

#ifdef CONFIG_SFC_MCDI_MON
static inline struct efx_mcdi_mon *efx_mcdi_mon(struct efx_nic *efx)
{
	return efx->mcdi ? &efx->mcdi->hwmon : NULL;
}
#endif

int efx_mcdi_init(struct efx_nic *efx);
void efx_mcdi_detach(struct efx_nic *efx);
void efx_mcdi_fini(struct efx_nic *efx);

int efx_mcdi_rpc(struct efx_nic *efx, unsigned int cmd,
		 const efx_dword_t *inbuf, size_t inlen,
		 efx_dword_t *outbuf, size_t outlen, size_t *outlen_actual);
int efx_mcdi_rpc_quiet(struct efx_nic *efx, unsigned int cmd,
		       const efx_dword_t *inbuf, size_t inlen,
		       efx_dword_t *outbuf, size_t outlen,
		       size_t *outlen_actual);

int efx_mcdi_rpc_async(struct efx_nic *efx, unsigned int cmd,
		       const efx_dword_t *inbuf, size_t inlen,
		       efx_mcdi_async_completer *complete,
		       unsigned long cookie);
int efx_mcdi_rpc_async_quiet(struct efx_nic *efx, unsigned int cmd,
			     const efx_dword_t *inbuf, size_t inlen,
			     efx_mcdi_async_completer *complete,
			     unsigned long cookie);
int efx_mcdi_rpc_async_ext(struct efx_nic *efx, unsigned int cmd,
			   const efx_dword_t *inbuf, size_t inlen,
			   efx_mcdi_async_completer *atomic_completer,
			   efx_mcdi_async_completer *completer,
			   unsigned long cookie, bool quiet,
			   bool immediate_only, unsigned int *handle);

/* Attempt to cancel an outstanding command.
 * This function guarantees that the completion function will never be called
 * after it returns. The command may or may not actually be cancelled.
 */
void efx_mcdi_cancel_cmd(struct efx_nic *efx, unsigned int handle);

void efx_mcdi_display_error(struct efx_nic *efx, unsigned int cmd,
			    size_t inlen, efx_dword_t *outbuf,
			    size_t outlen, int rc);
int efx_mcdi_errno(struct efx_nic *efx, unsigned int mcdi_err);

int efx_mcdi_poll_reboot(struct efx_nic *efx);
void efx_mcdi_mode_poll(struct efx_nic *efx);
void efx_mcdi_mode_event(struct efx_nic *efx);
/* Wait for all commands and all cleanup for them to be complete */
void efx_mcdi_wait_for_cleanup(struct efx_nic *efx);
/* Wait for all commands to be complete */
int efx_mcdi_wait_for_quiescence(struct efx_nic *efx,
				 unsigned int timeout_jiffies);
/* Indicate to the MCDI module that MC reset processing is complete
 * so new commands can now be sent.
 */
void efx_mcdi_post_reset(struct efx_nic *efx);

bool efx_mcdi_process_event(struct efx_channel *channel, efx_qword_t *event);
#ifdef CONFIG_SFC_MCDI_MON
void efx_mcdi_sensor_event(struct efx_nic *efx, efx_qword_t *ev);
#else
static inline void efx_mcdi_sensor_event(struct efx_nic *efx, efx_qword_t *ev)
{
}
#endif

/* We expect that 16- and 32-bit fields in MCDI requests and responses
 * are appropriately aligned, but 64-bit fields are only
 * 32-bit-aligned.  Also, on Siena we must copy to the MC shared
 * memory strictly 32 bits at a time, so add any necessary padding.
 */
#define _MCDI_DECLARE_BUF(_name, _len)					\
	efx_dword_t _name[DIV_ROUND_UP(_len, 4)]
#define MCDI_DECLARE_BUF(_name, _len)					\
	_MCDI_DECLARE_BUF(_name, _len) = {{{0}}}
#define MCDI_DECLARE_BUF_ERR(_name)					\
	MCDI_DECLARE_BUF(_name, 8)
#define _MCDI_PTR(_buf, _offset)					\
	((u8 *)(_buf) + (_offset))
#define MCDI_PTR(_buf, _field)						\
	_MCDI_PTR(_buf, MC_CMD_ ## _field ## _OFST)
/* Use MCDI_STRUCT_ functions to access members of MCDI structuredefs.
 * _buf should point to the start of the structure, typically obtained with
 * MCDI_DECLARE_STRUCT_PTR(structure) = _MCDI_DWORD(mcdi_buf, FIELD_WHICH_IS_STRUCT);
 */
#define MCDI_STRUCT_PTR(_buf, _field)					\
	_MCDI_PTR(_buf, _field ## _OFST)
#define _MCDI_CHECK_ALIGN(_ofst, _align)				\
	((void)BUILD_BUG_ON_ZERO((_ofst) & (_align - 1)),		\
	 (_ofst))
#define _MCDI_DWORD(_buf, _field)					\
	((_buf) + (_MCDI_CHECK_ALIGN(MC_CMD_ ## _field ## _OFST, 4) >> 2))
#define _MCDI_STRUCT_DWORD(_buf, _field)				\
	((_buf) + (_MCDI_CHECK_ALIGN(_field ## _OFST, 4) >> 2))

#define MCDI_SET_BYTE(_buf, _field, _value) do {			\
	BUILD_BUG_ON(MC_CMD_ ## _field ## _LEN != 1);			\
	*(u8 *)MCDI_PTR(_buf, _field) = _value;				\
	} while (0)
#define MCDI_STRUCT_SET_BYTE(_buf, _field, _value) do {			\
	BUILD_BUG_ON(_field ## _LEN != 1);				\
	*(u8 *)MCDI_STRUCT_PTR(_buf, _field) = _value;			\
	} while (0)
#define MCDI_BYTE(_buf, _field)						\
	((void)BUILD_BUG_ON_ZERO(MC_CMD_ ## _field ## _LEN != 1),	\
	 *MCDI_PTR(_buf, _field))
#define MCDI_SET_WORD(_buf, _field, _value) do {			\
	BUILD_BUG_ON(MC_CMD_ ## _field ## _LEN != 2);			\
	BUILD_BUG_ON(MC_CMD_ ## _field ## _OFST & 1);			\
	*(__force __le16 *)MCDI_PTR(_buf, _field) = cpu_to_le16(_value);\
	} while (0)
#define MCDI_STRUCT_SET_WORD(_buf, _field, _value) do {			\
	BUILD_BUG_ON(_field ## _LEN != 2);				\
	BUILD_BUG_ON(_field ## _OFST & 1);				\
	*(__force __le16 *)MCDI_STRUCT_PTR(_buf, _field) = cpu_to_le16(_value);\
	} while (0)
#define MCDI_WORD(_buf, _field)						\
	((void)BUILD_BUG_ON_ZERO(MC_CMD_ ## _field ## _LEN != 2),	\
	 le16_to_cpu(*(__force const __le16 *)MCDI_PTR(_buf, _field)))
/* Read a 16-bit field defined in the protocol as being big-endian. */
#define MCDI_WORD_BE(_buf, _field)					\
	((void)BUILD_BUG_ON_ZERO(MC_CMD_ ## _field ## _LEN != 2),	\
	 *(__force const __be16 *)MCDI_PTR(_buf, _field))
/* Write a 16-bit field defined in the protocol as being big-endian. */
#define MCDI_SET_WORD_BE(_buf, _field, _value) do {			\
	BUILD_BUG_ON(MC_CMD_ ## _field ## _LEN != 2);			\
	BUILD_BUG_ON(MC_CMD_ ## _field ## _OFST & 1);			\
	*(__force __be16 *)MCDI_PTR(_buf, _field) = (_value);		\
	} while (0)
#define MCDI_STRUCT_SET_WORD_BE(_buf, _field, _value) do {		\
	BUILD_BUG_ON(_field ## _LEN != 2);				\
	BUILD_BUG_ON(_field ## _OFST & 1);				\
	*(__force __be16 *)MCDI_STRUCT_PTR(_buf, _field) = (_value);	\
	} while (0)
#define MCDI_SET_DWORD(_buf, _field, _value)				\
	EFX_POPULATE_DWORD_1(*_MCDI_DWORD(_buf, _field), EFX_DWORD_0, _value)
#define MCDI_STRUCT_SET_DWORD(_buf, _field, _value)			\
	EFX_POPULATE_DWORD_1(*_MCDI_STRUCT_DWORD(_buf, _field), EFX_DWORD_0, _value)
#define MCDI_DWORD(_buf, _field)					\
	EFX_DWORD_FIELD(*_MCDI_DWORD(_buf, _field), EFX_DWORD_0)
/* Read a 32-bit field defined in the protocol as being big-endian. */
#define MCDI_DWORD_BE(_buf, _field)					\
	((void)BUILD_BUG_ON_ZERO(MC_CMD_ ## _field ## _LEN != 4),	\
	 *(__force const __be32 *)MCDI_PTR(_buf, _field))
/* Write a 32-bit field defined in the protocol as being big-endian. */
#define MCDI_SET_DWORD_BE(_buf, _field, _value) do {			\
	BUILD_BUG_ON(MC_CMD_ ## _field ## _LEN < 4);			\
	BUILD_BUG_ON(MC_CMD_ ## _field ## _OFST & 3);			\
	*(__force __be32 *)MCDI_PTR(_buf, _field) = (_value);		\
	} while (0)
#define MCDI_STRUCT_SET_DWORD_BE(_buf, _field, _value) do {		\
	BUILD_BUG_ON(_field ## _LEN != 4);				\
	BUILD_BUG_ON(_field ## _OFST & 3);				\
	*(__force __be32 *)MCDI_STRUCT_PTR(_buf, _field) = (_value);	\
	} while (0)
#define MCDI_POPULATE_DWORD_1(_buf, _field, _name1, _value1)		\
	EFX_POPULATE_DWORD_1(*_MCDI_DWORD(_buf, _field),		\
			     MC_CMD_ ## _name1, _value1)
#define MCDI_POPULATE_DWORD_2(_buf, _field, _name1, _value1,		\
			      _name2, _value2)				\
	EFX_POPULATE_DWORD_2(*_MCDI_DWORD(_buf, _field),		\
			     MC_CMD_ ## _name1, _value1,		\
			     MC_CMD_ ## _name2, _value2)
#define MCDI_POPULATE_DWORD_3(_buf, _field, _name1, _value1,		\
			      _name2, _value2, _name3, _value3)		\
	EFX_POPULATE_DWORD_3(*_MCDI_DWORD(_buf, _field),		\
			     MC_CMD_ ## _name1, _value1,		\
			     MC_CMD_ ## _name2, _value2,		\
			     MC_CMD_ ## _name3, _value3)
#define MCDI_POPULATE_DWORD_4(_buf, _field, _name1, _value1,		\
			      _name2, _value2, _name3, _value3,		\
			      _name4, _value4)				\
	EFX_POPULATE_DWORD_4(*_MCDI_DWORD(_buf, _field),		\
			     MC_CMD_ ## _name1, _value1,		\
			     MC_CMD_ ## _name2, _value2,		\
			     MC_CMD_ ## _name3, _value3,		\
			     MC_CMD_ ## _name4, _value4)
#define MCDI_POPULATE_DWORD_5(_buf, _field, _name1, _value1,		\
			      _name2, _value2, _name3, _value3,		\
			      _name4, _value4, _name5, _value5)		\
	EFX_POPULATE_DWORD_5(*_MCDI_DWORD(_buf, _field),		\
			     MC_CMD_ ## _name1, _value1,		\
			     MC_CMD_ ## _name2, _value2,		\
			     MC_CMD_ ## _name3, _value3,		\
			     MC_CMD_ ## _name4, _value4,		\
			     MC_CMD_ ## _name5, _value5)
#define MCDI_POPULATE_DWORD_6(_buf, _field, _name1, _value1,		\
			      _name2, _value2, _name3, _value3,		\
			      _name4, _value4, _name5, _value5,		\
			      _name6, _value6)				\
	EFX_POPULATE_DWORD_6(*_MCDI_DWORD(_buf, _field),		\
			     MC_CMD_ ## _name1, _value1,		\
			     MC_CMD_ ## _name2, _value2,		\
			     MC_CMD_ ## _name3, _value3,		\
			     MC_CMD_ ## _name4, _value4,		\
			     MC_CMD_ ## _name5, _value5,		\
			     MC_CMD_ ## _name6, _value6)
#define MCDI_POPULATE_DWORD_7(_buf, _field, _name1, _value1,		\
			      _name2, _value2, _name3, _value3,		\
			      _name4, _value4, _name5, _value5,		\
			      _name6, _value6, _name7, _value7)		\
	EFX_POPULATE_DWORD_7(*_MCDI_DWORD(_buf, _field),		\
			     MC_CMD_ ## _name1, _value1,		\
			     MC_CMD_ ## _name2, _value2,		\
			     MC_CMD_ ## _name3, _value3,		\
			     MC_CMD_ ## _name4, _value4,		\
			     MC_CMD_ ## _name5, _value5,		\
			     MC_CMD_ ## _name6, _value6,		\
			     MC_CMD_ ## _name7, _value7)
#define MCDI_POPULATE_DWORD_8(_buf, _field, _name1, _value1,		\
			      _name2, _value2, _name3, _value3,		\
			      _name4, _value4, _name5, _value5,		\
			      _name6, _value6, _name7, _value7,		\
			      _name8, _value8)		\
	EFX_POPULATE_DWORD_8(*_MCDI_DWORD(_buf, _field),		\
			     MC_CMD_ ## _name1, _value1,		\
			     MC_CMD_ ## _name2, _value2,		\
			     MC_CMD_ ## _name3, _value3,		\
			     MC_CMD_ ## _name4, _value4,		\
			     MC_CMD_ ## _name5, _value5,		\
			     MC_CMD_ ## _name6, _value6,		\
			     MC_CMD_ ## _name7, _value7,		\
			     MC_CMD_ ## _name8, _value8)
#define MCDI_POPULATE_DWORD_9(_buf, _field, _name1, _value1,		\
			      _name2, _value2, _name3, _value3,		\
			      _name4, _value4, _name5, _value5,		\
			      _name6, _value6, _name7, _value7,		\
			      _name8, _value8, _name9, _value9)		\
	EFX_POPULATE_DWORD_9(*_MCDI_DWORD(_buf, _field),		\
			     MC_CMD_ ## _name1, _value1,		\
			     MC_CMD_ ## _name2, _value2,		\
			     MC_CMD_ ## _name3, _value3,		\
			     MC_CMD_ ## _name4, _value4,		\
			     MC_CMD_ ## _name5, _value5,		\
			     MC_CMD_ ## _name6, _value6,		\
			     MC_CMD_ ## _name7, _value7,		\
			     MC_CMD_ ## _name8, _value8,		\
			     MC_CMD_ ## _name9, _value9)
#define MCDI_POPULATE_DWORD_10(_buf, _field, _name1, _value1,		\
			       _name2, _value2, _name3, _value3,	\
			       _name4, _value4, _name5, _value5,	\
			       _name6, _value6, _name7, _value7,	\
			       _name8, _value8, _name9, _value9,	\
			       _name10, _value10)			\
	EFX_POPULATE_DWORD_10(*_MCDI_DWORD(_buf, _field),		\
			      MC_CMD_ ## _name1, _value1,		\
			      MC_CMD_ ## _name2, _value2,		\
			      MC_CMD_ ## _name3, _value3,		\
			      MC_CMD_ ## _name4, _value4,		\
			      MC_CMD_ ## _name5, _value5,		\
			      MC_CMD_ ## _name6, _value6,		\
			      MC_CMD_ ## _name7, _value7,		\
			      MC_CMD_ ## _name8, _value8,		\
			      MC_CMD_ ## _name9, _value9,		\
			      MC_CMD_ ## _name10, _value10)
#define MCDI_POPULATE_DWORD_11(_buf, _field, _name1, _value1,		\
			       _name2, _value2, _name3, _value3,	\
			       _name4, _value4, _name5, _value5,	\
			       _name6, _value6, _name7, _value7,	\
			       _name8, _value8, _name9, _value9,	\
			       _name10, _value10, _name11, _value11)	\
	EFX_POPULATE_DWORD_11(*_MCDI_DWORD(_buf, _field),		\
			      MC_CMD_ ## _name1, _value1,		\
			      MC_CMD_ ## _name2, _value2,		\
			      MC_CMD_ ## _name3, _value3,		\
			      MC_CMD_ ## _name4, _value4,		\
			      MC_CMD_ ## _name5, _value5,		\
			      MC_CMD_ ## _name6, _value6,		\
			      MC_CMD_ ## _name7, _value7,		\
			      MC_CMD_ ## _name8, _value8,		\
			      MC_CMD_ ## _name9, _value9,		\
			      MC_CMD_ ## _name10, _value10,		\
			      MC_CMD_ ## _name11, _value11)
#define MCDI_SET_QWORD(_buf, _field, _value)				\
	do {								\
		EFX_POPULATE_DWORD_1(_MCDI_DWORD(_buf, _field)[0],	\
				     EFX_DWORD_0, (u32)(_value));	\
		EFX_POPULATE_DWORD_1(_MCDI_DWORD(_buf, _field)[1],	\
				     EFX_DWORD_0, (u64)(_value) >> 32);	\
	} while (0)
#define MCDI_QWORD(_buf, _field)					\
	(EFX_DWORD_FIELD(_MCDI_DWORD(_buf, _field)[0], EFX_DWORD_0) |	\
	(u64)EFX_DWORD_FIELD(_MCDI_DWORD(_buf, _field)[1], EFX_DWORD_0) << 32)
#define MCDI_FIELD(_ptr, _type, _field)					\
	EFX_EXTRACT_DWORD(						\
		*(efx_dword_t *)					\
		_MCDI_PTR(_ptr, MC_CMD_ ## _type ## _ ## _field ## _OFST & ~3),\
		MC_CMD_ ## _type ## _ ## _field ## _LBN & 0x1f,	\
		(MC_CMD_ ## _type ## _ ## _field ## _LBN & 0x1f) +	\
		MC_CMD_ ## _type ## _ ## _field ## _WIDTH - 1)

#define _MCDI_ARRAY_PTR(_buf, _field, _index, _align)			\
	(_MCDI_PTR(_buf, _MCDI_CHECK_ALIGN(MC_CMD_ ## _field ## _OFST, _align))\
	 + (_index) * _MCDI_CHECK_ALIGN(MC_CMD_ ## _field ## _LEN, _align))
#define MCDI_DECLARE_STRUCT_PTR(_name)					\
	efx_dword_t *_name
#define MCDI_ARRAY_STRUCT_PTR(_buf, _field, _index)			\
	((efx_dword_t *)_MCDI_ARRAY_PTR(_buf, _field, _index, 4))
#define MCDI_VAR_ARRAY_LEN(_len, _field)				\
	min_t(size_t, MC_CMD_ ## _field ## _MAXNUM,			\
	      ((_len) - MC_CMD_ ## _field ## _OFST) / MC_CMD_ ## _field ## _LEN)
#define MCDI_ARRAY_WORD(_buf, _field, _index)				\
	(BUILD_BUG_ON_ZERO(MC_CMD_ ## _field ## _LEN != 2) +		\
	 le16_to_cpu(*(__force const __le16 *)				\
		     _MCDI_ARRAY_PTR(_buf, _field, _index, 2)))
#define _MCDI_ARRAY_DWORD(_buf, _field, _index)				\
	(BUILD_BUG_ON_ZERO(MC_CMD_ ## _field ## _LEN != 4) +		\
	 (efx_dword_t *)_MCDI_ARRAY_PTR(_buf, _field, _index, 4))
#define MCDI_SET_ARRAY_DWORD(_buf, _field, _index, _value)		\
	EFX_SET_DWORD_FIELD(*_MCDI_ARRAY_DWORD(_buf, _field, _index),	\
			    EFX_DWORD_0, _value)
#define MCDI_ARRAY_DWORD(_buf, _field, _index)				\
	EFX_DWORD_FIELD(*_MCDI_ARRAY_DWORD(_buf, _field, _index), EFX_DWORD_0)
#define _MCDI_ARRAY_QWORD(_buf, _field, _index)				\
	(BUILD_BUG_ON_ZERO(MC_CMD_ ## _field ## _LEN != 8) +		\
	 (efx_dword_t *)_MCDI_ARRAY_PTR(_buf, _field, _index, 4))
#define MCDI_SET_ARRAY_QWORD(_buf, _field, _index, _value)		\
	do {								\
		EFX_SET_DWORD_FIELD(_MCDI_ARRAY_QWORD(_buf, _field, _index)[0],\
				    EFX_DWORD_0, (u32)(_value));	\
		EFX_SET_DWORD_FIELD(_MCDI_ARRAY_QWORD(_buf, _field, _index)[1],\
				    EFX_DWORD_0, (u64)(_value) >> 32);	\
	} while (0)
#define MCDI_ARRAY_FIELD(_buf, _field1, _type, _index, _field2)		\
	MCDI_FIELD(MCDI_ARRAY_STRUCT_PTR(_buf, _field1, _index),	\
		   _type ## _TYPEDEF, _field2)

#define MCDI_EVENT_FIELD(_ev, _field)			\
	EFX_QWORD_FIELD(_ev, MCDI_EVENT_ ## _field)

#define MCDI_CAPABILITY(field)						\
	MC_CMD_GET_CAPABILITIES_V8_OUT_ ## field ## _LBN

#define MCDI_CAPABILITY_OFST(field) \
	MC_CMD_GET_CAPABILITIES_V8_OUT_ ## field ## _OFST

#define efx_has_cap(efx, field) \
	efx->type->check_caps(efx, \
			      MCDI_CAPABILITY(field), \
			      MCDI_CAPABILITY_OFST(field))

void efx_mcdi_print_fwver(struct efx_nic *efx, char *buf, size_t len);
void efx_mcdi_dump_versions(struct efx_nic *efx, void *print_info);
int efx_mcdi_drv_attach(struct efx_nic *efx, u32 fw_variant, u32 *out_flags,
			bool reattach);
int efx_mcdi_drv_detach(struct efx_nic *efx);
int efx_mcdi_get_board_cfg(struct efx_nic *efx, int port_num, u8 *mac_address,
			   u16 *fw_subtype_list, u32 *capabilities);
int efx_mcdi_get_board_perm_mac(struct efx_nic *efx, u8 *mac_address);
int efx_mcdi_log_ctrl(struct efx_nic *efx, bool evq, bool uart, u32 dest_evq);
void efx_mcdi_log_puts(struct efx_nic *efx, const char *text);
int efx_mcdi_nvram_types(struct efx_nic *efx, u32 *nvram_types_out);
int efx_mcdi_nvram_info(struct efx_nic *efx, unsigned int type,
			size_t *size_out, size_t *erase_size_out,
			size_t *write_size_out, bool *protected_out);
int efx_mcdi_nvram_test_all(struct efx_nic *efx);
int efx_new_mcdi_nvram_test_all(struct efx_nic *efx);
int efx_mcdi_handle_assertion(struct efx_nic *efx);
void efx_mcdi_set_id_led(struct efx_nic *efx, enum efx_led_mode mode);
int efx_mcdi_wol_filter_set_magic(struct efx_nic *efx, const u8 *mac,
				  int *id_out);
int efx_mcdi_wol_filter_get_magic(struct efx_nic *efx, int *id_out);
int efx_mcdi_wol_filter_remove(struct efx_nic *efx, int id);
int efx_mcdi_wol_filter_reset(struct efx_nic *efx);
int efx_mcdi_port_probe(struct efx_nic *efx);
void efx_mcdi_port_remove(struct efx_nic *efx);
int efx_mcdi_port_reconfigure(struct efx_nic *efx);
u32 efx_mcdi_phy_get_caps(struct efx_nic *efx);
bool efx_mcdi_mac_check_fault(struct efx_nic *efx);
int efx_mcdi_reset(struct efx_nic *efx, enum reset_type method);
int efx_mcdi_set_workaround(struct efx_nic *efx, u32 type, bool enabled,
			    unsigned int *flags);
int efx_mcdi_get_workarounds(struct efx_nic *efx, unsigned int *impl_out,
			     unsigned int *enabled_out);
int efx_mcdi_get_privilege_mask(struct efx_nic *efx, u32 *mask);
int efx_mcdi_rpc_proxy_cmd(struct efx_nic *efx, u32 pf, u32 vf,
			   const void *request_buf, size_t request_size,
			   void *response_buf, size_t response_size,
			   size_t *response_size_actual);

#ifdef CONFIG_SFC_MCDI_MON
int efx_mcdi_mon_probe(struct efx_nic *efx);
void efx_mcdi_mon_remove(struct efx_nic *efx);
#else
static inline int efx_mcdi_mon_probe(struct efx_nic *efx) { return 0; }
static inline void efx_mcdi_mon_remove(struct efx_nic *efx) {}
#endif

int efx_mcdi_nvram_update_start(struct efx_nic *efx, unsigned int type);
int efx_mcdi_nvram_read(struct efx_nic *efx, unsigned int type,
			loff_t offset, u8 *buffer, size_t length);
int efx_mcdi_nvram_write(struct efx_nic *efx, unsigned int type,
			 loff_t offset, const u8 *buffer, size_t length);
int efx_mcdi_nvram_erase(struct efx_nic *efx, unsigned int type,
			 loff_t offset, size_t length);
int efx_mcdi_nvram_update_finish(struct efx_nic *efx, unsigned int type);

#ifdef CONFIG_SFC_MTD
int efx_mcdi_mtd_read(struct mtd_info *mtd, loff_t start, size_t len,
		      size_t *retlen, u8 *buffer);
int efx_mcdi_mtd_erase(struct mtd_info *mtd, loff_t start, size_t len);
int efx_mcdi_mtd_write(struct mtd_info *mtd, loff_t start, size_t len,
		       size_t *retlen, const u8 *buffer);
int efx_mcdi_mtd_sync(struct mtd_info *mtd);
void efx_mcdi_mtd_rename(struct efx_mtd_partition *part);
#endif

#endif /* EFX_MCDI_H */
