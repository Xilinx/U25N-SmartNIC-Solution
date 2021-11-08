/****************************************************************************
 * Image upgrade IOCTLS for Xilinx U25N Accelerator Card.
 * Copyright 2021 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/uaccess.h>
#include <linux/crc32.h>
#include <linux/slab.h>
#include "imageupgrade.h"
#include <tc.h>

#include "tc.h"
#include "mcdi.h"
#include "emcdi.h"
#include "mac_loop.h"
#include "linkmode.h"
#include "ef100_rep.h"
#include "mcdi_pcol.h"
#include "efx_ioctl.h"
#include "net_driver.h"
#include "imgup_pcol.h"
#include "spawn_pcol.h"
#include "efx_devlink.h"
#include "fpgacaps.h"

/*Return Status*/
#define ERROR (1)
#define SUCCESS (0)

#define TIMEOUT_VAR		1
#define CONST			(10 * HZ)
#define TIMEOUT			(TIMEOUT_VAR * CONST)

//TODO: Merging the mcdi_pcol.h and mc_driver_pcol.h

struct file_details detail;
struct fini_details fini_det;
struct imgdata *data = NULL;

static int set_legacy_mode(struct efx_nic *efx)
{
	int rc = -1;
        uint8_t mode = 0;
        uint8_t bootstrap = 0x2; /* MC_CMD_EXTERNAL_MAE_LINK_MODE_BOOTSTRAP */
        uint8_t legacy = 0x0;    /*MC_CMD_EXTERNAL_MAE_LINK_MODE_LEGACY */
	struct efx_ef10_nic_data *nic_data = efx->nic_data;

	efx_ext_mae_get_link(efx, &mode);
        if (mode == bootstrap) {
                rc = efx_ext_mae_set_link(efx, legacy);
		if (rc != 0) {
			netif_err(efx, drv, efx->net_dev, "Error in setting the mode back to Legacy\n");
		} else {
			nic_data->mode = U25_MODE_LEGACY;
			netif_info(efx, drv, efx->net_dev, "Mode switched back to Legacy\n");
		}
        }

	return rc;
}

static int spawn_imgup_in_ps(struct efx_nic *efx)
{
	int rc = 1;

	rc = efx_emcdi_init(efx, EMCDI_TYPE_CONTROLLER);
	if (rc != 0) {
		netif_err(efx, drv, efx->net_dev, "EMCDI init failed or already done\n");
		return rc;
	}

	BUILD_BUG_ON(MC_CMD_SPAWN_IMGUP_IN_LEN != 0);
	rc = efx_emcdi_rpc(efx, MC_CMD_SPAWN_IMGUP, NULL, 0, NULL, 0, NULL, EMCDI_TYPE_CONTROLLER);
	if (rc) {
		netif_err(efx, drv, efx->net_dev, "Failed to spawn app\n");
		efx_emcdi_fini(efx, EMCDI_TYPE_CONTROLLER);
		return rc;
	}
	netif_info(efx, drv, efx->net_dev, "Application in the PS is running\n");

	return 0;
}

static int stop_imgup_in_ps(struct efx_nic *efx)
{
	int rc = 1;
	int ret = 1;

	rc = efx_emcdi_rpc(efx, MC_CMD_STOP_IMGUP, NULL, 0, NULL, 0, NULL, EMCDI_TYPE_CONTROLLER);
	if (rc) {
		netif_err(efx, drv, efx->net_dev, "Failed to stop imgup app\n");
	} else
		netif_info(efx, drv, efx->net_dev, "Application in the PS has been stopped\n");

	efx_emcdi_fini(efx, EMCDI_TYPE_CONTROLLER);

	set_legacy_mode(efx);
	ret = efx_mac_loopback(efx, false);
	if (ret)
		netif_warn(efx, drv, efx->net_dev, "Failed to set loopback mode\n");

	return rc;
}

