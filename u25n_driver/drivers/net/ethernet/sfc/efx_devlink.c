/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2019 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */
#include "net_driver.h"
#include "efx_devlink.h"
#include "nic.h"
#include "mcdi.h"
#include "mcdi_pcol.h"
#include "emcdi.h"
#include "u25_rep.h"
#include "ef10_sriov.h"
#include "tc.h"
#include "linkmode.h"
#include "logger.h"

/* These must match with enum efx_info_type. */
static const char *type_name[EFX_INFO_TYPE_MAX] = {
	[EFX_INFO_TYPE_DRIVER] = "driver",
	[EFX_INFO_TYPE_MCFW] = "fw.mc",
	[EFX_INFO_TYPE_SUCFW] = "fw.suc",
	[EFX_INFO_TYPE_NMCFW] = "fw.nmc",
	[EFX_INFO_TYPE_CMCFW] = "fw.cmc",
	[EFX_INFO_TYPE_FPGA] = "fpga",
	[EFX_INFO_TYPE_BOARD_ID] = "board.id",
	[EFX_INFO_TYPE_BOARD_REV] = "board.rev",
	[EFX_INFO_TYPE_SERIAL] = "board.sn"
};

#ifdef CONFIG_NET_DEVLINK
#define _EFX_USE_DEVLINK
#endif

#ifdef _EFX_USE_DEVLINK
#if !defined(EFX_USE_KCOMPAT) || defined(EFX_HAVE_DEVLINK)
void efx_devlink_dump_version(void *info, enum efx_info_type type,
			      const char *buf)
{
	struct devlink_info_req *req = info;

	switch(type) {
	case EFX_INFO_TYPE_BOARD_ID:
	case EFX_INFO_TYPE_BOARD_REV:
		devlink_info_version_fixed_put(req, type_name[type], buf);
		break;
	case EFX_INFO_TYPE_SERIAL:
		devlink_info_serial_number_put(req, buf);
		break;
	default:
		devlink_info_version_running_put(req, type_name[type], buf);
	}
}

static int efx_devlink_info_get(struct devlink *devlink,
				struct devlink_info_req *req,
				struct netlink_ext_ack *extack)
{
	struct efx_devlink *devlink_private = devlink_priv(devlink);
	struct efx_nic *efx = devlink_private->efx;
	int rc;

	if (efx_nic_rev(efx) == EFX_REV_EF100)
		rc = devlink_info_driver_name_put(req, "sfc_ef100");
	else
		rc = devlink_info_driver_name_put(req, "sfc");

	efx_mcdi_dump_versions(efx, req);
	return rc;
}
#endif

