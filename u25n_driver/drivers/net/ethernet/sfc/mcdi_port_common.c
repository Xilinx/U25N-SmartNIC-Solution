/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2018 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */
#include "efx.h"
#include "nic.h"
#include "efx_common.h"
#include "mcdi_port_common.h"

static int efx_mcdi_phy_diag_type(struct efx_nic *efx);
static int efx_mcdi_phy_sff_8472_level(struct efx_nic *efx);
static u32 efx_mcdi_phy_module_type(struct efx_nic *efx);


static u8 efx_mcdi_link_state_flags(struct efx_link_state *link_state)
{
	return (link_state->up ? (1 << MC_CMD_GET_LINK_OUT_LINK_UP_LBN) : 0) |
		(link_state->fd ? (1 << MC_CMD_GET_LINK_OUT_FULL_DUPLEX_LBN) : 0);
}

static u8 efx_mcdi_link_state_fcntl(struct efx_link_state *link_state)
{
	switch (link_state->fc) {
	case EFX_FC_AUTO | EFX_FC_TX | EFX_FC_RX:
		return MC_CMD_FCNTL_AUTO;
	case EFX_FC_TX | EFX_FC_RX:
		return MC_CMD_FCNTL_BIDIR;
	case EFX_FC_RX:
		return MC_CMD_FCNTL_RESPOND;
	default:
		WARN_ON_ONCE(1);
		/* fall through */
	case 0:
		return MC_CMD_FCNTL_OFF;
	}
}

int efx_mcdi_get_phy_cfg(struct efx_nic *efx, struct efx_mcdi_phy_data *cfg)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_PHY_CFG_OUT_LEN);
	size_t outlen;
	int rc;

	BUILD_BUG_ON(MC_CMD_GET_PHY_CFG_IN_LEN != 0);
	BUILD_BUG_ON(MC_CMD_GET_PHY_CFG_OUT_NAME_LEN != sizeof(cfg->name));

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_PHY_CFG, NULL, 0,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		goto fail;

	if (outlen < MC_CMD_GET_PHY_CFG_OUT_LEN) {
		rc = -EIO;
		goto fail;
	}

	cfg->flags = MCDI_DWORD(outbuf, GET_PHY_CFG_OUT_FLAGS);
	cfg->type = MCDI_DWORD(outbuf, GET_PHY_CFG_OUT_TYPE);
	cfg->supported_cap =
		MCDI_DWORD(outbuf, GET_PHY_CFG_OUT_SUPPORTED_CAP);
	cfg->channel = MCDI_DWORD(outbuf, GET_PHY_CFG_OUT_CHANNEL);
	cfg->port = MCDI_DWORD(outbuf, GET_PHY_CFG_OUT_PRT);
	cfg->stats_mask = MCDI_DWORD(outbuf, GET_PHY_CFG_OUT_STATS_MASK);
	memcpy(cfg->name, MCDI_PTR(outbuf, GET_PHY_CFG_OUT_NAME),
	       sizeof(cfg->name));
	cfg->media = MCDI_DWORD(outbuf, GET_PHY_CFG_OUT_MEDIA_TYPE);
	cfg->mmd_mask = MCDI_DWORD(outbuf, GET_PHY_CFG_OUT_MMD_MASK);
	memcpy(cfg->revision, MCDI_PTR(outbuf, GET_PHY_CFG_OUT_REVISION),
	       sizeof(cfg->revision));

	return 0;

fail:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

void efx_link_set_wanted_fc(struct efx_nic *efx, u8 wanted_fc)
{
	efx->wanted_fc = wanted_fc;
	if (efx->link_advertising[0] & ADVERTISED_Autoneg) {
		if (wanted_fc & EFX_FC_RX)
			efx->link_advertising[0] |= (ADVERTISED_Pause |
						     ADVERTISED_Asym_Pause);
		else
			efx->link_advertising[0] &= ~(ADVERTISED_Pause |
						      ADVERTISED_Asym_Pause);
		if (wanted_fc & EFX_FC_TX)
			efx->link_advertising[0] ^= ADVERTISED_Asym_Pause;
	}
}

void efx_link_set_advertising(struct efx_nic *efx,
			      const unsigned long *advertising)
{
	memcpy(efx->link_advertising, advertising,
	       sizeof(__ETHTOOL_DECLARE_LINK_MODE_MASK()));
	if (advertising[0] & ADVERTISED_Autoneg) {
		if (advertising[0] & ADVERTISED_Pause)
			efx->wanted_fc |= (EFX_FC_TX | EFX_FC_RX);
		else
			efx->wanted_fc &= ~(EFX_FC_TX | EFX_FC_RX);
		if (advertising[0] & ADVERTISED_Asym_Pause)
			efx->wanted_fc ^= EFX_FC_TX;
	}
}

static void efx_mcdi_set_link_completer(struct efx_nic *efx,
					unsigned long cookie, int rc,
					efx_dword_t *outbuf,
					size_t outlen_actual)
{
	struct efx_mcdi_phy_data *phy_cfg = efx->phy_data;
	__ETHTOOL_DECLARE_LINK_MODE_MASK(advertising);

	/* EAGAIN means another MODULECHANGE event came in while we were
	 * doing the SET_LINK. Ignore the failure, we should be
	 * trying again shortly.
	 */
	if (rc == -EAGAIN)
		return;

	/* For other failures, nothing we can do except log it */
	if (rc) {
		if (net_ratelimit())
			netif_err(efx, link, efx->net_dev,
				  "Failed to set link settings for new module (%#x)\n",
				  rc);
		return;
	}

	mcdi_to_ethtool_linkset(efx, phy_cfg->media, cookie, advertising);
	efx_link_set_advertising(efx, advertising);
}

int efx_mcdi_set_link(struct efx_nic *efx, u32 capabilities,
		      u32 flags, u32 loopback_mode, bool async, u8 seq)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_SET_LINK_IN_V2_LEN);
	int rc;

	BUILD_BUG_ON(MC_CMD_SET_LINK_OUT_LEN != 0);

	MCDI_SET_DWORD(inbuf, SET_LINK_IN_V2_CAP, capabilities);
	MCDI_SET_DWORD(inbuf, SET_LINK_IN_V2_FLAGS, flags);
	MCDI_SET_DWORD(inbuf, SET_LINK_IN_V2_LOOPBACK_MODE, loopback_mode);
	MCDI_SET_DWORD(inbuf, SET_LINK_IN_V2_LOOPBACK_SPEED, 0 /* auto */);
	MCDI_SET_DWORD(inbuf, SET_LINK_IN_V2_MODULE_SEQ, seq);

	if (async)
		rc = efx_mcdi_rpc_async(efx, MC_CMD_SET_LINK, inbuf,
					sizeof(inbuf),
					efx_mcdi_set_link_completer,
					capabilities);
	else
		rc = efx_mcdi_rpc(efx, MC_CMD_SET_LINK, inbuf, sizeof(inbuf),
				  NULL, 0, NULL);
	return rc;
}

int efx_mcdi_loopback_modes(struct efx_nic *efx, u64 *loopback_modes)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_LOOPBACK_MODES_OUT_LEN);
	size_t outlen;
	int rc;

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_LOOPBACK_MODES, NULL, 0,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		goto fail;

	if (outlen < (MC_CMD_GET_LOOPBACK_MODES_OUT_SUGGESTED_OFST +
		      MC_CMD_GET_LOOPBACK_MODES_OUT_SUGGESTED_LEN)) {
		rc = -EIO;
		goto fail;
	}

	*loopback_modes = MCDI_QWORD(outbuf, GET_LOOPBACK_MODES_OUT_SUGGESTED);

	return 0;

fail:
	netif_err(efx, hw, efx->net_dev, "%s: failed rc=%d\n", __func__, rc);
	return rc;
}