static int spawn_flashupgrade_in_ps(struct efx_nic *efx)
{
        int rc = 1;

        rc = efx_emcdi_init(efx, EMCDI_TYPE_CONTROLLER);
        if (rc != 0) {
                netif_err(efx, drv, efx->net_dev, "EMCDI init failed or already done\n");
                return rc;
        }

        BUILD_BUG_ON(MC_CMD_SPAWN_FLASHUPGRADE_IN_LEN != 0);
        rc = efx_emcdi_rpc(efx, MC_CMD_SPAWN_FLASHUPGRADE, NULL, 0, NULL, 0, NULL, EMCDI_TYPE_CONTROLLER);
        if (rc) {
                netif_warn(efx, drv, efx->net_dev, "Failed to spawn flashupgrade app\n");
                efx_emcdi_fini(efx, EMCDI_TYPE_CONTROLLER);
                return rc;
        }
        netif_info(efx, drv, efx->net_dev, "Application in the PS is running\n");

        return 0;
}

#if 0
static int stop_flashupgrade_in_ps(struct efx_nic *efx)
{
        int rc = 1;
        int ret;

        rc = efx_emcdi_rpc(efx, MC_CMD_STOP_FLASHUPGRADE, NULL, 0, NULL, 0, NULL, EMCDI_TYPE_CONTROLLER);
        if (rc) {
                netif_err(efx, drv, efx->net_dev, "Failed to stop flashupgrade app\n");
        } else
                netif_info(efx, drv, efx->net_dev, "Application in the PS has been stopped\n");

        efx_emcdi_fini(efx, EMCDI_TYPE_CONTROLLER);

        ret = efx_mac_loopback(efx, false);
        if (ret)
                netif_warn(efx, drv, efx->net_dev, "Failed to set loopback mode\n");

        return rc;
}
#endif
static int start_apps_in_ps(struct efx_nic *efx)
{
	int rc = 1;

	rc = efx_emcdi_rpc(efx, MC_CMD_MAE_LAUNCH_APPLICATION, NULL, 0, NULL, 0, NULL, EMCDI_TYPE_CONTROLLER);
	if (rc) {
		netif_err(efx, drv, efx->net_dev, "Failed to start apps\n");
	}

	return rc;
}

static int stop_apps_in_ps(struct efx_nic *efx)
{
	int rc = 1;

	rc = efx_emcdi_rpc(efx, MC_CMD_KILL_APPLICATION, NULL, 0, NULL, 0, NULL, EMCDI_TYPE_CONTROLLER);
	if (rc) {
		netif_err(efx, drv, efx->net_dev, "Failed to stop app\n");
	} else
		netif_info(efx, drv, efx->net_dev, "Application in the PS has been stopped\n");

	return rc;
}

static int init_img_upgrade(struct efx_nic *efx, struct file_details *init_details, uint8_t type)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_IMAGE_UPGRADE_INIT_IN_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_IMAGE_UPGRADE_INIT_OUT_LEN);

        int rc = -1;
	size_t outlength = 0;

        rc = efx_emcdi_init(efx, type);
        if (rc != 0) {
                netif_err(efx, drv, efx->net_dev, "eMCDI init for image upgrade failed\n");
                return rc;
        }
        MCDI_SET_DWORD(inbuf, IMAGE_UPGRADE_INIT_IN, init_details->fsize);
        rc = efx_emcdi_rpc(efx, MC_CMD_IMAGE_UPGRADE_INIT, inbuf,
                        sizeof(inbuf), outbuf, sizeof(outbuf), &outlength, type);
        if (rc) {
                netif_err(efx, drv, efx->net_dev, "RPC for init failed\n");
                efx_emcdi_fini(efx, type);
        } else {
		if(outlength != MC_CMD_IMAGE_UPGRADE_INIT_OUT_LEN) {
                        netif_err(efx, drv, efx->net_dev, "Mismatch in outlength.\n");
                        rc = -1;
               }/* else {
                          init_details->status = MCDI_DWORD(outbuf, IMAGE_UPGRADE_INIT_STATUS_OUT);
               }*/
        }

	init_details->status = MCDI_DWORD(outbuf, IMAGE_UPGRADE_INIT_STATUS_OUT);

        return rc;
}