int efx_devlink_start_switchdev(struct efx_nic *efx)
{
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	uint8_t switchdev = 0x1;
	uint8_t bootstrap = 0x2;
	uint8_t mode = 0xf;	
	int rc, rc1,i;
	bool log_status;
	struct logger_info *logs = NULL;

	rc = efx_ext_mae_get_link(efx, &mode);
	if (rc)
		netif_err(efx, drv, efx->net_dev, "Error in getting the mode\n");
	netif_info(efx, drv, efx->net_dev, "Mode: %d\n", mode);

	if ((mode == 0x0) || (mode == 0xf)) {
		netif_info(efx, drv, efx->net_dev, "Setting into Bootstrap Mode\n");
		rc = efx_ext_mae_set_link(efx, bootstrap);
		if (rc != 0) {
			netif_err(efx, drv, efx->net_dev, "Error in setting the mode\n");
			return rc;
		}
	}

	rc = efx_emcdi_init(efx, EMCDI_TYPE_CONTROLLER);
        if (rc != 0) {
                netif_err(efx, drv, efx->net_dev, "EMCDI init for controller failed\n");
                return rc;
        }

	log_status = check_logger_is_running(efx);
        if (log_status) {
                rc = efx_emcdi_init(efx, EMCDI_TYPE_LOGGER);
                if (rc) {
                        netif_err(efx, drv, efx->net_dev, "EMCDI init for logger failed\n");
                        return rc;
                }
                logger_init(efx, efx->emcdi->logger);
                mdelay(10);
                efx_emcdi_start_request_log(efx);
        }

	rc = efx_emcdi_init(efx, EMCDI_TYPE_MAE);
	if (rc)
		return rc;

	netif_info(efx, drv, efx->net_dev, "Setting into Switchdev Mode\n");
	rc = efx_ext_mae_set_link(efx, switchdev);
	if (rc != 0) {
		netif_err(efx, drv, efx->net_dev, "Error in setting the mode\n");
		return rc;
	} else {
		netif_info(efx, drv, efx->net_dev, "Mode has been set\n");
	}

        rc = efx_init_tc(efx);
        if (rc) {
                /* Either we don't have an MAE at all (i.e. legacy v-switching),
                 * or we do but we failed to probe it.  In the latter case, we
                 * may not have set up default rules, in which case we won't be
                 * able to pass any traffic.  However, we don't fail the probe,
                 * because the user might need to use the netdevice to apply
                 * configuration changes to fix whatever's wrong with the MAE.
                 */
                netif_warn(efx, probe, efx->net_dev,
                                "Failed to probe MAE rc %d; TC offload unavailable\n",
                                rc);
//		return rc;
		goto err_logger;
        } else {
#if !defined(EFX_USE_KCOMPAT) || defined(EFX_TC_OFFLOAD)
                efx->net_dev->features |= NETIF_F_HW_TC;
                efx->fixed_features |= NETIF_F_HW_TC;
#endif
        }

#if !defined(EFX_USE_KCOMPAT) || defined(EFX_TC_OFFLOAD)
	if(efx->vf_count > 0) {
		nic_data->vf_rep = kcalloc(efx->vf_count, 
				sizeof(struct net_device *), GFP_KERNEL);
		if (!nic_data->vf_rep) {
			rc = -ENOMEM;
			goto err_vfrep_alloc;
		}

		for (i = 0; i < efx->vf_count; i++) {
			rc = efx_u25_vfrep_create(efx, i);
			if (rc)
				goto err_vfrep_create;
			if ( i == 0)
				efx_u25_vf_filter_insert(efx, i);
		}

		spin_lock_bh(&nic_data->vf_reps_lock);
		nic_data->rep_count = efx->vf_count;
		if (netif_running(efx->net_dev) &&
				(efx->state == STATE_NET_UP))
			__efx_ef10_attach_reps(efx);
		else
			__efx_ef10_detach_reps(efx);
		spin_unlock_bh(&nic_data->vf_reps_lock);
	}
#endif

	return 0;

#if !defined(EFX_USE_KCOMPAT) || defined(EFX_TC_OFFLOAD)
err_vfrep_create:
	for (; i--;) {
		if (i == 0)
			efx_filter_remove_id_safe(efx, EFX_FILTER_PRI_REQUIRED,
					nic_data->vf[i].filter_id);
		efx_u25_vfrep_destroy(efx, i);
	}

	kfree(nic_data->vf_rep);
#endif
err_vfrep_alloc:
	efx_fini_tc(efx);
err_logger:
	efx_emcdi_fini(efx, EMCDI_TYPE_MAE);
        if (log_status) {
                if(efx->emcdi->logger != NULL) {
                        logs = efx->emcdi->logger;
                        if(nic_data->base_mport == logs->pf_id) {
                                rc1 = efx_emcdi_stop_logs(efx);
                                if (rc1)
                                        netif_warn(efx, drv, efx->net_dev,
                                                        "Failed to stop logs from PS, rc=%d.\n",rc);
                                logger_deinit(efx);
                                efx_emcdi_fini(efx, EMCDI_TYPE_LOGGER);
                        }
                }
        }
	return rc;
}

int efx_devlink_stop_switchdev(struct efx_nic *efx)
{
	int i ,rc;
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	struct logger_info *logs;
#if !defined(EFX_USE_KCOMPAT) || defined(EFX_TC_OFFLOAD)
	/* We take the lock as a barrier to ensure no-one holding the lock
	 * still sees nonzero rep_count when we start destroying reps
	 */
	spin_lock_bh(&nic_data->vf_reps_lock);
	nic_data->rep_count = 0;
	spin_unlock_bh(&nic_data->vf_reps_lock);

	for (i = 0; i < efx->vf_count; i++) {
		if (i == 0)
			efx_filter_remove_id_safe(efx, EFX_FILTER_PRI_REQUIRED,
					nic_data->vf[i].filter_id);
		efx_u25_vfrep_destroy(efx, i);
	}
	kfree(nic_data->vf_rep);
#endif
	efx_fini_tc(efx);
	if(efx->emcdi->logger != NULL) {
                logs = efx->emcdi->logger;
                if(nic_data->base_mport == logs->pf_id) {
                        rc = efx_emcdi_stop_logs(efx);
                        if (rc )
                                netif_warn(efx, drv, efx->net_dev,
                                        "Failed to stop logs from PS, rc=%d.\n",rc);
                        logger_deinit(efx);
                        efx_emcdi_fini(efx, EMCDI_TYPE_LOGGER);
                }
        }
	efx_emcdi_fini(efx, EMCDI_TYPE_CONTROLLER);
	efx_emcdi_fini(efx, EMCDI_TYPE_MAE);
	return 0;
}

