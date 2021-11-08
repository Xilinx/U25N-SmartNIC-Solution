/****************************************************************************
 * IPSEC Offload framework for Xilinx U25N Accelerator Card.
 * Copyright 2021 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "nic.h"
#include "efx_ioctl.h"
#include "emcdi.h"
#include "mcdi_pcol_ipsec.h"
#include "ipsec.h"

#define KEY_LEN			32

struct ipsec_add_sa_dec {
	uint32_t ipsec_offload_sc;
	uint32_t ipsec_offload_dc;
	uint32_t ipsec_offload_spi;
	uint32_t ipsec_offload_iiv;
	uint8_t ipsec_offload_key [32];
};

struct ipsec_add_sa_enc {
	uint32_t ipsec_offload_sc;
	uint32_t ipsec_offload_dc;
	uint8_t ipsec_offload_protocol;
	uint32_t ipsec_offload_spi;
	uint32_t ipsec_offload_iiv;
	uint64_t ipsec_offload_eiv;
	uint8_t ipsec_offload_key [32];
	uint8_t ipsec_offload_action_flag:3;
};

struct ipsec_del_sa_enc {
	uint32_t ipsec_offload_sc;
	uint32_t ipsec_offload_dc;
	uint8_t ipsec_offload_protocol;
};

struct ipsec_del_sa_dec {
	uint32_t ipsec_offload_sc;
	uint32_t ipsec_offload_dc;
	uint32_t ipsec_offload_spi;
};

struct ipsec_query_sa {

	uint32_t ipsec_offload_spi;
	uint64_t ipsec_offload_packets;
	uint64_t ipsec_offload_bytes;
};

static int efx_ipsec_del_sa_enc(struct efx_nic *efx, struct ipsec_del_sa_enc *sa)
{
	//	struct ipsec_stat old_del_sa_enc;
	//      int ret;
	MCDI_DECLARE_BUF(inbuf,MC_CMD_IPSEC_OFFLOAD_DEL_SA_ENC);
	MCDI_SET_DWORD(inbuf, IPSEC_OFFLOAD_DEL_SA_ENC_SC, sa->ipsec_offload_sc);
	MCDI_SET_DWORD(inbuf, IPSEC_OFFLOAD_DEL_SA_ENC_DC, sa->ipsec_offload_dc);
	MCDI_SET_DWORD(inbuf, IPSEC_OFFLOAD_DEL_SA_ENC_PROTOCOL, sa->ipsec_offload_protocol);

	return efx_emcdi_rpc(efx, MC_CMD_IPSEC_DEL_SA_ENC, inbuf,
			sizeof(inbuf), NULL, 0, NULL, EMCDI_TYPE_IPSEC);
}

static int efx_ipsec_add_sa_enc(struct efx_nic *efx, struct ipsec_add_sa_enc *sa)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_IPSEC_OFFLOAD_ADD_SA_ENC);
	uint8_t tmp;
	int i;

	for (i = 0; i < (KEY_LEN / 2); i++)
	{
		tmp = sa->ipsec_offload_key[i];
		sa->ipsec_offload_key[i] = sa->ipsec_offload_key[KEY_LEN-i-1];
		sa->ipsec_offload_key[KEY_LEN-i-1] = tmp;
	}

	MCDI_SET_DWORD(inbuf, IPSEC_OFFLOAD_ADD_SA_ENC_SC, sa->ipsec_offload_sc);
	MCDI_SET_DWORD(inbuf, IPSEC_OFFLOAD_ADD_SA_ENC_DC, sa->ipsec_offload_dc);
	MCDI_SET_DWORD(inbuf, IPSEC_OFFLOAD_ADD_SA_ENC_PROTOCOL, sa->ipsec_offload_protocol);
	MCDI_SET_DWORD(inbuf, IPSEC_OFFLOAD_ADD_SA_ENC_ACTION_FLAG, sa->ipsec_offload_action_flag);
	MCDI_SET_DWORD(inbuf, IPSEC_OFFLOAD_ADD_SA_ENC_SPI, sa->ipsec_offload_spi);
	MCDI_SET_DWORD(inbuf, IPSEC_OFFLOAD_ADD_SA_ENC_IIV, sa->ipsec_offload_iiv);
	memcpy(MCDI_PTR(inbuf, IPSEC_OFFLOAD_ADD_SA_ENC_EIV), &sa->ipsec_offload_eiv,
			sizeof(sa->ipsec_offload_eiv));
	memcpy(MCDI_PTR(inbuf, IPSEC_OFFLOAD_ADD_SA_ENC_KEY), sa->ipsec_offload_key,
			sizeof(sa->ipsec_offload_key));

	return efx_emcdi_rpc(efx, MC_CMD_IPSEC_ADD_SA_ENC, inbuf,
			sizeof(inbuf), NULL, 0, NULL, EMCDI_TYPE_IPSEC);
}

static int efx_ipsec_add_sa_dec(struct efx_nic *efx, struct ipsec_add_sa_dec *sa)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_IPSEC_OFFLOAD_ADD_SA_DEC);
	uint8_t tmp;
	int i;

	for (i = 0; i < (KEY_LEN / 2); i++)
	{
		tmp = sa->ipsec_offload_key[i];
		sa->ipsec_offload_key[i] = sa->ipsec_offload_key[KEY_LEN -i - 1];
		sa->ipsec_offload_key[KEY_LEN -i -1] = tmp;
	}

	MCDI_SET_DWORD(inbuf, IPSEC_OFFLOAD_ADD_SA_DEC_SC, sa->ipsec_offload_sc);
	MCDI_SET_DWORD(inbuf, IPSEC_OFFLOAD_ADD_SA_DEC_DC, sa->ipsec_offload_dc);
	MCDI_SET_DWORD(inbuf, IPSEC_OFFLOAD_ADD_SA_DEC_SPI, sa->ipsec_offload_spi);
	MCDI_SET_DWORD(inbuf, IPSEC_OFFLOAD_ADD_SA_DEC_IIV, sa->ipsec_offload_iiv);
	memcpy(MCDI_PTR(inbuf, IPSEC_OFFLOAD_ADD_SA_DEC_KEY), 
			sa->ipsec_offload_key, sizeof(sa->ipsec_offload_key));

	return efx_emcdi_rpc(efx, MC_CMD_IPSEC_ADD_SA_DEC, inbuf,
			sizeof(inbuf), NULL, 0, NULL, EMCDI_TYPE_IPSEC);
}

static int efx_ipsec_del_sa_dec(struct efx_nic *efx, struct ipsec_del_sa_dec *sa)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_IPSEC_OFFLOAD_DEL_SA_DEC);

	MCDI_SET_DWORD(inbuf, IPSEC_OFFLOAD_DEL_SA_DEC_SC, sa->ipsec_offload_sc);
	MCDI_SET_DWORD(inbuf, IPSEC_OFFLOAD_DEL_SA_DEC_DC, sa->ipsec_offload_dc);
	MCDI_SET_DWORD(inbuf, IPSEC_OFFLOAD_DEL_SA_DEC_SPI, sa->ipsec_offload_spi);

	return efx_emcdi_rpc(efx, MC_CMD_IPSEC_DEL_SA_DEC, inbuf,
			sizeof(inbuf), NULL, 0, NULL, EMCDI_TYPE_IPSEC);
}

int efx_ipsec_ioctl(struct efx_nic *efx, struct ifreq *ifr, int cmd)
{
	struct ipsec_add_sa_dec *add_sa_dec;
	struct ipsec_add_sa_enc *add_sa_enc;
	struct ipsec_del_sa_enc *del_sa_enc;
	struct ipsec_del_sa_dec *del_sa_dec;
	struct ipsec_query_sa *query_sa;
	int rc = 0;

	switch(cmd) {
		case IPSEC_OFFLOAD_ADD_SA_DEC : 
			add_sa_dec = kmalloc(sizeof(struct ipsec_add_sa_dec), GFP_KERNEL);
			if (copy_from_user(add_sa_dec, (struct ipsec_add_sa_dec *) ifr->ifr_data,
						sizeof(struct ipsec_add_sa_dec))) {
				kfree(add_sa_dec);
				return -EFAULT;
			}

			rc = efx_ipsec_add_sa_dec(efx, add_sa_dec);
			kfree(add_sa_dec);
			break;
		case IPSEC_OFFLOAD_ADD_SA_ENC :
			add_sa_enc = kmalloc(sizeof(struct ipsec_add_sa_enc), GFP_KERNEL);
			if (copy_from_user(add_sa_enc, (struct ipsec_add_sa_enc *) ifr->ifr_data,
						sizeof(struct ipsec_add_sa_enc))) {
				kfree(add_sa_enc);
				return -EFAULT;
			}

			rc = efx_ipsec_add_sa_enc(efx, add_sa_enc);
			kfree(add_sa_enc);
			break;
		case IPSEC_OFFLOAD_DEL_SA_ENC :
			del_sa_enc = kmalloc(sizeof(struct ipsec_del_sa_enc), GFP_KERNEL);

			if (copy_from_user(del_sa_enc, (struct ipsec_del_sa_enc *) ifr->ifr_data,
						sizeof(struct ipsec_del_sa_enc))) {
				kfree(del_sa_enc);
				return -EFAULT;
			}

			rc = efx_ipsec_del_sa_enc(efx,del_sa_enc);
			kfree(del_sa_enc);
			break;
		case IPSEC_OFFLOAD_DEL_SA_DEC :
			del_sa_dec = kmalloc(sizeof(struct ipsec_del_sa_dec), GFP_KERNEL);

			if (copy_from_user(del_sa_dec, (struct ipsec_del_sa_dec *) ifr->ifr_data,
						sizeof(struct ipsec_del_sa_dec)))
			{
				kfree(del_sa_dec);
				return -EFAULT;
			}

			rc = efx_ipsec_del_sa_dec(efx, del_sa_dec);
			kfree(del_sa_dec);
			break;
		case IPSEC_OFFLOAD_QUERY_SA : 
			query_sa = kmalloc(sizeof(struct ipsec_query_sa), GFP_KERNEL);

			if (copy_from_user(query_sa, (struct ipsec_query_sa*) ifr->ifr_data,
						sizeof(struct ipsec_query_sa))) {
				kfree(query_sa);
				return -EFAULT;
			}

#if 0
			// TODO - update statistics
			query_sa->ipsec_offload_packets = 1234;
			query_sa->ipsec_offload_bytes = 1111;
#endif
			copy_to_user((struct ipsec_query_sa * )ifr->ifr_data, query_sa, 
					sizeof(struct ipsec_query_sa));
			kfree(query_sa);
			break;
		default:
			netif_err(efx, drv, efx->net_dev, "Unexpected IPSec ioctl (rc %d)\n", -EINVAL);
			return -EINVAL;
	}

	return rc;
}

static ssize_t ipsec_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct efx_nic *efx = pci_get_drvdata(to_pci_dev(dev));
	struct efx_emcdi_iface *emcdi;

	emcdi = efx_emcdi(efx, EMCDI_TYPE_IPSEC);
	return scnprintf(buf, PAGE_SIZE, "%d\n", emcdi ? emcdi->enabled : 0);
}

static ssize_t ipsec_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct efx_nic *efx = pci_get_drvdata(to_pci_dev(dev));
	int rc;

	if (!strcmp(buf, "1\n")) {
		rc = efx_emcdi_init(efx, EMCDI_TYPE_IPSEC);
		if (rc)
			netif_err(efx, drv, efx->net_dev, "IPSec init failed\n");
	} else if (!strcmp(buf, "0\n"))
		efx_emcdi_fini(efx, EMCDI_TYPE_IPSEC);
	else
		return -EINVAL;

	return count;
}

static DEVICE_ATTR_RW(ipsec_enable);

int efx_ipsec_init(struct efx_nic *efx)
{
	return device_create_file(&efx->pci_dev->dev, &dev_attr_ipsec_enable);
}

void efx_ipsec_fini(struct efx_nic *efx)
{
	device_remove_file(&efx->pci_dev->dev, &dev_attr_ipsec_enable);
}