static int upgrade_fpga_image(struct efx_nic *efx, struct ifreq *ifr, unsigned int *status)
{
        int rc = -1;
	size_t outlength = 0;

        MCDI_DECLARE_BUF(inbuf, MC_CMD_IMAGE_UPGRADE_DATA_IN_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_IMAGE_UPGRADE_DATA_OUT_LEN);

        struct command_format *local_command = NULL;
        local_command = (struct command_format *) kzalloc(sizeof(struct command_format), GFP_KERNEL);
        if (local_command == NULL) {
                rc = -ENOMEM;
                return rc;
        }

        rc = copy_from_user(local_command, (struct command_format *)ifr->ifr_data, sizeof(struct command_format));

        local_command = (struct command_format *) krealloc(local_command, sizeof(struct command_format) + (sizeof(char) * local_command->cmd_info.image.size), GFP_KERNEL);

	if (local_command == NULL) {
                rc = -ENOMEM;
                goto fail;
        }

        rc = copy_from_user(local_command, (struct command_format *)ifr->ifr_data,
                        sizeof(struct command_format) + (sizeof(char) * local_command->cmd_info.image.size));
	if (rc) {
                netif_err(efx, drv, efx->net_dev, "Copy from user failed.\n");
                goto fail;
        }

        MCDI_SET_DWORD(inbuf, IMAGE_UPGRADE_DATA_SIZ_IN, local_command->cmd_info.image.size);
        memcpy(MCDI_PTR(inbuf, IMAGE_UPGRADE_DATA_BIN_IN), local_command->cmd_info.image.buffer, local_command->cmd_info.image.size);

        rc = efx_emcdi_rpc(efx, MC_CMD_IMAGE_UPGRADE_DATA, inbuf,
                        sizeof(inbuf), outbuf, sizeof(outbuf), &outlength, EMCDI_TYPE_IMG);
        if (rc) {
                netif_err(efx, drv, efx->net_dev, "RPC failed\n");
        } else {
		if(outlength != MC_CMD_IMAGE_UPGRADE_DATA_OUT_LEN) {
                        netif_err(efx, drv, efx->net_dev, "Mismatch in outlength.\n");
                        rc = -1;
		}/* else {
                        *status =  MCDI_DWORD(outbuf, IMAGE_UPGRADE_DATA_STATUS_OUT);
		}*/
	}

	*status = MCDI_DWORD(outbuf, IMAGE_UPGRADE_DATA_STATUS_OUT);

fail:
        kfree(local_command);
	return rc;
}

static int upgrade_flash_image(struct efx_nic *efx, struct ifreq *ifr, unsigned int *status)
{
        int rc = -1;
	size_t outlength = 0;

        MCDI_DECLARE_BUF(inbuf, MC_CMD_IMAGE_UPGRADE_DATA_IN_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_IMAGE_UPGRADE_DATA_OUT_LEN);

        struct command_format *local_command = NULL;
        local_command = (struct command_format *) kzalloc(sizeof(struct command_format), GFP_KERNEL);
        if (local_command == NULL) {
                rc = -ENOMEM;
                return rc;
        }

        rc = copy_from_user(local_command, (struct command_format *)ifr->ifr_data, sizeof(struct command_format));

        local_command = (struct command_format *) krealloc(local_command, sizeof(struct command_format) + (sizeof(char) * local_command->cmd_info.image.size), GFP_KERNEL);

	if (local_command == NULL) {
                rc = -ENOMEM;
                goto fail;
        }

        rc = copy_from_user(local_command, (struct command_format *)ifr->ifr_data,
                        sizeof(struct command_format) + (sizeof(char) * local_command->cmd_info.image.size));
	if (rc) {
                netif_err(efx, drv, efx->net_dev, "Copy from user failed.\n");
                goto fail;
        }

        MCDI_SET_DWORD(inbuf, IMAGE_UPGRADE_DATA_SIZ_IN, local_command->cmd_info.image.size);
        memcpy(MCDI_PTR(inbuf, IMAGE_UPGRADE_DATA_BIN_IN), local_command->cmd_info.image.buffer, local_command->cmd_info.image.size);

        rc = efx_emcdi_rpc(efx, MC_CMD_IMAGE_UPGRADE_DATA, inbuf,
                        sizeof(inbuf), outbuf, sizeof(outbuf), &outlength, EMCDI_TYPE_FLASH_UPGRADE);
        if (rc) {
                netif_err(efx, drv, efx->net_dev, "RPC failed\n");
        } else {
		if(outlength != MC_CMD_IMAGE_UPGRADE_DATA_OUT_LEN) {
                        netif_err(efx, drv, efx->net_dev, "Mismatch in outlength.\n");
                        rc = -1;
		}/* else {
                        *status =  MCDI_DWORD(outbuf, IMAGE_UPGRADE_DATA_STATUS_OUT);
		}*/
	}

	*status = MCDI_DWORD(outbuf, IMAGE_UPGRADE_DATA_STATUS_OUT);

fail:
        kfree(local_command);
	return rc;
}