int efx_devlink_start_legacy(struct efx_nic *efx)
{
	uint8_t bootstrap = 0x2;
	uint8_t mode = 0xf;
	uint8_t legacy = 0x0;
	int rc = 1;

	rc = efx_ext_mae_get_link(efx, &mode);
	if (rc)
		netif_err(efx, drv, efx->net_dev, "Error in getting the mode\n");
	netif_info(efx, drv, efx->net_dev, "Mode: %d\n", mode);

	if ((mode == 0x1) || (mode == 0xf)){
		netif_info(efx, drv, efx->net_dev, "Setting into Bootstrap Mode\n");
		rc = efx_ext_mae_set_link(efx, bootstrap);
		if (rc != 0) {
			netif_err(efx, drv, efx->net_dev, "Error in setting the mode\n");
			return rc;
		}
	}

	netif_info(efx, drv, efx->net_dev, "Setting into Legacy Mode\n");
	rc = efx_ext_mae_set_link(efx, legacy);
	if (rc != 0) {
		netif_err(efx, drv, efx->net_dev, "Error in setting the mode\n");
		return rc;
	} else {
		netif_info(efx, drv, efx->net_dev, "Mode has been set\n");
	}

	return rc;
}

static int efx_devlink_stop_legacy(struct efx_nic *efx)
{
	//TODO - do any cleanups needed.
	return 0;
}

/*static*/ int efx_devlink_eswitch_mode_set(struct devlink *devlink, 
		uint16_t mode, struct netlink_ext_ack *extack)
{
	struct efx_devlink *devlink_private = devlink_priv(devlink);
	struct efx_nic *efx = devlink_private->efx;
	struct efx_ef10_nic_data *nic_data;
	int rc;

	if (efx->type->is_vf)
		return -EOPNOTSUPP;

	if (efx_nic_rev(efx) != EFX_REV_HUNT_A0)
		return -EOPNOTSUPP;

	nic_data = efx->nic_data;
	if (!nic_data->is_u25)
		return -EOPNOTSUPP;

	//pr_err("------%s-----%d----%d\n", __func__, nic_data->mode, devlink_private->eswitch_mode);
	if (devlink_private->eswitch_mode == mode)
		return 0;

	if (mode == DEVLINK_ESWITCH_MODE_SWITCHDEV) {
		rc = efx_devlink_stop_legacy(efx);
		if (rc)
			return -EAGAIN;
		rc = efx_devlink_start_switchdev(efx);
		if (rc) {
			efx_devlink_start_legacy(efx);
			return rc;
		}
		devlink_private->vf_count = efx->vf_count;
		nic_data->mode = U25_MODE_SWITCHDEV;
	} else if (mode == DEVLINK_ESWITCH_MODE_LEGACY){
		rc = efx_devlink_stop_switchdev(efx);
		if (rc)
			return -EAGAIN;
		rc = efx_devlink_start_legacy(efx);
		if (rc) {
			efx_devlink_start_switchdev(efx);
			return rc;
		}
		devlink_private->vf_count = 0;
		nic_data->mode = U25_MODE_LEGACY;
	} else
		return -EINVAL;

	devlink_private->eswitch_mode = mode;

	pr_info("eswitchmode changed to %s\n",
			mode == DEVLINK_ESWITCH_MODE_SWITCHDEV ? "switchdev" : "legacy");
	return 0;
}

static int efx_devlink_eswitch_mode_get(struct devlink *devlink, uint16_t *mode)
{
	struct efx_devlink *devlink_private = devlink_priv(devlink);
	struct efx_nic *efx = devlink_private->efx;

	if (efx->type->is_vf)
		return -EOPNOTSUPP;

	if (!efx_is_u25(efx))
		return -EOPNOTSUPP;

	*mode = devlink_private->eswitch_mode;
	return 0;
}

#if !defined(EFX_USE_KCOMPAT) || defined(EFX_HAVE_DEVLINK)
struct devlink_port *efx_get_devlink_port(struct net_device *dev)
{
	struct efx_nic *efx = efx_netdev_priv(dev);
	struct efx_devlink *devlink_private;

	if (!efx->devlink)
		return NULL;

	devlink_private = devlink_priv(efx->devlink);
	if (devlink_private)
		return &devlink_private->dl_port;
	else
		return NULL;
}
#endif

static const struct devlink_ops sfc_devlink_ops = {
	.eswitch_mode_set = efx_devlink_eswitch_mode_set,
	.eswitch_mode_get = efx_devlink_eswitch_mode_get,
#if !defined(EFX_USE_KCOMPAT) || defined(EFX_HAVE_DEVLINK)
	.info_get	= efx_devlink_info_get,
#endif
};