#define MAP_CAP(mcdi_cap, ethtool_cap) do { \
		if (cap & (1 << MC_CMD_PHY_CAP_##mcdi_cap##_LBN)) \
			SET_CAP(ethtool_cap); \
		cap &= ~(1 << MC_CMD_PHY_CAP_##mcdi_cap##_LBN); \
	} while(0)
#define SET_CAP(ethtool_cap) (result |= SUPPORTED_##ethtool_cap)
static u32 mcdi_to_ethtool_cap(struct efx_nic *efx, u32 media, u32 cap)
{
	u32 result = 0;

	switch (media) {
	case MC_CMD_MEDIA_KX4:
		SET_CAP(Backplane);
		MAP_CAP(1000FDX, 1000baseKX_Full);
		MAP_CAP(10000FDX, 10000baseKX4_Full);
		MAP_CAP(40000FDX, 40000baseKR4_Full);
		break;

	case MC_CMD_MEDIA_XFP:
	case MC_CMD_MEDIA_SFP_PLUS:
	case MC_CMD_MEDIA_QSFP_PLUS:
		SET_CAP(FIBRE);
		MAP_CAP(1000FDX, 1000baseT_Full);
		MAP_CAP(10000FDX, 10000baseT_Full);
		MAP_CAP(40000FDX, 40000baseCR4_Full);
		break;

	case MC_CMD_MEDIA_BASE_T:
		SET_CAP(TP);
		MAP_CAP(10HDX, 10baseT_Half);
		MAP_CAP(10FDX, 10baseT_Full);
		MAP_CAP(100HDX, 100baseT_Half);
		MAP_CAP(100FDX, 100baseT_Full);
		MAP_CAP(1000HDX, 1000baseT_Half);
		MAP_CAP(1000FDX, 1000baseT_Full);
		MAP_CAP(10000FDX, 10000baseT_Full);
		break;
	}

	MAP_CAP(PAUSE, Pause);
	MAP_CAP(ASYM, Asym_Pause);
	MAP_CAP(AN, Autoneg);

#ifdef EFX_NOT_UPSTREAM
	if (cap & MCDI_PORT_SPEED_CAPS) {
		static bool warned;

		if (!warned) {
			netif_notice(efx, drv, efx->net_dev,
				     "This NIC has link speeds that are not supported by your kernel: %#x\n",
				     cap);
			warned = true;
		}
	}
#endif

	return result;
}
#undef SET_CAP

#if !defined(EFX_USE_KCOMPAT) || defined(EFX_HAVE_ETHTOOL_LINKSETTINGS)
#define SET_CAP(name)	__set_bit(ETHTOOL_LINK_MODE_ ## name ## _BIT, \
				  linkset)
void mcdi_to_ethtool_linkset(struct efx_nic *efx, u32 media, u32 cap,
			     unsigned long *linkset)
{
	bitmap_zero(linkset, __ETHTOOL_LINK_MODE_MASK_NBITS);
	switch (media) {
	case MC_CMD_MEDIA_KX4:
		SET_CAP(Backplane);
		MAP_CAP(1000FDX, 1000baseKX_Full);
		MAP_CAP(10000FDX, 10000baseKX4_Full);
		MAP_CAP(40000FDX, 40000baseKR4_Full);
		break;

	case MC_CMD_MEDIA_XFP:
	case MC_CMD_MEDIA_SFP_PLUS:
	case MC_CMD_MEDIA_QSFP_PLUS:
		SET_CAP(FIBRE);
		MAP_CAP(1000FDX, 1000baseT_Full);
		MAP_CAP(10000FDX, 10000baseT_Full);
		MAP_CAP(40000FDX, 40000baseCR4_Full);
#if !defined (EFX_USE_KCOMPAT) || defined (EFX_HAVE_LINK_MODE_25_50_100)
		MAP_CAP(100000FDX, 100000baseCR4_Full);
		MAP_CAP(25000FDX, 25000baseCR_Full);
		MAP_CAP(50000FDX, 50000baseCR2_Full);
#endif
		break;

	case MC_CMD_MEDIA_BASE_T:
		SET_CAP(TP);
		MAP_CAP(10HDX, 10baseT_Half);
		MAP_CAP(10FDX, 10baseT_Full);
		MAP_CAP(100HDX, 100baseT_Half);
		MAP_CAP(100FDX, 100baseT_Full);
		MAP_CAP(1000HDX, 1000baseT_Half);
		MAP_CAP(1000FDX, 1000baseT_Full);
		MAP_CAP(10000FDX, 10000baseT_Full);
		break;
	}

	MAP_CAP(PAUSE, Pause);
	MAP_CAP(ASYM, Asym_Pause);
	MAP_CAP(AN, Autoneg);

#ifdef EFX_NOT_UPSTREAM
	if (cap & MCDI_PORT_SPEED_CAPS) {
		static bool warned;

		if (!warned) {
			netif_notice(efx, drv, efx->net_dev,
				     "This NIC has link speeds that are not supported by your kernel: %#x\n",
				     cap);
			warned = true;
		}
	}
#endif
}

#ifdef EFX_HAVE_LINK_MODE_FEC_BITS
static void mcdi_fec_to_ethtool_linkset(u32 cap, unsigned long *linkset)
{
	if (cap & ((1 << MC_CMD_PHY_CAP_BASER_FEC_LBN) |
		   (1 << MC_CMD_PHY_CAP_25G_BASER_FEC_LBN)))
		SET_CAP(FEC_BASER);
	if (cap & (1 << MC_CMD_PHY_CAP_RS_FEC_LBN))
		SET_CAP(FEC_RS);
	if (!(cap & ((1 << MC_CMD_PHY_CAP_BASER_FEC_REQUESTED_LBN) |
		     (1 << MC_CMD_PHY_CAP_25G_BASER_FEC_REQUESTED_LBN) |
		     (1 << MC_CMD_PHY_CAP_RS_FEC_REQUESTED_LBN))))
		SET_CAP(FEC_NONE);
}
#endif
#undef SET_CAP
#else
void mcdi_to_ethtool_linkset(struct efx_nic *efx, u32 media, u32 cap,
			     unsigned long *linkset)
{
	linkset[0] = mcdi_to_ethtool_cap(efx, media, cap);
}
#endif
#undef MAP_CAP

static u32 ethtool_to_mcdi_cap(u32 cap)
{
	u32 result = 0;

	if (cap & SUPPORTED_10baseT_Half)
		result |= (1 << MC_CMD_PHY_CAP_10HDX_LBN);
	if (cap & SUPPORTED_10baseT_Full)
		result |= (1 << MC_CMD_PHY_CAP_10FDX_LBN);
	if (cap & SUPPORTED_100baseT_Half)
		result |= (1 << MC_CMD_PHY_CAP_100HDX_LBN);
	if (cap & SUPPORTED_100baseT_Full)
		result |= (1 << MC_CMD_PHY_CAP_100FDX_LBN);
	if (cap & SUPPORTED_1000baseT_Half)
		result |= (1 << MC_CMD_PHY_CAP_1000HDX_LBN);
	if (cap & (SUPPORTED_1000baseT_Full | SUPPORTED_1000baseKX_Full))
		result |= (1 << MC_CMD_PHY_CAP_1000FDX_LBN);
	if (cap & (SUPPORTED_10000baseT_Full | SUPPORTED_10000baseKX4_Full))
		result |= (1 << MC_CMD_PHY_CAP_10000FDX_LBN);
	if (cap & (SUPPORTED_40000baseCR4_Full | SUPPORTED_40000baseKR4_Full))
		result |= (1 << MC_CMD_PHY_CAP_40000FDX_LBN);
	if (cap & SUPPORTED_Pause)
		result |= (1 << MC_CMD_PHY_CAP_PAUSE_LBN);
	if (cap & SUPPORTED_Asym_Pause)
		result |= (1 << MC_CMD_PHY_CAP_ASYM_LBN);
	if (cap & SUPPORTED_Autoneg)
		result |= (1 << MC_CMD_PHY_CAP_AN_LBN);

	return result;
}

#if !defined(EFX_USE_KCOMPAT) || defined(EFX_HAVE_ETHTOOL_LINKSETTINGS)
u32 ethtool_linkset_to_mcdi_cap(const unsigned long *linkset)
{
	u32 result = 0;

	#define TEST_BIT(name)	test_bit(ETHTOOL_LINK_MODE_ ## name ## _BIT, \
					 linkset)

	if (TEST_BIT(10baseT_Half))
		result |= (1 << MC_CMD_PHY_CAP_10HDX_LBN);
	if (TEST_BIT(10baseT_Full))
		result |= (1 << MC_CMD_PHY_CAP_10FDX_LBN);
	if (TEST_BIT(100baseT_Half))
		result |= (1 << MC_CMD_PHY_CAP_100HDX_LBN);
	if (TEST_BIT(100baseT_Full))
		result |= (1 << MC_CMD_PHY_CAP_100FDX_LBN);
	if (TEST_BIT(1000baseT_Half))
		result |= (1 << MC_CMD_PHY_CAP_1000HDX_LBN);
	if (TEST_BIT(1000baseT_Full) || TEST_BIT(1000baseKX_Full))
		result |= (1 << MC_CMD_PHY_CAP_1000FDX_LBN);
	if (TEST_BIT(10000baseT_Full) || TEST_BIT(10000baseKX4_Full))
		result |= (1 << MC_CMD_PHY_CAP_10000FDX_LBN);
	if (TEST_BIT(40000baseCR4_Full) || TEST_BIT(40000baseKR4_Full))
		result |= (1 << MC_CMD_PHY_CAP_40000FDX_LBN);
#if !defined (EFX_USE_KCOMPAT) || defined (EFX_HAVE_LINK_MODE_25_50_100)
	if (TEST_BIT(100000baseCR4_Full))
		result |= (1 << MC_CMD_PHY_CAP_100000FDX_LBN);
	if (TEST_BIT(25000baseCR_Full))
		result |= (1 << MC_CMD_PHY_CAP_25000FDX_LBN);
	if (TEST_BIT(50000baseCR2_Full))
		result |= (1 << MC_CMD_PHY_CAP_50000FDX_LBN);
#endif
	if (TEST_BIT(Pause))
		result |= (1 << MC_CMD_PHY_CAP_PAUSE_LBN);
	if (TEST_BIT(Asym_Pause))
		result |= (1 << MC_CMD_PHY_CAP_ASYM_LBN);
	if (TEST_BIT(Autoneg))
		result |= (1 << MC_CMD_PHY_CAP_AN_LBN);

	#undef TEST_BIT

	return result;
}
#else
u32 ethtool_linkset_to_mcdi_cap(const unsigned long *linkset)
{
	return ethtool_to_mcdi_cap(linkset[0]);
}
#endif