static int fini_img_upgrade(struct efx_nic *efx, struct fini_details *fini_details, uint8_t type)
{
	int rc = 1;
	size_t outlength = 0;

	MCDI_DECLARE_BUF(inbuf, MC_CMD_IMAGE_UPGRADE_FINI_IN_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_IMAGE_UPGRADE_FINI_OUT_LEN);

        MCDI_SET_DWORD(inbuf, IMAGE_UPGRADE_FINI_CRC_IN, fini_details->crc);
        rc = efx_emcdi_rpc(efx, MC_CMD_IMAGE_UPGRADE_FINI, inbuf,
                        sizeof(inbuf), outbuf, sizeof(outbuf), &outlength, type);
        if (rc) {
                netif_err(efx, drv, efx->net_dev, "RPC for fini failed\n");
	} else {
		if(outlength != MC_CMD_IMAGE_UPGRADE_FINI_OUT_LEN) {
                        netif_err(efx, drv, efx->net_dev, "Mismatch in outlength.\n");
                        rc = -1;
                }/* else {
			fini_details->status = MCDI_DWORD(outbuf, IMAGE_UPGRADE_FINI_STATUS_OUT);
                }*/
        }
	
	fini_details->status = MCDI_DWORD(outbuf, IMAGE_UPGRADE_FINI_STATUS_OUT);
	//FIXME SSF
	if (type == EMCDI_TYPE_IMG) {
		/* Need to deinit the emcdi channel which is done below if the type is EMCDI_TYPE_IMG */
		efx_emcdi_fini(efx, type);
	} else {
		/* Do nothing */
	}

        return rc;
}

static int wait_for_link_up(struct efx_nic *efx)
{
	struct efx_link_state *link_state = &efx->link_state;
	struct efx_mcdi_iface *mcdi = efx_mcdi(efx);
	int rc = 1;

	rc = wait_event_timeout(mcdi->cmd_complete_wq, link_state->up, TIMEOUT);
	if (rc > 0)
		rc = 0;
	else if (rc == 0)
		rc = -ETIMEDOUT;

	return rc;
}

static int check_iface(struct efx_nic *efx)
{
        int rc = 1;
	uint8_t mode = 0xf;

        if (!efx_is_u25(efx))
                return -EOPNOTSUPP;

        rc = efx_ext_mae_get_link(efx, &mode);
        if (rc) {
                netif_err(efx, drv, efx->net_dev, "Error in getting the mode\n");
                return rc;
        }
        if (mode == 0x1) {
                netif_err(efx, drv, efx->net_dev, "Device is in switchdev\n");
                return -EAGAIN;
        }

        return 0;
}

static int check_u25(struct efx_nic *efx)
{
        if (!efx_is_u25(efx))
                return -EOPNOTSUPP;

        return 0;
}


static int check_link(struct efx_nic *efx)
{
	int rc = 0;
	uint8_t bootstrap = 0x2; /* MC_CMD_EXTERNAL_MAE_LINK_MODE_BOOTSTRAP */
	uint8_t mode = 0xf; /* MC_CMD_EXTERNAL_MAE_LINK_MODE_PENDING */
	struct efx_ef10_nic_data *nic_data = efx->nic_data;
	int retryCount = 3;

	/* Checking whether the link is already UP or not */
	/* If link is DOWN, it is made UP by putting the external MACs in internal loopback mode */
	if (!efx->link_state.up) {
		/* Retry mechanism is added as a workaround to solve the issue where the link is not becoming UP after the reset */
		while(retryCount-- > 0) {
			rc = efx_mac_loopback(efx, true);
			if (rc)
				return -ENOLINK;

			rc = wait_for_link_up(efx);
			if (rc) {
				/* If link is still DOWN */
				efx_mac_loopback(efx, false);
			} else {
				/* If link is UP */
				break;
			}
		}
	}

	if(rc != 0) {
		netif_warn(efx, drv, efx->net_dev,
                           "The link state is not confirmed (rc: %d)\n", rc);
		return rc;
	}

	if (!efx_is_u25(efx))
		return -EOPNOTSUPP;

	rc = efx_ext_mae_get_link(efx, &mode);
	if (rc) {
		netif_err(efx, drv, efx->net_dev, "Error in getting the mode\n");
		return rc;
	}
	if (mode == 0x0) {
		//netif_err(efx, drv, efx->net_dev, "Device is in switchdev\n");
		//return -EAGAIN;
	//}
		rc = efx_ext_mae_set_link(efx, bootstrap);
		if (rc != 0) {
			netif_err(efx, drv, efx->net_dev, "Error in setting the mode\n");
			netif_err(efx, drv, efx->net_dev, 
				"Unable to prepare card for image upgrade: Aborting");
			return rc;
		} else {
			nic_data->mode = U25_MODE_BOOTSTRAP;
			netif_info(efx, drv, efx->net_dev, "Mode switched to Bootstrap\n");
		}
	}
	return 0;
}

