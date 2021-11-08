/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2019 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

/* MCDI interface for the ef100 Match-Action Engine */

#include "net_driver.h"
#include "tc.h"
#include "mcdi_pcol_mae.h" /* needed for various MC_CMD_MAE_*_NULL defines */

int efx_mae_allocate_mport(struct efx_nic *efx, u32 *id, u32 *label);
int efx_mae_free_mport(struct efx_nic *efx, u32 id);

void efx_mae_mport_wire(struct efx_nic *efx, u32 *out);
void efx_mae_mport_uplink(struct efx_nic *efx, u32 *out);
void efx_mae_mport_vf(struct efx_nic *efx, u32 vf_id, u32 *out);
void efx_mae_mport_mport(struct efx_nic *efx, u32 mport_id, u32 *out);

int efx_mae_lookup_mport(struct efx_nic *efx, u32 selector, u32 *id);

int efx_mae_start_counters(struct efx_nic *efx, struct efx_channel *channel);
int efx_mae_stop_counters(struct efx_nic *efx, struct efx_channel *channel);
void efx_mae_counters_grant_credits(struct work_struct *work);

#define MAE_NUM_FIELDS	(MAE_FIELD_ENC_VNET_ID + 1)

struct mae_caps {
	u32 match_field_count;
	u32 encap_types;
	u32 action_prios;
	u8 action_rule_fields[MAE_NUM_FIELDS];
	u8 outer_rule_fields[MAE_NUM_FIELDS];
};

#define MAE_ENCAP_TYPE_SUPPORTED(_caps, _type)	((_caps)->encap_type & \
						 BIT(MC_CMD_MAE_GET_CAPABILITIES_OUT_ENCAP_TYPE_ ## _type ## _LBN))

int efx_mae_get_caps(struct efx_nic *efx, struct mae_caps *caps);

#if !defined(EFX_USE_KCOMPAT) || defined(EFX_TC_OFFLOAD)
int efx_mae_match_check_caps(struct efx_nic *efx,
			     const struct efx_tc_match_fields *mask,
			     struct netlink_ext_ack *extack);
int efx_mae_match_check_caps_lhs(struct efx_nic *efx,
				 const struct efx_tc_match_fields *mask,
				 struct netlink_ext_ack *extack);
int efx_mae_check_encap_match_caps(struct efx_nic *efx, unsigned char ipv);

int efx_mae_allocate_counter(struct efx_nic *efx, struct efx_tc_counter *cnt);
int efx_mae_free_counter(struct efx_nic *efx, u32 id);

int efx_mae_allocate_encap_md(struct efx_nic *efx,
			      struct efx_tc_encap_action *encap);
int efx_mae_update_encap_md(struct efx_nic *efx,
			    struct efx_tc_encap_action *encap);
int efx_mae_free_encap_md(struct efx_nic *efx,
			  struct efx_tc_encap_action *encap);
#endif

int efx_mae_alloc_action_set(struct efx_nic *efx, struct efx_tc_action_set *act);
int efx_mae_free_action_set(struct efx_nic *efx, struct efx_tc_action_set *act);

int efx_mae_alloc_action_set_list(struct efx_nic *efx,
				  struct efx_tc_action_set_list *acts);
int efx_mae_free_action_set_list(struct efx_nic *efx,
				 struct efx_tc_action_set_list *acts);

#if !defined(EFX_USE_KCOMPAT) || defined(EFX_TC_OFFLOAD)
int efx_mae_register_encap_match(struct efx_nic *efx,
				 struct efx_tc_encap_match *encap);
int efx_mae_unregister_encap_match(struct efx_nic *efx,
				   struct efx_tc_encap_match *encap);
int efx_mae_insert_lhs_rule(struct efx_nic *efx, struct efx_tc_lhs_rule *rule,
			    u32 prio);
int efx_mae_remove_lhs_rule(struct efx_nic *efx, struct efx_tc_lhs_rule *rule);
#if !defined(EFX_USE_KCOMPAT) || defined(EFX_CONNTRACK_OFFLOAD)
int efx_mae_insert_ct(struct efx_nic *efx, struct efx_tc_ct_entry *conn);
int efx_mae_remove_ct(struct efx_nic *efx, struct efx_tc_ct_entry *conn);
#endif
#endif

int efx_mae_insert_rule(struct efx_nic *efx, const struct efx_tc_match *match,
			u32 prio, u32 acts_id, u32 *id);
int efx_mae_update_rule(struct efx_nic *efx, u32 acts_id, u32 id);
int efx_mae_delete_rule(struct efx_nic *efx, u32 id);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,5,0)
int efx_legacy_insert_rule(struct efx_nic *efx, const struct efx_legacy_match *match,
                                struct efx_legacy_action_set *act,
                                u32 prio, u32 acts_id, u32 *id);
int delete_legacy_rule(struct efx_nic *efx, const struct efx_legacy_match *match, u32 *id);
int efx_legacy_firewall_caps(struct efx_nic *efx);
#endif