void efx_fini_devlink(struct efx_nic *efx)
{
	//pr_err("----%s-----\n", __func__);
	if (efx->devlink) {
		struct efx_devlink *devlink_private;

		efx_devlink_eswitch_mode_set(efx->devlink,
				DEVLINK_ESWITCH_MODE_LEGACY, NULL);
		devlink_private = devlink_priv(efx->devlink);
		devlink_port_unregister(&devlink_private->dl_port);

		devlink_unregister(efx->devlink);
		devlink_free(efx->devlink);
	}
	efx->devlink = NULL;
}

int efx_probe_devlink(struct efx_nic *efx)
{
	struct efx_devlink *devlink_private;
	uint8_t bus = efx->pci_dev->bus->number;
	int rc;

	efx->devlink = devlink_alloc(&sfc_devlink_ops,
				     sizeof(struct efx_devlink));
	if (!efx->devlink)
		return -ENOMEM;
	devlink_private = devlink_priv(efx->devlink);
	devlink_private->efx = efx;

	if (efx_is_u25(efx))
		devlink_private->eswitch_mode = DEVLINK_ESWITCH_MODE_LEGACY;

	rc = devlink_register(efx->devlink, &efx->pci_dev->dev);
	if (rc)
		goto out_free;

	devlink_port_attrs_set(&devlink_private->dl_port, DEVLINK_PORT_FLAVOUR_PHYSICAL,
			efx->port_num, false, 0, &bus, sizeof(uint8_t));
	rc = devlink_port_register(efx->devlink, &devlink_private->dl_port,
				   efx->port_num);
	if (rc)
		goto out_unreg;

	devlink_port_type_eth_set(&devlink_private->dl_port, efx->net_dev);
	return 0;

out_unreg:
	devlink_unregister(efx->devlink);
out_free:
	devlink_free(efx->devlink);
	efx->devlink = NULL;
	return rc;
}
#else
/* devlink is not available, provide the version information via a file
 * in sysfs.
 */
#include <linux/device.h>

void efx_devlink_dump_version(void *info,
			      enum efx_info_type type,
			      const char *buf_in)
{
	char *buf_out = info;
	int offset = strlen(buf_out);

	scnprintf(&buf_out[offset], PAGE_SIZE-offset, "%s: %s\n",
		  type_name[type], buf_in);
}

static ssize_t versions_show(struct device *dev,
			     struct device_attribute *attr, char *buf_out)
{
	struct efx_nic *efx = pci_get_drvdata(to_pci_dev(dev));

	if (efx_nic_rev(efx) == EFX_REV_EF100)
		sprintf(buf_out, "driver: sfc_ef100\n");
	else
		sprintf(buf_out, "driver: sfc\n");

	efx_mcdi_dump_versions(efx, buf_out);
	return strlen(buf_out);
}

static DEVICE_ATTR_RO(versions);

int efx_probe_devlink(struct efx_nic *efx)
{
	return device_create_file(&efx->pci_dev->dev, &dev_attr_versions);
}

void efx_fini_devlink(struct efx_nic *efx)
{
	device_remove_file(&efx->pci_dev->dev, &dev_attr_versions);
}

#endif	/* _EFX_USE_DEVLINK */

void efx_devlink_sriov_configure(struct efx_nic *efx)
{
#ifdef _EFX_USE_DEVLINK
	struct efx_devlink *devlink_private = devlink_priv(efx->devlink);
	struct efx_ef10_nic_data *nic_data;
	int i;

	if (efx->type->is_vf)
		return;

	if (efx_nic_rev(efx) != EFX_REV_HUNT_A0)
		return;

	nic_data = efx->nic_data;
	if (!nic_data->is_u25)
		return;

	if (devlink_private->eswitch_mode == DEVLINK_ESWITCH_MODE_SWITCHDEV) {
#if !defined(EFX_USE_KCOMPAT) || defined(EFX_TC_OFFLOAD)
		if (efx->vf_count && !devlink_private->vf_count) {
			for (i = 0; i < efx->vf_count; i++) {
				efx_u25_vfrep_create(efx, i);
				if (i == 0)
					efx_u25_vf_filter_insert(efx, i);
			}
		} else if (!efx->vf_count && devlink_private->vf_count) {
			for (i = 0; i < devlink_private->vf_count; i++) {
				if (i == 0)
					efx_filter_remove_id_safe(efx,
							EFX_FILTER_PRI_REQUIRED,
							nic_data->vf[i].filter_id);
				efx_u25_vfrep_destroy(efx, i);
			}
		}
#endif
		devlink_private->vf_count = efx->vf_count;
		nic_data->rep_count = efx->vf_count;
	}
#endif
}