u32 efx_get_mcdi_phy_flags(struct efx_nic *efx)
{
	struct efx_mcdi_phy_data *phy_cfg = efx->phy_data;
	enum efx_phy_mode mode, supported;
	u32 flags;

	/* TODO: Advertise the capabilities supported by this PHY */
	supported = 0;
	if (phy_cfg->flags & (1 << MC_CMD_GET_PHY_CFG_OUT_TXDIS_LBN))
		supported |= PHY_MODE_TX_DISABLED;
	if (phy_cfg->flags & (1 << MC_CMD_GET_PHY_CFG_OUT_LOWPOWER_LBN))
		supported |= PHY_MODE_LOW_POWER;
	if (phy_cfg->flags & (1 << MC_CMD_GET_PHY_CFG_OUT_POWEROFF_LBN))
		supported |= PHY_MODE_OFF;

	mode = efx->phy_mode & supported;

	flags = 0;
	if (mode & PHY_MODE_TX_DISABLED)
		flags |= (1 << MC_CMD_SET_LINK_IN_TXDIS_LBN);
	if (mode & PHY_MODE_LOW_POWER)
		flags |= (1 << MC_CMD_SET_LINK_IN_LOWPOWER_LBN);
	if (mode & PHY_MODE_OFF)
		flags |= (1 << MC_CMD_SET_LINK_IN_POWEROFF_LBN);

	if (efx->state != STATE_UNINIT && !netif_running(efx->net_dev))
		flags |= (1 << MC_CMD_SET_LINK_IN_LINKDOWN_LBN);

	return flags;
}

static u8 mcdi_to_ethtool_media(u32 media)
{
	switch (media) {
	case MC_CMD_MEDIA_XAUI:
	case MC_CMD_MEDIA_CX4:
	case MC_CMD_MEDIA_KX4:
		return PORT_OTHER;

	case MC_CMD_MEDIA_XFP:
	case MC_CMD_MEDIA_SFP_PLUS:
	case MC_CMD_MEDIA_QSFP_PLUS:
		return PORT_FIBRE;

	case MC_CMD_MEDIA_BASE_T:
		return PORT_TP;

	default:
		return PORT_OTHER;
	}
}

void efx_mcdi_phy_decode_link(struct efx_nic *efx,
			      struct efx_link_state *link_state,
			      u32 speed, u32 flags, u32 fcntl,
			      u32 ld_caps, u32 lp_caps)
{
	switch (fcntl) {
	case MC_CMD_FCNTL_AUTO:
		WARN_ON(1);	/* This is not a link mode */
		link_state->fc = EFX_FC_AUTO | EFX_FC_TX | EFX_FC_RX;
		break;
	case MC_CMD_FCNTL_BIDIR:
		link_state->fc = EFX_FC_TX | EFX_FC_RX;
		break;
	case MC_CMD_FCNTL_RESPOND:
		link_state->fc = EFX_FC_RX;
		break;
	default:
		WARN_ON(1);
		/* fall through */
	case MC_CMD_FCNTL_OFF:
		link_state->fc = 0;
		break;
	}

	link_state->up = !!(flags & (1 << MC_CMD_GET_LINK_OUT_LINK_UP_LBN));
	link_state->fd = !!(flags & (1 << MC_CMD_GET_LINK_OUT_FULL_DUPLEX_LBN));
	link_state->speed = speed;
	link_state->ld_caps = ld_caps;
	link_state->lp_caps = lp_caps;
}

/* The semantics of the ethtool FEC mode bitmask are not well defined,
 * particularly the meaning of combinations of bits.  Which means we get to
 * define our own semantics, as follows:
 * OFF overrides any other bits, and means "disable all FEC" (with the
 * exception of 25G KR4/CR4, where it is not possible to reject it if AN
 * partner requests it).
 * AUTO on its own means use cable requirements and link partner autoneg with
 * fw-default preferences for the cable type.
 * AUTO and either RS or BASER means use the specified FEC type if cable and
 * link partner support it, otherwise autoneg/fw-default.
 * RS or BASER alone means use the specified FEC type if cable and link partner
 * support it and either requests it, otherwise no FEC.
 * Both RS and BASER (whether AUTO or not) means use FEC if cable and link
 * partner support it, preferring RS to BASER.
 */
#define FEC_BIT(x) (1 << MC_CMD_PHY_CAP_##x##_LBN)
u32 ethtool_fec_caps_to_mcdi(u32 supported_cap, u32 ethtool_cap)
{
	u32 ret = 0;

	if (ethtool_cap & ETHTOOL_FEC_OFF)
		return 0;

	if (ethtool_cap & ETHTOOL_FEC_AUTO)
		ret |= (FEC_BIT(BASER_FEC) | FEC_BIT(25G_BASER_FEC) |
			FEC_BIT(RS_FEC)) &
		       supported_cap;
	if (ethtool_cap & ETHTOOL_FEC_RS && supported_cap & FEC_BIT(RS_FEC))
		ret |= FEC_BIT(RS_FEC) | FEC_BIT(RS_FEC_REQUESTED);
	if (ethtool_cap & ETHTOOL_FEC_BASER) {
		if (supported_cap & FEC_BIT(BASER_FEC))
			ret |= FEC_BIT(BASER_FEC) | FEC_BIT(BASER_FEC_REQUESTED);
		if (supported_cap & FEC_BIT(25G_BASER_FEC))
		       ret |= FEC_BIT(25G_BASER_FEC) | FEC_BIT(25G_BASER_FEC_REQUESTED);
	}
	return ret;
}

/* Basic validation to ensure that the caps we are going to attempt to 
 * set are in fact supported by the adapter. Note that no FEC is always
 * supported.
 */

static int ethtool_fec_supported(u32 supported_cap, u32 ethtool_cap)
{
	if (ethtool_cap & ETHTOOL_FEC_OFF)
		return 0;

	if (ethtool_cap &&
	    !ethtool_fec_caps_to_mcdi(supported_cap, ethtool_cap))
		return -EINVAL;
	return 0;
}

/* Invert ethtool_fec_caps_to_mcdi. */
u32 mcdi_fec_caps_to_ethtool(u32 caps, bool is_25g)
{
	bool rs = caps & FEC_BIT(RS_FEC),
	     rs_req = caps & FEC_BIT(RS_FEC_REQUESTED),
	     baser = is_25g ? caps & FEC_BIT(25G_BASER_FEC)
			    : caps & FEC_BIT(BASER_FEC),
	     baser_req = is_25g ? caps & FEC_BIT(25G_BASER_FEC_REQUESTED)
				: caps & FEC_BIT(BASER_FEC_REQUESTED);

	if (!baser && !rs)
		return ETHTOOL_FEC_OFF;
	return (rs_req ? ETHTOOL_FEC_RS : 0) |
	       (baser_req ? ETHTOOL_FEC_BASER : 0) |
	       (baser == baser_req && rs == rs_req ? 0 : ETHTOOL_FEC_AUTO);
}
#undef FEC_BIT

/* Verify that the forced flow control settings (!EFX_FC_AUTO) are
 * supported by the link partner. Warn the user if this isn't the case
 */
void efx_mcdi_phy_check_fcntl(struct efx_nic *efx, u32 lpa)
{
	struct efx_mcdi_phy_data *phy_cfg = efx->phy_data;
	u32 rmtadv;

	/* The link partner capabilities are only relevant if the
	 * link supports flow control autonegotiation */
	if (~phy_cfg->supported_cap & (1 << MC_CMD_PHY_CAP_AN_LBN))
		return;

	/* If flow control autoneg is supported and enabled, then fine */
	if (efx->wanted_fc & EFX_FC_AUTO)
		return;

	rmtadv = 0;
	if (lpa & (1 << MC_CMD_PHY_CAP_PAUSE_LBN))
		rmtadv |= ADVERTISED_Pause;
	if (lpa & (1 << MC_CMD_PHY_CAP_ASYM_LBN))
		rmtadv |=  ADVERTISED_Asym_Pause;

	if ((efx->wanted_fc & EFX_FC_TX) && rmtadv == ADVERTISED_Asym_Pause)
		netif_err(efx, link, efx->net_dev,
			  "warning: link partner doesn't support pause frames");
}

bool efx_mcdi_phy_poll(struct efx_nic *efx)
{
	struct efx_link_state old_state = efx->link_state;
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_LINK_OUT_LEN);
	int rc;

	WARN_ON(!mutex_is_locked(&efx->mac_lock));

	BUILD_BUG_ON(MC_CMD_GET_LINK_IN_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_LINK, NULL, 0,
			  outbuf, sizeof(outbuf), NULL);
	if (rc) {
		efx->link_state.up = false;
	} else {
		u32 ld_caps = MCDI_DWORD(outbuf, GET_LINK_OUT_CAP);
		u32 lp_caps = ld_caps;

		efx_mcdi_phy_decode_link(
			efx, &efx->link_state,
			MCDI_DWORD(outbuf, GET_LINK_OUT_LINK_SPEED),
			MCDI_DWORD(outbuf, GET_LINK_OUT_FLAGS),
			MCDI_DWORD(outbuf, GET_LINK_OUT_FCNTL),
			ld_caps, lp_caps);
	}

	return !efx_link_state_equal(&efx->link_state, &old_state);
}