static int app_status_in_ps(struct efx_nic *efx)
{
	/* TODO: App status to be handled better */
	int rc;

	rc = start_apps_in_ps(efx);
	stop_imgup_in_ps(efx);

	return rc;
}

static int select_flash(struct efx_nic *efx, struct command_format *command)
{
        int rc = 0;
        struct fpgacaps fpga;

        unsigned int flash_index = command->cmd_info.flash_index;

        if(!((flash_index == 0) ||
             (flash_index == 1)))
        {
                netif_err(efx, drv, efx->net_dev, "Invalid flash index : %u.\n", flash_index);
                rc = -1;
                goto fail;
        }

        pr_info("Flash selected is %s.\n", ((flash_index == 0)?"Primary":"Secondary"));

        fpga.nvram_id = flash_index;
        if (select_fpga_nvram(efx, &fpga) != 0)
        {
                rc = -1;
                netif_info(efx, drv, efx->net_dev, "Flash selection is failed\n");
        }

fail:
        return rc;
}

static int reset_flash(struct efx_nic *efx)
{
        int rc = -1;

        if (reset_fpga(efx) == 0) {
                netif_info(efx, drv, efx->net_dev, "Resetting FPGA: Success\n");
                rc = 0;
        }

        return rc;
}

static int get_upgrade_status(struct efx_nic *efx, struct flash_upgrade_status *status, uint8_t type)
{
        MCDI_DECLARE_BUF(outbuf, MC_CMD_IMAGE_UPGRADE_GET_STATUS_OUT_LEN);
        int rc = -1;
        size_t outlength = 0;

        rc = efx_emcdi_rpc(efx, MC_CMD_IMAGE_UPGRADE_GET_STATUS, NULL,
                           0, outbuf, sizeof(outbuf), &outlength, type);
        if (rc) {
                netif_err(efx, drv, efx->net_dev, "RPC for get imgup status failed\n");
        } else {
               if(outlength != MC_CMD_IMAGE_UPGRADE_GET_STATUS_OUT_LEN) {
                        netif_err(efx, drv, efx->net_dev, "Mismatch in outlength.\n");
                        rc = -1;
               }/* else {
			status->overall_status  = MCDI_DWORD(outbuf, IMAGE_UPGRADE_GET_STATUS_OVERALL_STATUS_OUT);
			status->curr_state      = MCDI_DWORD(outbuf, IMAGE_UPGRADE_GET_STATUS_CURR_STATE_OUT);
			status->operation       = MCDI_DWORD(outbuf, IMAGE_UPGRADE_GET_STATUS_CURR_OPERATION_OUT);
			status->bytes_performed = MCDI_DWORD(outbuf, IMAGE_UPGRADE_GET_STATUS_BYTES_PERFORMED_OUT);
               }*/
        }

	status->overall_status  = MCDI_DWORD(outbuf, IMAGE_UPGRADE_GET_STATUS_OVERALL_STATUS_OUT);
	status->curr_state      = MCDI_DWORD(outbuf, IMAGE_UPGRADE_GET_STATUS_CURR_STATE_OUT);
	status->operation       = MCDI_DWORD(outbuf, IMAGE_UPGRADE_GET_STATUS_CURR_OPERATION_OUT);
	status->bytes_performed = MCDI_DWORD(outbuf, IMAGE_UPGRADE_GET_STATUS_BYTES_PERFORMED_OUT);

        return rc;
}