void efx_mcdi_phy_get_settings(struct efx_nic *efx, struct ethtool_cmd *ecmd)
{
	struct efx_mcdi_phy_data *phy_cfg = efx->phy_data;
#if !defined(EFX_USE_KCOMPAT) || defined(EFX_USE_ETHTOOL_LP_ADVERTISING)
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_LINK_OUT_LEN);
	int rc;
#endif

	ecmd->supported = mcdi_to_ethtool_cap(efx, phy_cfg->media,
					      phy_cfg->supported_cap);
	ecmd->advertising = efx->link_advertising[0];
	ethtool_cmd_speed_set(ecmd, efx->link_state.speed);
	ecmd->duplex = efx->link_state.fd;
	ecmd->port = mcdi_to_ethtool_media(phy_cfg->media);
	ecmd->phy_address = phy_cfg->port;
	ecmd->transceiver = XCVR_INTERNAL;
	ecmd->autoneg = !!(efx->link_advertising[0] & ADVERTISED_Autoneg);
#if !defined(EFX_USE_KCOMPAT) || defined(EFX_USE_ETHTOOL_MDIO_SUPPORT)
	ecmd->mdio_support = (efx->mdio.mode_support &
			      (MDIO_SUPPORTS_C45 | MDIO_SUPPORTS_C22));
#endif

#if !defined(EFX_USE_KCOMPAT) || defined(EFX_USE_ETHTOOL_LP_ADVERTISING)
	BUILD_BUG_ON(MC_CMD_GET_LINK_IN_LEN != 0);
	rc = efx_mcdi_rpc(efx, MC_CMD_GET_LINK, NULL, 0,
			  outbuf, sizeof(outbuf), NULL);
	if (rc)
		return;
	ecmd->lp_advertising =
		mcdi_to_ethtool_cap(efx, phy_cfg->media,
				    MCDI_DWORD(outbuf, GET_LINK_OUT_LP_CAP));
#endif
}

static u32 ethtool_speed_to_mcdi_cap(bool duplex, u32 speed)
{
	if (duplex) {
		switch (speed) {
		case 10:     return 1 << MC_CMD_PHY_CAP_10FDX_LBN;
		case 100:    return 1 << MC_CMD_PHY_CAP_100FDX_LBN;
		case 1000:   return 1 << MC_CMD_PHY_CAP_1000FDX_LBN;
		case 10000:  return 1 << MC_CMD_PHY_CAP_10000FDX_LBN;
		case 40000:  return 1 << MC_CMD_PHY_CAP_40000FDX_LBN;
		case 100000: return 1 << MC_CMD_PHY_CAP_100000FDX_LBN;
		case 25000:  return 1 << MC_CMD_PHY_CAP_25000FDX_LBN;
		case 50000:  return 1 << MC_CMD_PHY_CAP_50000FDX_LBN;
		}
	} else {
		switch (speed) {
		case 10:     return 1 << MC_CMD_PHY_CAP_10HDX_LBN;
		case 100:    return 1 << MC_CMD_PHY_CAP_100HDX_LBN;
		case 1000:   return 1 << MC_CMD_PHY_CAP_1000HDX_LBN;
		}
	}

	return 0;
}

int efx_mcdi_phy_set_settings(struct efx_nic *efx, struct ethtool_cmd *ecmd,
			      unsigned long *new_adv)
{
	struct efx_mcdi_phy_data *phy_cfg = efx->phy_data;
	unsigned int advertising = ecmd->advertising;
	u32 caps;
	int rc;

	/* Remove flow control settings that the MAC supports
	 * but that the PHY can't advertise.
	 */
	if (~phy_cfg->supported_cap & (1 << MC_CMD_PHY_CAP_PAUSE_LBN))
		advertising &= ~ADVERTISED_Pause;
	if (~phy_cfg->supported_cap & (1 << MC_CMD_PHY_CAP_ASYM_LBN))
		advertising &= ~ADVERTISED_Asym_Pause;

	if (ecmd->autoneg)
		caps = ethtool_to_mcdi_cap(advertising) |
				1 << MC_CMD_PHY_CAP_AN_LBN;
	else
		caps = ethtool_speed_to_mcdi_cap(ecmd->duplex,
						 ethtool_cmd_speed(ecmd));
	if (!caps)
		return -EINVAL;

	caps |= ethtool_fec_caps_to_mcdi(phy_cfg->supported_cap,
					 efx->fec_config);

	rc = efx_mcdi_set_link(efx, caps, efx_get_mcdi_phy_flags(efx),
			       efx->loopback_mode, false, SET_LINK_SEQ_IGNORE);
	if (rc) {
		if (rc == -EINVAL)
			netif_err(efx, link, efx->net_dev,
				  "invalid link settings: autoneg=%u"
				  " advertising=%#x speed=%u duplex=%u"
				  " translated to caps=%#x\n",
				  ecmd->autoneg, ecmd->advertising,
				  ecmd->speed, ecmd->duplex, caps);
		return rc;
	}

	/* Rather than storing the original advertising mask, we
	 * convert the capabilities we're actually using back to an
	 * advertising mask so that (1) get_settings() will report
	 * correct information (2) we can push the capabilities again
	 * after an MC reset, or recalculate them on module change.
	 */
	mcdi_to_ethtool_linkset(efx, phy_cfg->media, caps, new_adv);

	return 1;
}

#if !defined(EFX_USE_KCOMPAT) || defined(EFX_HAVE_ETHTOOL_LINKSETTINGS)
void efx_mcdi_phy_get_ksettings(struct efx_nic *efx,
				struct ethtool_link_ksettings *out)
{
	struct efx_mcdi_phy_data *phy_cfg = efx->phy_data;
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_LINK_OUT_LEN);
	struct ethtool_link_settings *base = &out->base;
	int rc;

	if (netif_carrier_ok(efx->net_dev)) {
		base->speed = efx->link_state.speed;
		base->duplex = efx->link_state.fd ? DUPLEX_FULL : DUPLEX_HALF;
	} else {
		base->speed = 0;
		base->duplex = DUPLEX_UNKNOWN;
	}
	base->port = mcdi_to_ethtool_media(phy_cfg->media);
	base->phy_address = phy_cfg->port;
	base->autoneg = efx->link_advertising[0] & ADVERTISED_Autoneg ?
							AUTONEG_ENABLE :
							AUTONEG_DISABLE;
	base->mdio_support = (efx->mdio.mode_support &
			      (MDIO_SUPPORTS_C45 | MDIO_SUPPORTS_C22));
	mcdi_to_ethtool_linkset(efx, phy_cfg->media, phy_cfg->supported_cap,
				out->link_modes.supported);
	memcpy(out->link_modes.advertising, efx->link_advertising,
	       sizeof(__ETHTOOL_DECLARE_LINK_MODE_MASK()));

	BUILD_BUG_ON(MC_CMD_GET_LINK_IN_LEN != 0);
	rc = efx_mcdi_rpc(efx, MC_CMD_GET_LINK, NULL, 0,
			  outbuf, sizeof(outbuf), NULL);
	if (rc)
		return;
	mcdi_to_ethtool_linkset(efx, phy_cfg->media,
				MCDI_DWORD(outbuf, GET_LINK_OUT_LP_CAP),
				out->link_modes.lp_advertising);
#ifdef EFX_HAVE_LINK_MODE_FEC_BITS
	mcdi_fec_to_ethtool_linkset(MCDI_DWORD(outbuf, GET_LINK_OUT_CAP),
				    out->link_modes.advertising);
	mcdi_fec_to_ethtool_linkset(phy_cfg->supported_cap,
				    out->link_modes.supported);
	mcdi_fec_to_ethtool_linkset(MCDI_DWORD(outbuf, GET_LINK_OUT_LP_CAP),
				    out->link_modes.lp_advertising);
#endif
}

int efx_mcdi_phy_set_ksettings(struct efx_nic *efx,
			       const struct ethtool_link_ksettings *settings,
			       unsigned long *advertising)
{
	const struct ethtool_link_settings *base = &settings->base;
	struct efx_mcdi_phy_data *phy_cfg = efx->phy_data;
	u32 caps;
	int rc;

	memcpy(advertising, settings->link_modes.advertising,
	       sizeof(__ETHTOOL_DECLARE_LINK_MODE_MASK()));

	/* Remove flow control settings that the MAC supports
	 * but that the PHY can't advertise.
	 */
	if (~phy_cfg->supported_cap & (1 << MC_CMD_PHY_CAP_PAUSE_LBN))
		__clear_bit(ETHTOOL_LINK_MODE_Pause_BIT, advertising);
	if (~phy_cfg->supported_cap & (1 << MC_CMD_PHY_CAP_ASYM_LBN))
		__clear_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT, advertising);

	if (base->autoneg)
		caps = ethtool_linkset_to_mcdi_cap(advertising) |
					1 << MC_CMD_PHY_CAP_AN_LBN;
	else
		caps = ethtool_speed_to_mcdi_cap(base->duplex, base->speed);
	if (!caps)
		return -EINVAL;

	caps |= ethtool_fec_caps_to_mcdi(phy_cfg->supported_cap,
					 efx->fec_config);

	rc = efx_mcdi_set_link(efx, caps, efx_get_mcdi_phy_flags(efx),
			       efx->loopback_mode, false, SET_LINK_SEQ_IGNORE);
	if (rc) {
		if (rc == -EINVAL)
			netif_err(efx, link, efx->net_dev,
				  "invalid link settings: autoneg=%u"
				  " advertising=%*pb speed=%u duplex=%u"
				  " translated to caps=%#x\n",
				  base->autoneg, __ETHTOOL_LINK_MODE_MASK_NBITS,
				  settings->link_modes.advertising, base->speed,
				  base->duplex, caps);
		return rc;
	}

	/* Rather than storing the original advertising mask, we
	 * convert the capabilities we're actually using back to an
	 * advertising mask so that (1) get_settings() will report
	 * correct information (2) we can push the capabilities again
	 * after an MC reset.
	 */
	mcdi_to_ethtool_linkset(efx, phy_cfg->media, caps, advertising);

	return 1;
}
#endif

int efx_mcdi_phy_get_fecparam(struct efx_nic *efx, struct ethtool_fecparam *fec)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_LINK_OUT_V2_LEN);
	u32 caps, active, speed; /* MCDI format */
	bool is_25g = false;
	size_t outlen;
	int rc;

	BUILD_BUG_ON(MC_CMD_GET_LINK_IN_LEN != 0);
	rc = efx_mcdi_rpc(efx, MC_CMD_GET_LINK, NULL, 0,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	if (outlen < MC_CMD_GET_LINK_OUT_V2_LEN)
		return -EOPNOTSUPP;

	/* behaviour for 25G/50G links depends on 25G BASER bit */
	speed = MCDI_DWORD(outbuf, GET_LINK_OUT_V2_LINK_SPEED);
	is_25g = speed == 25000 || speed == 50000;

	caps = MCDI_DWORD(outbuf, GET_LINK_OUT_V2_CAP);
	fec->fec = mcdi_fec_caps_to_ethtool(caps, is_25g);
	/* BASER is never supported on 100G */
	if (speed == 100000)
		fec->fec &= ~ETHTOOL_FEC_BASER;

	active = MCDI_DWORD(outbuf, GET_LINK_OUT_V2_FEC_TYPE);
	switch (active) {
	case MC_CMD_FEC_NONE:
		fec->active_fec = ETHTOOL_FEC_OFF;
		break;
	case MC_CMD_FEC_BASER:
		fec->active_fec = ETHTOOL_FEC_BASER;
		break;
	case MC_CMD_FEC_RS:
		fec->active_fec = ETHTOOL_FEC_RS;
		break;
	default:
		netif_warn(efx, hw, efx->net_dev,
			   "Firmware reports unrecognised FEC_TYPE %u\n",
			   active);
		/* We don't know what firmware has picked.  AUTO is as good a
		 * "can't happen" value as any other.
		 */
		fec->active_fec = ETHTOOL_FEC_AUTO;
		break;
	}

	return 0;
}

int efx_mcdi_phy_set_fecparam(struct efx_nic *efx,
			      const struct ethtool_fecparam *fec)
{
	u32 caps = ethtool_linkset_to_mcdi_cap(efx->link_advertising);
	struct efx_mcdi_phy_data *phy_data = efx->phy_data;
	int rc;

	rc = ethtool_fec_supported(phy_data->supported_cap, fec->fec);
	if (rc)
		return rc;

	caps |= ethtool_fec_caps_to_mcdi(phy_data->supported_cap, fec->fec);
	rc = efx_mcdi_set_link(efx, caps, efx_get_mcdi_phy_flags(efx),
			       efx->loopback_mode, false, SET_LINK_SEQ_IGNORE);
	if (rc)
		return rc;

	/* Record the new FEC setting for subsequent set_link calls */
	efx->fec_config = fec->fec;
	return 0;
}

int efx_mcdi_phy_test_alive(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_PHY_STATE_OUT_LEN);
	size_t outlen;
	int rc;

	BUILD_BUG_ON(MC_CMD_GET_PHY_STATE_IN_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_PHY_STATE, NULL, 0,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;

	if (outlen < MC_CMD_GET_PHY_STATE_OUT_LEN)
		return -EIO;
	if (MCDI_DWORD(outbuf, GET_PHY_STATE_OUT_STATE) != MC_CMD_PHY_STATE_OK)
		return -EINVAL;

	return 0;
}

static const char *const mcdi_sft9001_cable_diag_names[] = {
	"cable.pairA.length",
	"cable.pairB.length",
	"cable.pairC.length",
	"cable.pairD.length",
	"cable.pairA.status",
	"cable.pairB.status",
	"cable.pairC.status",
	"cable.pairD.status",
};

const char *efx_mcdi_phy_test_name(struct efx_nic *efx,
				   unsigned int index)
{
	struct efx_mcdi_phy_data *phy_cfg = efx->phy_data;

	if (phy_cfg->flags & (1 << MC_CMD_GET_PHY_CFG_OUT_BIST_LBN)) {
		if (index == 0)
			return "bist";
		--index;
	}

	if (phy_cfg->flags &
	    ((1 << MC_CMD_GET_PHY_CFG_OUT_BIST_CABLE_SHORT_LBN) |
	     (1 << MC_CMD_GET_PHY_CFG_OUT_BIST_CABLE_LONG_LBN))) {
		if (index == 0)
			return "cable";
		--index;

		if (efx->phy_type == PHY_TYPE_SFT9001B) {
			if (index < ARRAY_SIZE(mcdi_sft9001_cable_diag_names))
				return mcdi_sft9001_cable_diag_names[index];
			index -= ARRAY_SIZE(mcdi_sft9001_cable_diag_names);
		}
	}

	return NULL;
}

u32 efx_get_mcdi_caps(struct efx_nic *efx)
{
	struct efx_mcdi_phy_data *phy_data = efx->phy_data;

	return ethtool_linkset_to_mcdi_cap(efx->link_advertising) |
	       ethtool_fec_caps_to_mcdi(phy_data->supported_cap,
					efx->fec_config);
}

int efx_mcdi_port_reconfigure(struct efx_nic *efx)
{
	return efx_mcdi_set_link(efx, efx_get_mcdi_caps(efx),
				 efx_get_mcdi_phy_flags(efx),
				 efx->loopback_mode, false,
				 SET_LINK_SEQ_IGNORE);
}

static unsigned int efx_calc_mac_mtu(struct efx_nic *efx)
{
	return EFX_MAX_FRAME_LEN(efx->net_dev->mtu);
}

int efx_mcdi_set_mac(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(cmdbytes, MC_CMD_SET_MAC_IN_LEN);
	bool forward_fcs;
	u32 fcntl;

	BUILD_BUG_ON(MC_CMD_SET_MAC_OUT_LEN != 0);

	/* This has no effect on EF10 */
	ether_addr_copy(MCDI_PTR(cmdbytes, SET_MAC_IN_ADDR),
			efx->net_dev->dev_addr);

	MCDI_SET_DWORD(cmdbytes, SET_MAC_IN_MTU, efx_calc_mac_mtu(efx));
	MCDI_SET_DWORD(cmdbytes, SET_MAC_IN_DRAIN, 0);

	/* Set simple MAC filter for Siena */
	MCDI_POPULATE_DWORD_1(cmdbytes, SET_MAC_IN_REJECT,
			      SET_MAC_IN_REJECT_UNCST, efx->unicast_filter);

#if defined(EFX_USE_KCOMPAT) && !defined(EFX_HAVE_ETHTOOL_FCS)
	forward_fcs = efx->forward_fcs;
#else
	forward_fcs = !!(efx->net_dev->features & NETIF_F_RXFCS);
#endif
	MCDI_POPULATE_DWORD_1(cmdbytes, SET_MAC_IN_FLAGS,
			      SET_MAC_IN_FLAG_INCLUDE_FCS, forward_fcs);

	switch (efx->wanted_fc) {
	case EFX_FC_RX | EFX_FC_TX:
		fcntl = MC_CMD_FCNTL_BIDIR;
		break;
	case EFX_FC_RX:
		fcntl = MC_CMD_FCNTL_RESPOND;
		break;
	default:
		fcntl = MC_CMD_FCNTL_OFF;
		break;
	}
	if (efx->wanted_fc & EFX_FC_AUTO)
		fcntl = MC_CMD_FCNTL_AUTO;
	if (efx->fc_disable)
		fcntl = MC_CMD_FCNTL_OFF;

	MCDI_SET_DWORD(cmdbytes, SET_MAC_IN_FCNTL, fcntl);

	return efx_mcdi_rpc(efx, MC_CMD_SET_MAC, cmdbytes, sizeof(cmdbytes),
			    NULL, 0, NULL);
}