static int img_version_get(struct efx_nic *efx, struct command_format *cmd)
{     
        int rc = -1;
	size_t outlength = 0;

        MCDI_DECLARE_BUF(outbuf, MC_CMD_IMAGE_UPGRADE_GET_VERSION_OUT_LEN);

        rc = efx_emcdi_init(efx, EMCDI_TYPE_FLASH_UPGRADE);
        if (rc) {                     
                netif_err(efx, drv, efx->net_dev, "Emcdi init in version query failed!");
                return rc;
        }                                                      
        rc = efx_emcdi_rpc(efx, MC_CMD_IMAGE_UPGRADE_GET_VERSION, NULL, 0, outbuf,
                                sizeof(outbuf), &outlength,
                                EMCDI_TYPE_FLASH_UPGRADE);
        if (rc) {
                netif_err(efx, drv, efx->net_dev, "Version query failed with rc:%d.\n", rc);                                                          } else {
		if(outlength != MC_CMD_IMAGE_UPGRADE_GET_VERSION_OUT_LEN) {
                        netif_err(efx, drv, efx->net_dev, "Mismatch in outlength.\n");
                        rc = -1;
                }/* else {
			cmd->cmd_info.status      = MCDI_DWORD(outbuf, IMAGE_UPGRADE_GET_VERSION_STATUS_OUT);
			cmd->cmd_info.bit_version = MCDI_DWORD(outbuf, IMAGE_UPGRADE_GET_VERSION_VALUE_OUT);
			cmd->cmd_info.app_list    = MCDI_DWORD(outbuf, IMAGE_UPGRADE_GET_VERSION_APP_LIST_OUT);
			cmd->cmd_info.time_stamp  = MCDI_DWORD(outbuf, IMAGE_UPGRADE_GET_VERSION_TIME_STAMP_OUT);
                }*/
	}

	cmd->cmd_info.status      = MCDI_DWORD(outbuf, IMAGE_UPGRADE_GET_VERSION_STATUS_OUT);
	cmd->cmd_info.bit_version = MCDI_DWORD(outbuf, IMAGE_UPGRADE_GET_VERSION_VALUE_OUT);
	cmd->cmd_info.app_list    = MCDI_DWORD(outbuf, IMAGE_UPGRADE_GET_VERSION_APP_LIST_OUT);
	cmd->cmd_info.time_stamp  = MCDI_DWORD(outbuf, IMAGE_UPGRADE_GET_VERSION_TIME_STAMP_OUT);

	efx_emcdi_fini(efx, EMCDI_TYPE_FLASH_UPGRADE);
        return rc;
}

static int perform_fpga_operation(struct efx_nic *efx, struct ifreq *ifr)
{
        int rc = -1;
	int rc1 = -1;

        struct command_format *command = kmalloc(sizeof(struct command_format), GFP_KERNEL);
        if(command == NULL) {
                netif_err(efx, drv, efx->net_dev, "Failed to allocate memory for command\n");
                return rc;
        }

        rc = copy_from_user(command, (struct command_format *) ifr->ifr_data,
                            sizeof(struct command_format));
        if (rc != 0 ) {
                netif_err(efx, drv, efx->net_dev, "Copy from user failed.\n");
		kfree(command);
                return rc;
        }

	switch(command->subcommand)
        {
                case INIT:
                        rc = init_img_upgrade(efx, &(command->cmd_info.init_details), EMCDI_TYPE_IMG);
                        if (rc != 0) {
                                netif_dbg(efx, drv, efx->net_dev,
                                          "Image upgrade init failed\n");
				set_legacy_mode(efx);
                                efx_mac_loopback(efx, false);
                        }
			
			rc1 = copy_to_user((struct command_format *)(ifr->ifr_data), command, sizeof(struct command_format));
			if (rc1)
				netif_err(efx, drv, efx->net_dev, "Copy to user failed.\n");
                        break;
                case FINI:
                        rc = fini_img_upgrade(efx, &(command->cmd_info.fini_details), EMCDI_TYPE_IMG);
                        if (rc != 0) {
                                netif_dbg(efx, drv, efx->net_dev, "Image upgrade fini failed\n");
				set_legacy_mode(efx);
                                efx_mac_loopback(efx, false);
                        }

			rc1 = copy_to_user((struct command_format *)(ifr->ifr_data), command, sizeof(struct command_format));
			if (rc1)
				netif_err(efx, drv, efx->net_dev, "Copy to user failed.\n");
                        break;
                case UPGRADE:
			rc = upgrade_fpga_image(efx, ifr, &(command->cmd_info.image.status));
                        if (rc != 0) {
                                netif_dbg(efx, drv, efx->net_dev, "Error occured during transfer\n");
				set_legacy_mode(efx);
                                efx_mac_loopback(efx, false);
                        }
			
			rc1 = copy_to_user((struct command_format *)(ifr->ifr_data), command, sizeof(struct command_format));
			if (rc1)
				netif_err(efx, drv, efx->net_dev, "Copy to user failed.\n");
                        break;
		case CHECK_LINK:
			rc = check_link(efx);
			if (rc != 0) {
				netif_dbg(efx, drv, efx->net_dev, "Interface check failed\n");
				efx_mac_loopback(efx, false);
			}
			if(rc == 0) {
				rc = spawn_imgup_in_ps(efx);
				if (rc != 0) {
					netif_warn(efx, drv, efx->net_dev,
							"Did not get any response, image upgrade app in PS may not have spawned\n");
					efx_mac_loopback(efx, false);
				}
				if(rc == 0) {
					rc = stop_apps_in_ps(efx);
					if (rc != 0) {
						netif_warn(efx, drv, efx->net_dev,
								"Apps are still running\n");
						efx_mac_loopback(efx, false);
					}
				}
			}
			break;
		case APP_IN_PS:
			rc = app_status_in_ps(efx);
			break;
		default:
			netif_err(efx, drv, efx->net_dev, "Invalid Fpga operation : %u\n", command->subcommand);
			break;
        }
        kfree(command);
        return rc;
}                                    