int efx_mcdi_set_mtu(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_SET_MAC_EXT_IN_LEN);

	BUILD_BUG_ON(MC_CMD_SET_MAC_OUT_LEN != 0);

	MCDI_SET_DWORD(inbuf, SET_MAC_EXT_IN_MTU, efx_calc_mac_mtu(efx));

	MCDI_POPULATE_DWORD_1(inbuf, SET_MAC_EXT_IN_CONTROL,
			      SET_MAC_EXT_IN_CFG_MTU, 1);

	return efx_mcdi_rpc(efx, MC_CMD_SET_MAC, inbuf, sizeof(inbuf),
			    NULL, 0, NULL);
}

/*	MAC statistics
 */
enum efx_stats_action {
	EFX_STATS_ENABLE,
	EFX_STATS_DISABLE,
	EFX_STATS_PULL,
	EFX_STATS_PERIOD,
};

static int efx_mcdi_mac_stats(struct efx_nic *efx,
			      enum efx_stats_action action, int clear)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_MAC_STATS_IN_LEN);
	int rc;
	int dma, change;
	dma_addr_t dma_addr = efx->stats_buffer.dma_addr;
	u32 dma_len;

	if (action == EFX_STATS_ENABLE)
		efx->stats_enabled = true;
	else if (action == EFX_STATS_DISABLE)
		efx->stats_enabled = false;

	dma = action == EFX_STATS_PULL || efx->stats_enabled;
	change = action != EFX_STATS_PULL;

	dma_len = dma ? efx->num_mac_stats * sizeof(u64) : 0;

	BUILD_BUG_ON(MC_CMD_MAC_STATS_OUT_DMA_LEN != 0);

	MCDI_SET_QWORD(inbuf, MAC_STATS_IN_DMA_ADDR, dma_addr);
	MCDI_POPULATE_DWORD_7(inbuf, MAC_STATS_IN_CMD,
			      MAC_STATS_IN_DMA, dma,
			      MAC_STATS_IN_CLEAR, clear,
			      MAC_STATS_IN_PERIODIC_CHANGE, change,
			      MAC_STATS_IN_PERIODIC_ENABLE, efx->stats_enabled,
			      MAC_STATS_IN_PERIODIC_CLEAR, 0,
			      MAC_STATS_IN_PERIODIC_NOEVENT, 1,
			      MAC_STATS_IN_PERIOD_MS, efx->stats_period_ms);
	MCDI_SET_DWORD(inbuf, MAC_STATS_IN_DMA_LEN, dma_len);

	if (efx_nic_rev(efx) >= EFX_REV_HUNT_A0)
		MCDI_SET_DWORD(inbuf, MAC_STATS_IN_PORT_ID,
			       efx->vport.vport_id);

	rc = efx_mcdi_rpc_quiet(efx, MC_CMD_MAC_STATS, inbuf, sizeof(inbuf),
				NULL, 0, NULL);
	/* Expect ENOENT if DMA queues have not been set up */
	if (rc && (rc != -ENOENT || atomic_read(&efx->active_queues)))
		efx_mcdi_display_error(efx, MC_CMD_MAC_STATS, sizeof(inbuf),
				       NULL, 0, rc);
	return rc;
}

void efx_mcdi_mac_start_stats(struct efx_nic *efx)
{
	__le64 *dma_stats = efx->stats_buffer.addr;

	dma_stats[efx->num_mac_stats - 1] = EFX_MC_STATS_GENERATION_INVALID;

	efx_mcdi_mac_stats(efx, EFX_STATS_ENABLE, 0);
}

void efx_mcdi_mac_stop_stats(struct efx_nic *efx)
{
	efx_mcdi_mac_stats(efx, EFX_STATS_DISABLE, 0);
}

void efx_mcdi_mac_update_stats_period(struct efx_nic *efx)
{
	efx_mcdi_mac_stats(efx, EFX_STATS_PERIOD, 0);
}

#define EFX_MAC_STATS_WAIT_MS 1000

void efx_mcdi_mac_pull_stats(struct efx_nic *efx)
{
	__le64 *dma_stats = efx->stats_buffer.addr;
	unsigned long end;

	if (!dma_stats)
		return;

	dma_stats[efx->num_mac_stats - 1] = EFX_MC_STATS_GENERATION_INVALID;
	efx_mcdi_mac_stats(efx, EFX_STATS_PULL, 0);

	end = jiffies + msecs_to_jiffies(EFX_MAC_STATS_WAIT_MS);

	while (READ_ONCE(dma_stats[efx->num_mac_stats - 1]) ==
				EFX_MC_STATS_GENERATION_INVALID &&
	       time_before(jiffies, end))
		usleep_range(100, 1000);
}

int efx_mcdi_mac_init_stats(struct efx_nic *efx)
{
	int rc;

	if (!efx->num_mac_stats)
		return 0;

	/* Allocate buffer for stats */
	rc = efx_nic_alloc_buffer(efx, &efx->stats_buffer,
				  efx->num_mac_stats * sizeof(u64), GFP_KERNEL);
	if (rc) {
		netif_warn(efx, probe, efx->net_dev,
			   "failed to allocate DMA buffer: %d\n", rc);
		return rc;
	}

	efx->mc_initial_stats =
		kcalloc(efx->num_mac_stats, sizeof(u64), GFP_KERNEL);
	if (!efx->mc_initial_stats) {
		netif_warn(efx, probe, efx->net_dev,
			   "failed to allocate initial MC stats buffer\n");
		rc = -ENOMEM;
		goto fail;
	}

	netif_dbg(efx, probe, efx->net_dev,
		  "stats buffer at %llx (virt %p phys %llx)\n",
		  (u64) efx->stats_buffer.dma_addr,
		  efx->stats_buffer.addr,
		  (u64) virt_to_phys(efx->stats_buffer.addr));

	return 0;

fail:
	efx_mcdi_mac_fini_stats(efx);
	return rc;
}

void efx_mcdi_mac_fini_stats(struct efx_nic *efx)
{
	kfree(efx->mc_initial_stats);
	efx->mc_initial_stats = NULL;
	efx_nic_free_buffer(efx, &efx->stats_buffer);
}

/* Get physical port number (EF10 only; on Siena it is same as PF number) */
int efx_mcdi_port_get_number(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_PORT_ASSIGNMENT_OUT_LEN);
	int rc;

	rc = efx_mcdi_rpc(efx, MC_CMD_GET_PORT_ASSIGNMENT, NULL, 0,
			  outbuf, sizeof(outbuf), NULL);
	if (rc)
		return rc;

	return MCDI_DWORD(outbuf, GET_PORT_ASSIGNMENT_OUT_PORT);
}

/*	Event processing
 */
static unsigned int efx_mcdi_event_link_speed[] = {
	[MCDI_EVENT_LINKCHANGE_SPEED_100M] = 100,
	[MCDI_EVENT_LINKCHANGE_SPEED_1G] = 1000,
	[MCDI_EVENT_LINKCHANGE_SPEED_10G] = 10000,
	[MCDI_EVENT_LINKCHANGE_SPEED_40G] = 40000,
	[MCDI_EVENT_LINKCHANGE_SPEED_25G] = 25000,
	[MCDI_EVENT_LINKCHANGE_SPEED_50G] = 50000,
	[MCDI_EVENT_LINKCHANGE_SPEED_100G] = 100000,
};

void efx_mcdi_process_link_change(struct efx_nic *efx, efx_qword_t *ev)
{
	u32 flags, fcntl, speed, lpa;

	speed = EFX_QWORD_FIELD(*ev, MCDI_EVENT_LINKCHANGE_SPEED);
	if (speed < ARRAY_SIZE(efx_mcdi_event_link_speed)) {
		speed = efx_mcdi_event_link_speed[speed];
	} else {
		if (net_ratelimit())
			netif_warn(efx, hw, efx->net_dev,
				   "Invalid speed enum %d in link change event",
				   speed);
		speed = 0;
	}

	flags = efx_mcdi_link_state_flags(&efx->link_state) &
		~(1 << MC_CMD_GET_LINK_OUT_LINK_UP_LBN);
	flags |= EFX_QWORD_FIELD(*ev, MCDI_EVENT_LINKCHANGE_LINK_FLAGS);
	fcntl = EFX_QWORD_FIELD(*ev, MCDI_EVENT_LINKCHANGE_FCNTL);
	lpa = EFX_QWORD_FIELD(*ev, MCDI_EVENT_LINKCHANGE_LP_CAP);

	/* efx->link_state is only modified by efx_mcdi_phy_get_link(),
	 * which is only run after flushing the event queues. Therefore, it
	 * is safe to modify the link state outside of the mac_lock here.
	 */
	efx_mcdi_phy_decode_link(efx, &efx->link_state, speed, flags, fcntl,
			efx->link_state.ld_caps, lpa);
	efx_link_status_changed(efx);

	efx_mcdi_phy_check_fcntl(efx, lpa);
}

void efx_mcdi_process_link_change_v2(struct efx_nic *efx, efx_qword_t *ev)
{
	u32 link_up, flags, fcntl, speed, lpa;

	speed = EFX_QWORD_FIELD(*ev, MCDI_EVENT_LINKCHANGE_V2_SPEED);
	EFX_WARN_ON_PARANOID(speed >= ARRAY_SIZE(efx_mcdi_event_link_speed));
	speed = efx_mcdi_event_link_speed[speed];

	link_up = EFX_QWORD_FIELD(*ev, MCDI_EVENT_LINKCHANGE_V2_FLAGS_LINK_UP);
	fcntl = EFX_QWORD_FIELD(*ev, MCDI_EVENT_LINKCHANGE_V2_FCNTL);
	lpa = EFX_QWORD_FIELD(*ev, MCDI_EVENT_LINKCHANGE_V2_LP_CAP);
	flags = efx_mcdi_link_state_flags(&efx->link_state) &
		~(1 << MC_CMD_GET_LINK_OUT_LINK_UP_LBN);
	flags |= !!link_up << MC_CMD_GET_LINK_OUT_LINK_UP_LBN;
	flags |= 1 << MC_CMD_GET_LINK_OUT_FULL_DUPLEX_LBN;

	efx_mcdi_phy_decode_link(efx, &efx->link_state, speed, flags, fcntl,
				 efx->link_state.ld_caps, lpa);

	efx_link_status_changed(efx);

	efx_mcdi_phy_check_fcntl(efx, lpa);
}

void efx_mcdi_process_module_change(struct efx_nic *efx, efx_qword_t *ev)
{
	struct efx_mcdi_phy_data *phy_cfg = efx->phy_data;
	u32 caps = efx_get_mcdi_caps(efx);
	u8 flags = efx_mcdi_link_state_flags(&efx->link_state);
	u32 ld_caps;
	u8 seq;

	ld_caps = EFX_QWORD_FIELD(*ev, MCDI_EVENT_MODULECHANGE_LD_CAP);
	seq = EFX_DWORD_FIELD(*ev, MCDI_EVENT_MODULECHANGE_SEQ);

	flags |= 1 << MC_CMD_GET_LINK_OUT_FULL_DUPLEX_LBN;

	/* efx->link_state is only modified by efx_mcdi_phy_get_link(),
	 * which is only run after flushing the event queues. Therefore, it
	 * is safe to modify the link state outside of the mac_lock here.
	 */
	efx_mcdi_phy_decode_link(efx, &efx->link_state, efx->link_state.speed,
				 flags,
				 efx_mcdi_link_state_fcntl(&efx->link_state),
				 ld_caps, 0);

	/* See if efx->link_advertising works with the new module's ld_caps.
	 * If it doesn't, reset to all speeds rather than go with no speed.
	 */
	if (ld_caps && caps && !(caps & ld_caps & MCDI_PORT_SPEED_CAPS)) {
		caps |= (phy_cfg->supported_cap & MCDI_PORT_SPEED_CAPS);
		caps &= ld_caps;

		if (caps & MCDI_PORT_SPEED_CAPS)
			netif_info(efx, link, efx->net_dev,
				   "No configured speeds were supported with the new module. Resetting speed to default (caps=%#x)\n",
				   caps);
		else
			netif_info(efx, link, efx->net_dev,
				   "No speeds were supported with the new module (caps=%#x)\n",
				   caps);

		if (efx_mcdi_set_link(efx, caps, efx_get_mcdi_phy_flags(efx),
				      efx->loopback_mode, true, seq) == 0)
			mcdi_to_ethtool_linkset(efx, phy_cfg->media, caps,
						efx->link_advertising);
	}

	efx_link_status_changed(efx);
}

static void efx_handle_drain_event(struct efx_nic *efx)
{
	if (atomic_dec_and_test(&efx->active_queues))
		wake_up(&efx->flush_wq);

	WARN_ON(atomic_read(&efx->active_queues) < 0);
}

bool efx_mcdi_port_process_event_common(struct efx_channel *channel,
					efx_qword_t *event, int *rc, int budget)
{
	struct efx_nic *efx = channel->efx;
	int code = EFX_QWORD_FIELD(*event, MCDI_EVENT_CODE);

	switch (code) {
	case MCDI_EVENT_CODE_LINKCHANGE:
		efx_mcdi_process_link_change(efx, event);
		return true;
	case MCDI_EVENT_CODE_LINKCHANGE_V2:
		efx_mcdi_process_link_change_v2(efx, event);
		return true;
	case MCDI_EVENT_CODE_MODULECHANGE:
		efx_mcdi_process_module_change(efx, event);
		return true;
	case MCDI_EVENT_CODE_TX_FLUSH:
	case MCDI_EVENT_CODE_RX_FLUSH:
		/* Two flush events will be sent: one to the same event
		 * queue as completions, and one to event queue 0.
		 * In the latter case the {RX,TX}_FLUSH_TO_DRIVER
		 * flag will be set, and we should ignore the event
		 * because we want to wait for all completions.
		 */
		BUILD_BUG_ON(MCDI_EVENT_TX_FLUSH_TO_DRIVER_LBN !=
			     MCDI_EVENT_RX_FLUSH_TO_DRIVER_LBN);

		if (!MCDI_EVENT_FIELD(*event, TX_FLUSH_TO_DRIVER))
			efx_handle_drain_event(efx);
#ifdef EFX_NOT_UPSTREAM
#ifdef CONFIG_SFC_DRIVERLINK
		else
			*rc = efx_dl_handle_event(&efx->dl_nic, event, budget);
#endif
#endif
		return true;
	}

	return false;
}

static int efx_mcdi_bist(struct efx_nic *efx, unsigned int bist_mode,
			 int *results)
{
	unsigned int retry, i, count = 0;
	size_t outlen;
	u32 status;
	MCDI_DECLARE_BUF(inbuf, MC_CMD_START_BIST_IN_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_POLL_BIST_OUT_SFT9001_LEN);
	u8 *ptr;
	int rc;

	BUILD_BUG_ON(MC_CMD_START_BIST_OUT_LEN != 0);
	MCDI_SET_DWORD(inbuf, START_BIST_IN_TYPE, bist_mode);
	rc = efx_mcdi_rpc(efx, MC_CMD_START_BIST,
			  inbuf, MC_CMD_START_BIST_IN_LEN, NULL, 0, NULL);
	if (rc)
		goto out;

	/* Wait up to 10s for BIST to finish */
	for (retry = 0; retry < 100; ++retry) {
		BUILD_BUG_ON(MC_CMD_POLL_BIST_IN_LEN != 0);
		rc = efx_mcdi_rpc(efx, MC_CMD_POLL_BIST, NULL, 0,
				  outbuf, sizeof(outbuf), &outlen);
		if (rc)
			goto out;

		status = MCDI_DWORD(outbuf, POLL_BIST_OUT_RESULT);
		if (status != MC_CMD_POLL_BIST_RUNNING)
			goto finished;

		msleep(100);
	}

	rc = -ETIMEDOUT;
	goto out;

finished:
	results[count++] = (status == MC_CMD_POLL_BIST_PASSED) ? 1 : -1;

	/* SFT9001 specific cable diagnostics output */
	if (efx->phy_type == PHY_TYPE_SFT9001B &&
	    (bist_mode == MC_CMD_PHY_BIST_CABLE_SHORT ||
	     bist_mode == MC_CMD_PHY_BIST_CABLE_LONG)) {
		ptr = MCDI_PTR(outbuf, POLL_BIST_OUT_SFT9001_CABLE_LENGTH_A);
		if (status == MC_CMD_POLL_BIST_PASSED &&
		    outlen >= MC_CMD_POLL_BIST_OUT_SFT9001_LEN) {
			for (i = 0; i < 8; i++) {
				results[count + i] =
					EFX_DWORD_FIELD(((efx_dword_t *)ptr)[i],
							EFX_DWORD_0);
			}
		}
		count += 8;
	}
	rc = count;

out:
	return rc;
}

int efx_mcdi_phy_run_tests(struct efx_nic *efx, int *results,
			   unsigned int flags)
{
	struct efx_mcdi_phy_data *phy_cfg = efx->phy_data;
	u32 mode;
	int rc;

	if (phy_cfg->flags & (1 << MC_CMD_GET_PHY_CFG_OUT_BIST_LBN)) {
		rc = efx_mcdi_bist(efx, MC_CMD_PHY_BIST, results);
		if (rc < 0)
			return rc;

		results += rc;
	}

	/* If we support both LONG and SHORT, then run each in response to
	 * break or not. Otherwise, run the one we support
	 */
	mode = 0;
	if (phy_cfg->flags &
	    (1 << MC_CMD_GET_PHY_CFG_OUT_BIST_CABLE_SHORT_LBN)) {
		if ((flags & ETH_TEST_FL_OFFLINE) &&
		    (phy_cfg->flags &
		     (1 << MC_CMD_GET_PHY_CFG_OUT_BIST_CABLE_LONG_LBN)))
			mode = MC_CMD_PHY_BIST_CABLE_LONG;
		else
			mode = MC_CMD_PHY_BIST_CABLE_SHORT;
	} else if (phy_cfg->flags &
		   (1 << MC_CMD_GET_PHY_CFG_OUT_BIST_CABLE_LONG_LBN))
		mode = MC_CMD_PHY_BIST_CABLE_LONG;

	if (mode != 0) {
		rc = efx_mcdi_bist(efx, mode, results);
		if (rc < 0)
			return rc;
		results += rc;
	}

	return 0;
}