static int perform_flash_operation(struct efx_nic *efx, struct ifreq *ifr)
{
	int rc = -1;
	int rc1 = -1;
	struct flash_upgrade_status status;
	char version[5];
	char fw_version[32] ;
	unsigned int flash_index = 0;
	struct fpgacaps fpga;

	struct command_format *command = kmalloc(sizeof(struct command_format), GFP_KERNEL);
	if(command == NULL) {
		netif_err(efx, drv, efx->net_dev, "Failed to allocate memory for command\n");
		return rc;
	}

        rc = copy_from_user(command, (struct command_format *) ifr->ifr_data,
                            sizeof(struct command_format));
        if (rc != 0 ) {
                netif_err(efx, drv, efx->net_dev, "Copy from user failed.\n");
		kfree(command);
                return rc;
        }

	memset(&status, 0, sizeof(status));

	switch(command->subcommand)
	{
		case INIT:
			rc = init_img_upgrade(efx, &(command->cmd_info.init_details), EMCDI_TYPE_FLASH_UPGRADE);
			if (rc != 0) {
				netif_dbg(efx, drv, efx->net_dev,
					  "Image upgrade init failed\n");
				set_legacy_mode(efx);
				efx_mac_loopback(efx, false);
                	}

			rc1 = copy_to_user((struct command_format *)(ifr->ifr_data), command, sizeof(struct command_format));
			if (rc1)
				netif_err(efx, drv, efx->net_dev, "Copy to user failed.\n");
			break;
		case FINI:
			rc = fini_img_upgrade(efx, &(command->cmd_info.fini_details), EMCDI_TYPE_FLASH_UPGRADE);
			if (rc != 0) {
				netif_dbg(efx, drv, efx->net_dev, "Image upgrade fini failed\n");
				set_legacy_mode(efx);
				efx_mac_loopback(efx, false);
			} 

			rc1 = copy_to_user((struct command_format *)(ifr->ifr_data), command, sizeof(struct command_format));
			if (rc1)
				netif_err(efx, drv, efx->net_dev, "Copy to user failed.\n");
			break;
		case UPGRADE:
			rc = upgrade_flash_image(efx, ifr, &(command->cmd_info.image.status));
			if (rc != 0) {
				netif_dbg(efx, drv, efx->net_dev, "Error occured during transfer\n");
				set_legacy_mode(efx);				
				efx_mac_loopback(efx, false);
			} 

			rc1 = copy_to_user((struct command_format *)(ifr->ifr_data), command, sizeof(struct command_format));
			if (rc1)
				netif_err(efx, drv, efx->net_dev, "Copy to user failed.\n");
			break;
		case SELECT:
			rc = select_flash(efx, command);
			break;
		case RESET:
			rc = reset_flash(efx);
			break;
		case GET_STATUS:
			status.curr_state = STATE_IMGUP_IN_PROGRESS;
			rc = get_upgrade_status(efx, &status, EMCDI_TYPE_FLASH_UPGRADE);
			memcpy(&(command->cmd_info.flash_status), &status, sizeof(status));
			rc1 = copy_to_user((struct command_format *)(ifr->ifr_data), command, sizeof(struct command_format));
			if (rc1 != 0) {
				netif_err(efx, drv, efx->net_dev, "Copy to user failed.\n");
			}
			if (status.curr_state != STATE_IMGUP_IN_PROGRESS) {
				efx_emcdi_fini(efx, EMCDI_TYPE_FLASH_UPGRADE);
				set_legacy_mode(efx);
				efx_mac_loopback(efx, false);
			}
			break;
		case CHECK_IFACE:
                        rc = check_iface(efx);
                        if (rc != 0)
                                netif_dbg(efx, drv, efx->net_dev, "Interface check failed\n");
			break;
		case CHECK_LINK:
			rc = check_link(efx);
			if (rc != 0) {
				netif_dbg(efx, drv, efx->net_dev, "Interface check or link check failed\n");
				efx_mac_loopback(efx, false);
			}

			if(rc == 0) {
				if (spawn_flashupgrade_in_ps(efx) != 0) {
					netif_warn(efx, drv, efx->net_dev,
							"Did not get any response, flash upgrade app in PS may not have spawned\n");
					/* Just logging is done to make it work with golden image which doesn't have controller app to spawn flashupgrade */
				}
			}
			break;
		 case FLASH_INDEX:
			rc = get_fpga_nvram(efx, &fpga);
			flash_index = fpga.nvram_id;
		        command->cmd_info.flash_index = flash_index;
			rc1 = copy_to_user((struct command_format *)(ifr->ifr_data), command, sizeof(struct command_format));
			if (rc1 != 0) {
			       netif_err(efx, drv, efx->net_dev, "Copy to user failed.\n");
			}
                        break;
		case FPGA_VERSION:
                        rc = get_fpga_vers(efx,version);
			memcpy(command->cmd_info.version, version, sizeof(version));
			rc1 = copy_to_user((struct command_format *)(ifr->ifr_data), command, sizeof(struct command_format));
			if (rc1 != 0) {
			       netif_err(efx, drv, efx->net_dev, "Copy to user failed.\n");
			}
			break;
		case FW_VERSION:
                        efx_mcdi_print_fwver(efx, fw_version, sizeof(fw_version));
			memcpy(command->cmd_info.fw_version, fw_version, sizeof(fw_version));
			rc = copy_to_user((struct command_format *)(ifr->ifr_data), command, sizeof(struct command_format));
			if (rc != 0) {
			       netif_err(efx, drv, efx->net_dev, "Copy to user failed.\n");
			}
                        break;
		case BITSTR_VERSION:
			rc = img_version_get(efx, command);
			rc1 = copy_to_user((struct command_format *)(ifr->ifr_data), command, sizeof(struct command_format));
			if (rc1)
				netif_err(efx, drv, efx->net_dev, "Copy to user failed.\n");
			set_legacy_mode(efx);
			efx_mac_loopback(efx, false);
			break;

		case CHECK_U25:
                        rc = check_u25(efx);
                        if (rc != 0)
                                netif_dbg(efx, drv, efx->net_dev, "U25 check failed\n");
                        break;

		default:
			netif_err(efx, drv, efx->net_dev, "Invalid Flash operation : %u\n", command->subcommand);
			break;
	}
	kfree(command);
	return rc;
}

int image_upgrade_ioctl_call(struct efx_nic *efx, struct ifreq *ifr, unsigned int cmd)
{
	int rc = 1;

	switch (cmd) {
	case FLASH_OPERATION:
		rc = perform_flash_operation(efx, ifr);
		if (rc != 0) {
                        netif_dbg(efx, drv, efx->net_dev, "Flash operation failed\n");
                }
                break;
	case FPGA_OPERATION:
		rc = perform_fpga_operation(efx, ifr);
                if (rc != 0) {
                        netif_dbg(efx, drv, efx->net_dev, "Fpga operation failed\n");
                }
                break;
	default:
		rc = -EINVAL;
		break;
	}
	return rc;
}