#define SFP_PAGE_SIZE	   128
#define SFF_DIAG_TYPE_OFFSET    92
#define SFF_DIAG_ADDR_CHANGE    (1 << 2)
#define SFF_DIAG_IMPLEMENTED	(1 << 6)
#define SFF_8079_NUM_PAGES      2
#define SFF_8472_NUM_PAGES      4
#define SFF_8436_NUM_PAGES      5
#define SFF_DMT_LEVEL_OFFSET    94

/** efx_mcdi_phy_get_module_eeprom_page() - Get a single page of module eeprom
 * @efx:	NIC context
 * @page:       EEPROM page number
 * @data:       Destination data pointer
 * @offset:     Offset in page to copy from in to data
 * @space:      Space available in data
 *
 * Return:
 *   >=0 - amount of data copied
 *   <0  - error
 */
static int efx_mcdi_phy_get_module_eeprom_page(struct efx_nic *efx,
					       unsigned int page,
					       u8 *data, ssize_t offset,
					       ssize_t space)
{
	MCDI_DECLARE_BUF(outbuf, MC_CMD_GET_PHY_MEDIA_INFO_OUT_LENMAX);
	MCDI_DECLARE_BUF(inbuf, MC_CMD_GET_PHY_MEDIA_INFO_IN_LEN);
	size_t outlen;
	unsigned int payload_len;
	unsigned int to_copy;
	int rc;

	if (offset > SFP_PAGE_SIZE)
		return -EINVAL;

	to_copy = min(space, SFP_PAGE_SIZE - offset);

	MCDI_SET_DWORD(inbuf, GET_PHY_MEDIA_INFO_IN_PAGE, page);
	rc = efx_mcdi_rpc_quiet(efx, MC_CMD_GET_PHY_MEDIA_INFO,
			inbuf, sizeof(inbuf),
			outbuf, sizeof(outbuf),
			&outlen);

	if (rc)
		return rc;

	if (outlen < (MC_CMD_GET_PHY_MEDIA_INFO_OUT_DATA_OFST +
			SFP_PAGE_SIZE)) {
		/* There are SFP+ modules that claim to be SFF-8472 compliant
		 * and do not provide diagnostic information, but they don't
		 * return all bits 0 as the spec says they should... */
		if (page >= 2 && page < 4 &&
		    efx_mcdi_phy_module_type(efx) == MC_CMD_MEDIA_SFP_PLUS &&
		    efx_mcdi_phy_sff_8472_level(efx) > 0 &&
		    (efx_mcdi_phy_diag_type(efx) & SFF_DIAG_IMPLEMENTED) == 0) {
			memset(data, 0, to_copy);

			return to_copy;
		}

		return -EIO;
	}

	payload_len = MCDI_DWORD(outbuf, GET_PHY_MEDIA_INFO_OUT_DATALEN);
	if (payload_len != SFP_PAGE_SIZE)
		return -EIO;

	memcpy(data, MCDI_PTR(outbuf, GET_PHY_MEDIA_INFO_OUT_DATA) + offset,
		       to_copy);

	return to_copy;
}

static int efx_mcdi_phy_get_module_eeprom_byte(struct efx_nic *efx,
					       unsigned int page,
					       u8 byte)
{
	int rc;
	u8 data;

	rc = efx_mcdi_phy_get_module_eeprom_page(efx, page, &data, byte, 1);
	if (rc == 1)
		return data;

	return rc;
}

static int efx_mcdi_phy_diag_type(struct efx_nic *efx)
{
	/* Page zero of the EEPROM includes the diagnostic type at byte 92. */
	return efx_mcdi_phy_get_module_eeprom_byte(efx, 0,
			SFF_DIAG_TYPE_OFFSET);
}

static int efx_mcdi_phy_sff_8472_level(struct efx_nic *efx)
{
	/* Page zero of the EEPROM includes the DMT level at byte 94. */
	return efx_mcdi_phy_get_module_eeprom_byte(efx, 0,
			SFF_DMT_LEVEL_OFFSET);
}

static u32 efx_mcdi_phy_module_type(struct efx_nic *efx)
{
	struct efx_mcdi_phy_data *phy_data = efx->phy_data;

	if (phy_data->media != MC_CMD_MEDIA_QSFP_PLUS)
		return phy_data->media;

	/* A QSFP+ NIC may actually have an SFP+ module attached.
	 * The ID is page 0, byte 0.
	 * QSFP28 is of type SFF_8636, however, this is treated
	 * the same by ethtool, so we can also treat them the same.
	 */
	switch (efx_mcdi_phy_get_module_eeprom_byte(efx, 0, 0)) {
	case 0x3: // SFP
		return MC_CMD_MEDIA_SFP_PLUS;
	case 0xc:  // QSFP
	case 0xd:  // QSFP+
	case 0x11: // QSFP28
		return MC_CMD_MEDIA_QSFP_PLUS;
	default:
		return 0;
	}
}

static
int efx_mcdi_phy_get_module_eeprom_locked(struct efx_nic *efx,
					  struct ethtool_eeprom *ee, u8 *data)
{
	int rc;
	ssize_t space_remaining = ee->len;
	unsigned int page_off;
	bool ignore_missing;
	int num_pages;
	int page;

	switch (efx_mcdi_phy_module_type(efx)) {
	case MC_CMD_MEDIA_SFP_PLUS:
		num_pages = efx_mcdi_phy_sff_8472_level(efx) > 0 ?
				SFF_8472_NUM_PAGES : SFF_8079_NUM_PAGES;
		page = 0;
		ignore_missing = false;
		break;
	case MC_CMD_MEDIA_QSFP_PLUS:
		num_pages = SFF_8436_NUM_PAGES;
		page = -1; /* We obtain the lower page by asking for -1. */
		ignore_missing = true; /* Ignore missing pages after page 0. */
		break;
	default:
		return -EOPNOTSUPP;
	}

	page_off = ee->offset % SFP_PAGE_SIZE;
	page += ee->offset / SFP_PAGE_SIZE;

	while (space_remaining && (page < num_pages)) {
		rc = efx_mcdi_phy_get_module_eeprom_page(efx, page,
				data, page_off, space_remaining);

		if (rc > 0) {
			space_remaining -= rc;
			data += rc;
			page_off = 0;
			page++;
		} else if (rc == 0) {
			space_remaining = 0;
		} else if (ignore_missing && (page > 0)) {
			int intended_size = SFP_PAGE_SIZE - page_off;

			space_remaining -= intended_size;
			if (space_remaining < 0) {
				space_remaining = 0;
			} else {
				memset(data, 0, intended_size);
				data += intended_size;
				page_off = 0;
				page++;
				rc = 0;
			}
		} else {
			return rc;
		}
	}

	return 0;
}

int efx_mcdi_phy_get_module_eeprom(struct efx_nic *efx,
				   struct ethtool_eeprom *ee,
				   u8 *data)
{
	int ret;

	mutex_lock(&efx->mac_lock);
	ret = efx_mcdi_phy_get_module_eeprom_locked(efx, ee, data);
	mutex_unlock(&efx->mac_lock);

	return ret;
}

static int efx_mcdi_phy_get_module_info_locked(struct efx_nic *efx,
					struct ethtool_modinfo *modinfo)
{
	int sff_8472_level;
	int diag_type;

	switch (efx_mcdi_phy_module_type(efx)) {
	case MC_CMD_MEDIA_SFP_PLUS:
		sff_8472_level = efx_mcdi_phy_sff_8472_level(efx);

		/* If we can't read the diagnostics level we have none. */
		if (sff_8472_level < 0)
			return -EOPNOTSUPP;

		/* Check if this module requires the (unsupported) address
		 * change operation
		 */
		diag_type = efx_mcdi_phy_diag_type(efx);

		if ((sff_8472_level == 0) ||
		    (diag_type & SFF_DIAG_ADDR_CHANGE)) {
			modinfo->type = ETH_MODULE_SFF_8079;
			modinfo->eeprom_len = ETH_MODULE_SFF_8079_LEN;
		} else {
			modinfo->type = ETH_MODULE_SFF_8472;
			modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
		}
		break;

	case MC_CMD_MEDIA_QSFP_PLUS:
		modinfo->type = ETH_MODULE_SFF_8436;
		modinfo->eeprom_len = ETH_MODULE_SFF_8436_LEN;
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

int efx_mcdi_phy_get_module_info(struct efx_nic *efx,
				 struct ethtool_modinfo *modinfo)
{
	int ret;

	mutex_lock(&efx->mac_lock);
	ret = efx_mcdi_phy_get_module_info_locked(efx, modinfo);
	mutex_unlock(&efx->mac_lock);

	return ret;
}

