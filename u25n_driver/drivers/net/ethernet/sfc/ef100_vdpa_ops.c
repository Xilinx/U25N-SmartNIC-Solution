// SPDX-License-Identifier: GPL-2.0
/* Driver for Xilinx network controllers and boards
 * Copyright 2020 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/vdpa.h>
#include <linux/virtio_ids.h>
#include <linux/pci_ids.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <uapi/linux/virtio_config.h>
#include <uapi/linux/virtio_net.h>
#include "ef100_nic.h"
#include "io.h"
#include "ef100_vdpa.h"
#include "mcdi_vdpa.h"

#if defined(CONFIG_SFC_VDPA)
#if !defined(EFX_USE_KCOMPAT) && !defined(EFX_DISABLE_SFC_VDPA)

/* Get the queue's function-local index of the associated VI
 * virtqueue number queue 0 is reserved for MCDI
 */
#define EFX_GET_VI_INDEX(vq_num) (((vq_num) / 2) + 1)

struct feature_bit {
	u8 bit;
	char *str;
};

static const struct feature_bit feature_table[] = {
	{VIRTIO_F_NOTIFY_ON_EMPTY, "VIRTIO_F_NOTIFY_ON_EMPTY"},
	{VIRTIO_F_ANY_LAYOUT, "VIRTIO_F_ANY_LAYOUT"},
	{VIRTIO_F_VERSION_1, "VIRTIO_F_VERSION_1"},
	{VIRTIO_F_ACCESS_PLATFORM, "VIRTIO_F_ACCESS_PLATFORM"},
	{VIRTIO_F_RING_PACKED, "VIRTIO_F_RING_PACKED"},
	{VIRTIO_F_ORDER_PLATFORM, "VIRTIO_F_ORDER_PLATFORM"},
	{VIRTIO_F_IN_ORDER, "VIRTIO_F_IN_ORDER"},
	{VIRTIO_F_SR_IOV, "VIRTIO_F_SR_IOV"},
	{VIRTIO_NET_F_CSUM, "VIRTIO_NET_F_CSUM"},
	{VIRTIO_NET_F_GUEST_CSUM, "VIRTIO_NET_F_GUEST_CSUM"},
	{VIRTIO_NET_F_CTRL_GUEST_OFFLOADS, "VIRTIO_NET_F_CTRL_GUEST_OFFLOADS"},
	{VIRTIO_NET_F_MTU, "VIRTIO_NET_F_MTU"},
	{VIRTIO_NET_F_MAC, "VIRTIO_NET_F_MAC"},
	{VIRTIO_NET_F_GUEST_TSO4, "VIRTIO_NET_F_GUEST_TSO4"},
	{VIRTIO_NET_F_GUEST_TSO6, "VIRTIO_NET_F_GUEST_TSO6"},
	{VIRTIO_NET_F_GUEST_ECN, "VIRTIO_NET_F_GUEST_ECN"},
	{VIRTIO_NET_F_GUEST_UFO, "VIRTIO_NET_F_GUEST_UFO"},
	{VIRTIO_NET_F_HOST_TSO4, "VIRTIO_NET_F_HOST_TSO4"},
	{VIRTIO_NET_F_HOST_TSO6, "VIRTIO_NET_F_HOST_TSO6"},
	{VIRTIO_NET_F_HOST_ECN, "VIRTIO_NET_F_HOST_ECN"},
	{VIRTIO_NET_F_HOST_UFO, "VIRTIO_NET_F_HOST_UFO"},
	{VIRTIO_NET_F_MRG_RXBUF, "VIRTIO_NET_F_MRG_RXBUF"},
	{VIRTIO_NET_F_STATUS, "VIRTIO_NET_F_STATUS"},
	{VIRTIO_NET_F_CTRL_VQ, "VIRTIO_NET_F_CTRL_VQ"},
	{VIRTIO_NET_F_CTRL_RX, "VIRTIO_NET_F_CTRL_RX"},
	{VIRTIO_NET_F_CTRL_VLAN, "VIRTIO_NET_F_CTRL_VLAN"},
	{VIRTIO_NET_F_CTRL_RX_EXTRA, "VIRTIO_NET_F_CTRL_RX_EXTRA"},
	{VIRTIO_NET_F_GUEST_ANNOUNCE, "VIRTIO_NET_F_GUEST_ANNOUNCE"},
	{VIRTIO_NET_F_MQ, "VIRTIO_NET_F_MQ"},
	{VIRTIO_NET_F_CTRL_MAC_ADDR, "VIRTIO_NET_F_CTRL_MAC_ADDR"},
	{VIRTIO_NET_F_HASH_REPORT, "VIRTIO_NET_F_HASH_REPORT"},
	{VIRTIO_NET_F_RSS, "VIRTIO_NET_F_RSS"},
	{VIRTIO_NET_F_RSC_EXT, "VIRTIO_NET_F_RSC_EXT"},
	{VIRTIO_NET_F_STANDBY, "VIRTIO_NET_F_STANDBY"},
	{VIRTIO_NET_F_SPEED_DUPLEX, "VIRTIO_NET_F_SPEED_DUPLEX"},
	{VIRTIO_NET_F_GSO, "VIRTIO_NET_F_GSO"},
};

struct status_val {
	u8 bit;
	char *str;
};

static const struct status_val status_val_table[] = {
	{VIRTIO_CONFIG_S_ACKNOWLEDGE, "ACKNOWLEDGE"},
	{VIRTIO_CONFIG_S_DRIVER, "DRIVER"},
	{VIRTIO_CONFIG_S_FEATURES_OK, "FEATURES_OK"},
	{VIRTIO_CONFIG_S_DRIVER_OK, "DRIVER_OK"},
	{VIRTIO_CONFIG_S_FAILED, "FAILED"}
};

static struct ef100_vdpa_nic *get_vdpa_nic(struct vdpa_device *vdev)
{
	return container_of(vdev, struct ef100_vdpa_nic, vdpa_dev);
}

static void print_status_str(u8 status, struct vdpa_device *vdev)
{
	u16 table_len =  sizeof(status_val_table) / sizeof(struct status_val);
	char concat_str[] = ", ";
	char buf[100];
	u16 i = 0;

	buf[0] = '\0';
	if (status == 0) {
		dev_info(&vdev->dev, "RESET\n");
		return;
	}
	for ( ; (i < table_len) && status; i++) {
		if (status & status_val_table[i].bit) {
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
				 "%s", status_val_table[i].str);
			status &= ~status_val_table[i].bit;
			if (status > 0)
				snprintf(buf + strlen(buf),
					 sizeof(buf) - strlen(buf), "%s",
					 concat_str);
		}
	}
	dev_info(&vdev->dev, "%s\n", buf);
	if (status)
		dev_info(&vdev->dev, "Unknown status:0x%x\n", status);
}

static void print_features_str(u64 features, struct vdpa_device *vdev)
{
	int table_len = sizeof(feature_table) / sizeof(struct feature_bit);
	int i = 0;

	for (; (i < table_len) && features; i++) {
		if (features & (1ULL << feature_table[i].bit)) {
			dev_info(&vdev->dev, "%s: %s\n", __func__,
				 feature_table[i].str);
			features &= ~(1ULL << feature_table[i].bit);
		}
	}
	if (features) {
		dev_info(&vdev->dev,
			 "%s: Unknown Features:0x%llx\n",
			 __func__, features);
	}
}

static char *get_vdpa_state_str(enum ef100_vdpa_nic_state state)
{
	switch (state) {
	case EF100_VDPA_STATE_INITIALIZED:
		return "INITIALIZED";
	case EF100_VDPA_STATE_NEGOTIATED:
		return "NEGOTIATED";
	case EF100_VDPA_STATE_STARTED:
		return "STARTED";
	default:
		return "UNKNOWN";
	}
}

static irqreturn_t vring_intr_handler(int irq, void *arg)
{
	struct ef100_vdpa_vring_info *vring = arg;

	if (vring->cb.callback)
		return vring->cb.callback(vring->cb.private);

	return IRQ_NONE;
}

static void print_vring_state(u16 state, struct vdpa_device *vdev)
{
	dev_info(&vdev->dev, "%s: Vring state:\n", __func__);
	dev_info(&vdev->dev, "%s: Address Configured:%s\n", __func__,
		 (state & EF100_VRING_ADDRESS_CONFIGURED) ? "true" : "false");
	dev_info(&vdev->dev, "%s: Size Configured:%s\n", __func__,
		 (state & EF100_VRING_SIZE_CONFIGURED) ? "true" : "false");
	dev_info(&vdev->dev, "%s: Ready Configured:%s\n", __func__,
		 (state & EF100_VRING_READY_CONFIGURED) ? "true" : "false");
}

int ef100_vdpa_irq_vectors_alloc(struct pci_dev *pci_dev, u16 min, u16 max)
{
	int rc = 0;

	rc = pci_alloc_irq_vectors(pci_dev, min, max, PCI_IRQ_MSIX);
	if (rc < 0)
		pci_err(pci_dev,
			"Failed to alloc min:%d max:%d IRQ vectors Err:%d\n",
			min, max, rc);
	return rc;
}

void ef100_vdpa_irq_vectors_free(void *data)
{
	pci_free_irq_vectors(data);
}

static int irq_vring_init(struct ef100_vdpa_nic *vdpa_nic, u16 idx, u32 vector)
{
	struct ef100_vdpa_vring_info *vring = &vdpa_nic->vring[idx];
	struct pci_dev *pci_dev = vdpa_nic->efx->pci_dev;
	int rc = 0;
	int irq;

	snprintf(vring->msix_name, 256, "x_vdpa[%s]-%d\n",
		 pci_name(pci_dev), idx);
	irq = pci_irq_vector(pci_dev, vector);
	rc = devm_request_irq(&pci_dev->dev, irq, vring_intr_handler, 0,
			      vring->msix_name, vring);
	if (rc)
		pci_err(pci_dev,
			"Failed to request irq for vring %d, vector %u\n", idx, vector);
	else
		vring->irq = irq;

	return rc;
}

static void irq_vring_fini(struct ef100_vdpa_nic *vdpa_nic, u16 idx)
{
	struct ef100_vdpa_vring_info *vring = &vdpa_nic->vring[idx];
	struct pci_dev *pci_dev = vdpa_nic->efx->pci_dev;

	devm_free_irq(&pci_dev->dev, vring->irq, vring);
	vring->irq = -EINVAL;
}

static bool can_create_vring(struct ef100_vdpa_nic *vdpa_nic, u16 idx)
{
	if (vdpa_nic->vring[idx].vring_state & EF100_VRING_CONFIGURED &&
	    vdpa_nic->status & VIRTIO_CONFIG_S_DRIVER_OK &&
	    !vdpa_nic->vring[idx].vring_created) {
#ifdef EFX_NOT_UPSTREAM
		dev_info(&vdpa_nic->vdpa_dev.dev,
			 "%s: vring to be created for Index:%u\n", __func__,
			 idx);
#endif
		return true;
	}
#ifdef EFX_NOT_UPSTREAM
	dev_info(&vdpa_nic->vdpa_dev.dev,
		 "%s: vring cannot be created for Index:%u\n", __func__,
		 idx);
	print_vring_state(vdpa_nic->vring[idx].vring_state,
			  &vdpa_nic->vdpa_dev);
	dev_info(&vdpa_nic->vdpa_dev.dev, "%s: Vring  status:\n",
		 __func__);
	print_status_str(vdpa_nic->status, &vdpa_nic->vdpa_dev);
	dev_info(&vdpa_nic->vdpa_dev.dev,
		 "%s: Vring created:%u\n", __func__,
		 vdpa_nic->vring[idx].vring_created);
#endif
	return false;
}

static int create_vring_ctx(struct ef100_vdpa_nic *vdpa_nic, u16 idx)
{
	struct efx_vring_ctx *vring_ctx;
	u32 vi_index;
	int rc = 0;

	if (!vdpa_nic->efx) {
		dev_err(&vdpa_nic->vdpa_dev.dev,
			"%s: Invalid efx for idx:%u\n", __func__, idx);
		return -EINVAL;
	}
	if (idx % 2) /* Even VQ for RX and odd for TX */
		vdpa_nic->vring[idx].vring_type = EF100_VDPA_VQ_TYPE_NET_TXQ;
	else
		vdpa_nic->vring[idx].vring_type = EF100_VDPA_VQ_TYPE_NET_RXQ;
	vi_index = EFX_GET_VI_INDEX(idx);
	vring_ctx = efx_vdpa_vring_init(vdpa_nic->efx, vi_index,
					vdpa_nic->vring[idx].vring_type);
	if (IS_ERR(vring_ctx)) {
		rc = PTR_ERR(vring_ctx);
		return rc;
	}
	vdpa_nic->vring[idx].vring_ctx = vring_ctx;
	return 0;
}

static void delete_vring_ctx(struct ef100_vdpa_nic *vdpa_nic, u16 idx)
{
	efx_vdpa_vring_fini(vdpa_nic->vring[idx].vring_ctx);
	vdpa_nic->vring[idx].vring_ctx = NULL;
}

static int delete_vring(struct ef100_vdpa_nic *vdpa_nic, u16 idx)
{
	struct efx_vring_dyn_cfg vring_dyn_cfg;
	int rc = 0;

#ifdef EFX_NOT_UPSTREAM
	dev_info(&vdpa_nic->vdpa_dev.dev,
		 "%s: Called for %u\n", __func__, idx);
#endif
	rc = efx_vdpa_vring_destroy(vdpa_nic->vring[idx].vring_ctx,
				    &vring_dyn_cfg);
	if (rc != 0) {
		dev_err(&vdpa_nic->vdpa_dev.dev,
			"%s: Queue delete failed index:%u Err:%d\n",
			__func__, idx, rc);
		return rc;
	}
	vdpa_nic->vring[idx].last_avail_idx = vring_dyn_cfg.avail_idx;
	vdpa_nic->vring[idx].last_used_idx = vring_dyn_cfg.used_idx;
	vdpa_nic->vring[idx].vring_created = false;

	irq_vring_fini(vdpa_nic, idx);

	if (vdpa_nic->vring[idx].vring_ctx)
		delete_vring_ctx(vdpa_nic, idx);

	if ((idx == 0) && (vdpa_nic->filter_cnt != 0)) {
		rc = ef100_vdpa_filter_remove(vdpa_nic);
		if (rc < 0) {
			dev_err(&vdpa_nic->vdpa_dev.dev,
				"%s: vdpa remove filter failed, err:%d\n",
				__func__, rc);
		}
	}

	return rc;
}

static int create_vring(struct ef100_vdpa_nic *vdpa_nic, u16 idx)
{
	struct efx_vring_dyn_cfg vring_dyn_cfg;
	struct efx_vring_cfg vring_cfg;
	u32 vector;
	u32 offset;
	int rc = 0;

	if (!vdpa_nic->vring[idx].vring_ctx) {
		rc = create_vring_ctx(vdpa_nic, idx);
		if (rc != 0) {
			dev_err(&vdpa_nic->vdpa_dev.dev,
				"%s: Queue Context failed index:%u Err:%d\n",
				__func__, idx, rc);
			return rc;
		}
	}
#ifdef EFX_NOT_UPSTREAM
	dev_info(&vdpa_nic->vdpa_dev.dev,
		 "%s: Called for %u\n", __func__, idx);
#endif

	vector = idx + EF100_VDPA_VRING_VECTOR_BASE;
	rc = irq_vring_init(vdpa_nic, idx, vector);
	if (rc != 0) {
		dev_err(&vdpa_nic->vdpa_dev.dev,
			"%s: irq_vring_init failed. index:%u Err:%d\n",
			__func__, idx, rc);
		delete_vring_ctx(vdpa_nic, idx);
		return rc;
	}
	vring_cfg.desc = vdpa_nic->vring[idx].desc;
	vring_cfg.avail = vdpa_nic->vring[idx].avail;
	vring_cfg.used = vdpa_nic->vring[idx].used;
	vring_cfg.size = vdpa_nic->vring[idx].size;
	vring_cfg.features = vdpa_nic->features;
	vring_cfg.use_pasid = false;
	vring_cfg.pasid = 0;
	vring_cfg.msix_vector = vector;
	vring_dyn_cfg.avail_idx = vdpa_nic->vring[idx].last_avail_idx;
	vring_dyn_cfg.used_idx = vdpa_nic->vring[idx].last_used_idx;

	rc = efx_vdpa_vring_create(vdpa_nic->vring[idx].vring_ctx, &vring_cfg,
				   &vring_dyn_cfg);
	if (rc != 0) {
		dev_err(&vdpa_nic->vdpa_dev.dev,
			"%s: Queue create failed index:%u Err:%d\n",
			__func__, idx, rc);
		delete_vring_ctx(vdpa_nic, idx);
		return rc;
	}
	rc = efx_vdpa_get_doorbell_offset(vdpa_nic->vring[idx].vring_ctx,
					  &offset);
	if (rc != 0) {
		dev_err(&vdpa_nic->vdpa_dev.dev,
			"%s: Get Doorbell offset failed for index:%u Err:%d\n",
			__func__, idx, rc);
		goto fail;
	}
	vdpa_nic->vring[idx].vring_created = true;
	vdpa_nic->vring[idx].doorbell_offset = offset;

	/* Configure filters on rxq 0 */
	if ((idx == 0) && vdpa_nic->mac_configured &&
	    (vdpa_nic->filter_cnt == 0)) {
		rc = ef100_vdpa_filter_configure(vdpa_nic);
		if (rc < 0) {
			dev_err(&vdpa_nic->vdpa_dev.dev,
				"%s: vdpa configure filter failed, err:%d\n",
				__func__, rc);
			goto fail;
		}
	}

	return rc;
fail:
	delete_vring(vdpa_nic, idx);
	return rc;
}

static void reset_vring(struct ef100_vdpa_nic *vdpa_nic, u16 idx)
{
	int rc = 0;

	if (vdpa_nic->vring[idx].vring_created) {
		rc = delete_vring(vdpa_nic, idx);
		if (rc != 0) {
			dev_err(&vdpa_nic->vdpa_dev.dev,
				"%s:Queue deletion failed index:%u err:%d\n",
				__func__, idx, rc);
		}
	}
	memset((void *)&vdpa_nic->vring[idx], 0,
	       sizeof(vdpa_nic->vring[idx]));
	vdpa_nic->vring[idx].vring_type = EF100_VDPA_VQ_NTYPES;
}

static void reset_vdpa_device(struct ef100_vdpa_nic *vdpa_nic)
{
	int i;

	vdpa_nic->vdpa_state = EF100_VDPA_STATE_INITIALIZED;
	vdpa_nic->status = 0;
	vdpa_nic->features = 0;
	for (i = 0; i < (vdpa_nic->max_queue_pairs * 2); i++)
		reset_vring(vdpa_nic, i);
}

static int start_vdpa_device(struct ef100_vdpa_nic *vdpa_nic)
{
	int rc = 0;
	int i, j;

	for (i = 0; i < (vdpa_nic->max_queue_pairs * 2); i++) {
		if (can_create_vring(vdpa_nic, i)) {
			rc = create_vring(vdpa_nic, i);
			if (rc)
				goto clear_vring;
		}
	}
	vdpa_nic->vdpa_state = EF100_VDPA_STATE_STARTED;
	return rc;

clear_vring:
	for (j = 0; j < i; j++)
		if (vdpa_nic->vring[j].vring_created)
			delete_vring(vdpa_nic, j);
	return rc;
}

static int ef100_vdpa_set_vq_address(struct vdpa_device *vdev,
				     u16 idx, u64 desc_area, u64 driver_area,
				     u64 device_area)
{
	struct ef100_vdpa_nic *vdpa_nic = NULL;
	int rc = 0;

	vdpa_nic = get_vdpa_nic(vdev);
	if (idx >= (vdpa_nic->max_queue_pairs * 2)) {
		dev_err(&vdev->dev, "%s: Invalid queue Id %u\n", __func__, idx);
		return -EINVAL;
	}
#ifdef EFX_NOT_UPSTREAM
	dev_info(&vdev->dev, "%s: Invoked for index %u\n", __func__, idx);
#endif
	vdpa_nic->vring[idx].desc = desc_area;
	vdpa_nic->vring[idx].avail = driver_area;
	vdpa_nic->vring[idx].used = device_area;
	vdpa_nic->vring[idx].vring_state |= EF100_VRING_ADDRESS_CONFIGURED;
	return rc;
}

static void ef100_vdpa_set_vq_num(struct vdpa_device *vdev, u16 idx, u32 num)
{
	struct ef100_vdpa_nic *vdpa_nic;

	vdpa_nic = get_vdpa_nic(vdev);
	if (idx >= (vdpa_nic->max_queue_pairs * 2)) {
		dev_err(&vdev->dev, "%s: Invalid queue Id %u\n", __func__, idx);
		return;
	}
#ifdef EFX_NOT_UPSTREAM
	dev_info(&vdev->dev, "%s: Invoked for index:%u size:%u\n", __func__,
		 idx, num);
#endif
	if (num == 0 || ((num & (num - 1)) != 0)) {
		dev_err(&vdev->dev, "%s: Index:%u size:%u not power of 2\n",
			__func__, idx, num);
		return;
	}
	if (num > EF100_VDPA_VQ_NUM_MAX_SIZE) {
		dev_err(&vdev->dev, "%s: Index:%u size:%u more than max:%u\n",
			__func__, idx, num, EF100_VDPA_VQ_NUM_MAX_SIZE);
		return;
	}
	vdpa_nic->vring[idx].size  = num;
	vdpa_nic->vring[idx].vring_state |= EF100_VRING_SIZE_CONFIGURED;
}

static void ef100_vdpa_kick_vq(struct vdpa_device *vdev, u16 idx)
{
	struct ef100_vdpa_nic *vdpa_nic;
	struct efx_nic *efx;
	u32 idx_val;

	vdpa_nic = get_vdpa_nic(vdev);
	if (idx >= (vdpa_nic->max_queue_pairs * 2)) {
		dev_err(&vdev->dev, "%s: Invalid queue Id %u\n", __func__, idx);
		return;
	}
	efx = vdpa_nic->vring[idx].vring_ctx->nic;
	if (!efx) {
		dev_err(&vdev->dev, "%s: Invalid efx pointer %u\n", __func__,
			idx);
		return;
	}
	idx_val = idx;
#if defined(VERBOSE_DEBUG)
	dev_info(&vdev->dev, "%s: Writing value:%u in offset register:%u\n",
		 __func__, idx_val,
		 vdpa_nic->vring[idx].doorbell_offset);
#endif
	_efx_writed(efx, cpu_to_le32(idx_val),
		    vdpa_nic->vring[idx].doorbell_offset);
}

static void ef100_vdpa_set_vq_cb(struct vdpa_device *vdev, u16 idx,
				 struct vdpa_callback *cb)
{
	struct ef100_vdpa_nic *vdpa_nic;

	vdpa_nic = get_vdpa_nic(vdev);
	if (idx >= (vdpa_nic->max_queue_pairs * 2))
		dev_err(&vdev->dev, "%s: Invalid queue Id %u\n", __func__, idx);

	if (cb)
		vdpa_nic->vring[idx].cb = *cb;

#ifdef EFX_NOT_UPSTREAM
	dev_info(&vdev->dev, "%s: Setting vq callback for vring %u\n",
		 __func__, idx);
#endif
}

static void ef100_vdpa_set_vq_ready(struct vdpa_device *vdev, u16 idx,
				    bool ready)
{
	struct ef100_vdpa_nic *vdpa_nic;
	int rc;

	vdpa_nic = get_vdpa_nic(vdev);
	if (idx >= (vdpa_nic->max_queue_pairs * 2)) {
		dev_err(&vdev->dev, "%s: Invalid queue Id %u\n", __func__, idx);
		return;
	}
#ifdef EFX_NOT_UPSTREAM
	dev_info(&vdev->dev, "%s: Queue Id: %u Ready :%u\n", __func__,
		 idx, ready);
#endif
	if (ready) {
		vdpa_nic->vring[idx].vring_state |=
					EF100_VRING_READY_CONFIGURED;
		if (vdpa_nic->vdpa_state == EF100_VDPA_STATE_STARTED &&
		    can_create_vring(vdpa_nic, idx)) {
			rc = create_vring(vdpa_nic, idx);
			if (rc)
				/* Rollback ready configuration
				 * So that the above layer driver
				 * can make another attempt to set ready
				 */
				vdpa_nic->vring[idx].vring_state &=
					~EF100_VRING_READY_CONFIGURED;
		}
	} else {
		vdpa_nic->vring[idx].vring_state &=
					~EF100_VRING_READY_CONFIGURED;
		if (vdpa_nic->vring[idx].vring_created)
			delete_vring(vdpa_nic, idx);
	}
}

static bool ef100_vdpa_get_vq_ready(struct vdpa_device *vdev, u16 idx)
{
	struct ef100_vdpa_nic *vdpa_nic;

	vdpa_nic = get_vdpa_nic(vdev);
	if (idx >= (vdpa_nic->max_queue_pairs * 2)) {
		dev_err(&vdev->dev, "%s: Invalid queue Id %u\n", __func__, idx);
		return false;
	}
#ifdef EFX_NOT_UPSTREAM
	dev_info(&vdev->dev, "%s: Index:%u Value returned: %u\n", __func__,
		 idx, vdpa_nic->vring[idx].vring_state &
		 EF100_VRING_READY_CONFIGURED);
#endif
	return vdpa_nic->vring[idx].vring_state & EF100_VRING_READY_CONFIGURED;
}

static int ef100_vdpa_set_vq_state(struct vdpa_device *vdev, u16 idx,
				   const struct vdpa_vq_state *state)
{
	struct ef100_vdpa_nic *vdpa_nic;

	vdpa_nic = get_vdpa_nic(vdev);
	if (idx >= (vdpa_nic->max_queue_pairs * 2)) {
		dev_err(&vdev->dev, "%s: Invalid queue Id %u\n", __func__, idx);
		return -EINVAL;
	}
#ifdef EFX_NOT_UPSTREAM
	dev_info(&vdev->dev, "%s: Queue:%u State:0x%x", __func__, idx,
		 state->avail_index);
#endif
	vdpa_nic->vring[idx].last_avail_idx = state->avail_index;
	return 0;
}

static int ef100_vdpa_get_vq_state(struct vdpa_device *vdev, u16 idx,
				   struct vdpa_vq_state *state)
{
	struct ef100_vdpa_nic *vdpa_nic;

	vdpa_nic = get_vdpa_nic(vdev);
	if (idx >= (vdpa_nic->max_queue_pairs * 2)) {
		dev_err(&vdev->dev, "%s: Invalid queue Id %u\n", __func__, idx);
		return -EINVAL;
	}
#ifdef EFX_NOT_UPSTREAM
	dev_info(&vdev->dev, "%s: Queue:%u State:0x%x", __func__, idx,
		 vdpa_nic->vring[idx].last_avail_idx);
#endif
	state->avail_index = (u16)vdpa_nic->vring[idx].last_avail_idx;
	return 0;
}

static struct vdpa_notification_area
		ef100_vdpa_get_vq_notification(struct vdpa_device *vdev, u16 idx)
{
	struct vdpa_notification_area notify_area = {0, 0};
	struct ef100_vdpa_nic *vdpa_nic;
	struct efx_nic *efx;

	vdpa_nic = get_vdpa_nic(vdev);
	if (idx >= (vdpa_nic->max_queue_pairs * 2)) {
		dev_err(&vdev->dev, "%s: Invalid queue Id %u\n", __func__, idx);
		return notify_area;
	}
	if (!vdpa_nic->vring[idx].vring_created) {
		dev_err(&vdev->dev, "%s: Queue Id %u not created\n", __func__, idx);
		return notify_area;
	}
	efx = vdpa_nic->efx;
	notify_area.addr = (resource_size_t)efx_mem(efx,
					vdpa_nic->vring[idx].doorbell_offset);

	/* VDPA doorbells are at a stride of VI/2
	 * One VI stride is shared by both rx & tx doorbells
	 */
	notify_area.size = efx->vi_stride / 2;

#ifdef EFX_NOT_UPSTREAM
	dev_info(&vdev->dev, "%s: Queue Id:%u Notification addr:0x%x size:0x%x",
		 __func__, idx, (u32)notify_area.addr, (u32)notify_area.size);
#endif

	return notify_area;
}

static int ef100_get_vq_irq(struct vdpa_device *vdev, u16 idx)
{
	struct ef100_vdpa_nic *vdpa_nic = get_vdpa_nic(vdev);

	if (idx >= (vdpa_nic->max_queue_pairs * 2)) {
		dev_err(&vdev->dev, "%s: Invalid queue Id %u\n", __func__, idx);
		return -EINVAL;
	}
	dev_info(&vdev->dev, "%s: Queue Id %u, irq: %d\n",
		 __func__, idx, vdpa_nic->vring[idx].irq);

	return vdpa_nic->vring[idx].irq;
}

static u32 ef100_vdpa_get_vq_align(struct vdpa_device *vdev)
{
#ifdef EFX_NOT_UPSTREAM
	dev_info(&vdev->dev, "%s: Returning value:%u\n", __func__,
		 EF100_VDPA_VQ_ALIGN);
#endif
	return EF100_VDPA_VQ_ALIGN;
}

static u64 ef100_vdpa_get_features(struct vdpa_device *vdev)
{
	struct ef100_vdpa_nic *vdpa_nic;
	u64 features = 0;
	int rc = 0;

	vdpa_nic = get_vdpa_nic(vdev);
	rc = efx_vdpa_get_features(vdpa_nic->efx,
				   EF100_VDPA_DEVICE_TYPE_NET, &features);
	if (rc != 0) {
		dev_err(&vdev->dev, "%s: MCDI get features error:%d\n",
			__func__, rc);
		/* Returning 0 as value of features will lead to failure
		 * of feature negotiation.
		 */
		return 0;
	}
#ifdef EFX_NOT_UPSTREAM
	dev_info(&vdev->dev, "%s: Features returned:\n", __func__);
	print_features_str(features, vdev);
#endif
	return features;
}

static int ef100_vdpa_set_features(struct vdpa_device *vdev, u64 features)
{
	struct ef100_vdpa_nic *vdpa_nic;
	int rc = 0;

#ifdef EFX_NOT_UPSTREAM
	dev_info(&vdev->dev, "%s: Features received:\n", __func__);
	print_features_str(features, vdev);
#endif
	vdpa_nic = get_vdpa_nic(vdev);
	if (vdpa_nic->vdpa_state != EF100_VDPA_STATE_INITIALIZED) {
		dev_err(&vdev->dev, "%s: Invalid current state %s\n",
			__func__,
			get_vdpa_state_str(vdpa_nic->vdpa_state));
		return -EINVAL;
	}
	rc = efx_vdpa_verify_features(vdpa_nic->efx,
				      EF100_VDPA_DEVICE_TYPE_NET, features);

	if (rc != 0) {
		dev_err(&vdev->dev, "%s: MCDI verify features error:%d\n",
			__func__, rc);
		return rc;
	}

	vdpa_nic->features = features;
	return rc;
}

static void ef100_vdpa_set_config_cb(struct vdpa_device *vdev,
				     struct vdpa_callback *cb)
{
	struct ef100_vdpa_nic *vdpa_nic = get_vdpa_nic(vdev);

	if (cb)
		vdpa_nic->cfg_cb = *cb;

#ifdef EFX_NOT_UPSTREAM
	dev_info(&vdev->dev, "%s: Setting config callback\n", __func__);
#endif
}

static u16 ef100_vdpa_get_vq_num_max(struct vdpa_device *vdev)
{
#ifdef EFX_NOT_UPSTREAM
	dev_info(&vdev->dev, "%s: Returning value:%u\n", __func__,
		 EF100_VDPA_VQ_NUM_MAX_SIZE);
#endif
	return EF100_VDPA_VQ_NUM_MAX_SIZE;
}

static u32 ef100_vdpa_get_device_id(struct vdpa_device *vdev)
{
#ifdef EFX_NOT_UPSTREAM
	dev_info(&vdev->dev, "%s: Returning value:%u\n", __func__,
		 EF100_VDPA_VIRTIO_NET_DEVICE_ID);
#endif
	return EF100_VDPA_VIRTIO_NET_DEVICE_ID;
}

static u32 ef100_vdpa_get_vendor_id(struct vdpa_device *vdev)
{
#ifdef EFX_NOT_UPSTREAM
	dev_info(&vdev->dev, "%s: Returning value:0x%x\n", __func__,
		 EF100_VDPA_XNX_VENDOR_ID);
#endif
	return EF100_VDPA_XNX_VENDOR_ID;
}

static u8 ef100_vdpa_get_status(struct vdpa_device *vdev)
{
	struct ef100_vdpa_nic *vdpa_nic = get_vdpa_nic(vdev);

#ifdef EFX_NOT_UPSTREAM
		dev_info(&vdev->dev, "%s: Returning current status bit(s):\n",
			 __func__);
		print_status_str(vdpa_nic->status, vdev);
#endif
	return vdpa_nic->status;
}

static void ef100_vdpa_set_status(struct vdpa_device *vdev, u8 status)
{
	struct ef100_vdpa_nic *vdpa_nic = get_vdpa_nic(vdev);
	u8 new_status;
	int rc = 0;

	if (!status) {
		dev_info(&vdev->dev,
			 "%s: Status received is 0. Device reset being done\n",
			 __func__);
		reset_vdpa_device(vdpa_nic);
		return;
	}
	new_status = status & ~vdpa_nic->status;
	if (new_status == 0) {
		dev_info(&vdev->dev,
			 "%s: New status equal/subset of existing status:\n",
			 __func__);
		dev_info(&vdev->dev, "%s: New status bits:\n", __func__);
		print_status_str(status, vdev);
		dev_info(&vdev->dev, "%s: Existing status bits:\n", __func__);
		print_status_str(vdpa_nic->status, vdev);
		return;
	}
	if (new_status & VIRTIO_CONFIG_S_FAILED) {
		reset_vdpa_device(vdpa_nic);
		return;
	}
#ifdef EFX_NOT_UPSTREAM
	dev_info(&vdev->dev, "%s: New status:\n", __func__);
	print_status_str(new_status, vdev);
#endif
	while (new_status) {
		if (new_status & VIRTIO_CONFIG_S_ACKNOWLEDGE &&
		    vdpa_nic->vdpa_state == EF100_VDPA_STATE_INITIALIZED) {
			vdpa_nic->status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
			new_status = new_status & ~VIRTIO_CONFIG_S_ACKNOWLEDGE;
		} else if (new_status & VIRTIO_CONFIG_S_DRIVER &&
			   vdpa_nic->vdpa_state ==
					EF100_VDPA_STATE_INITIALIZED) {
			vdpa_nic->status |= VIRTIO_CONFIG_S_DRIVER;
			new_status = new_status & ~VIRTIO_CONFIG_S_DRIVER;
		} else if (new_status & VIRTIO_CONFIG_S_FEATURES_OK &&
			   vdpa_nic->vdpa_state ==
						EF100_VDPA_STATE_INITIALIZED) {
			vdpa_nic->status |= VIRTIO_CONFIG_S_FEATURES_OK;
			vdpa_nic->vdpa_state = EF100_VDPA_STATE_NEGOTIATED;
			new_status = new_status & ~VIRTIO_CONFIG_S_FEATURES_OK;
		} else if (new_status & VIRTIO_CONFIG_S_DRIVER_OK &&
			   vdpa_nic->vdpa_state ==
					EF100_VDPA_STATE_NEGOTIATED) {
			vdpa_nic->status |= VIRTIO_CONFIG_S_DRIVER_OK;
			rc = start_vdpa_device(vdpa_nic);
			if (rc) {
				dev_err(&vdpa_nic->vdpa_dev.dev,
					"%s: vDPA device failed:%d\n",
					__func__, rc);
				vdpa_nic->status &=
					~VIRTIO_CONFIG_S_DRIVER_OK;
				return;
			}
			new_status = new_status & ~VIRTIO_CONFIG_S_DRIVER_OK;
		} else {
			dev_warn(&vdev->dev, "%s: Mismatch Status & State\n",
				 __func__);
			dev_warn(&vdev->dev, "%s: New status Bits:\n", __func__);
			print_status_str(new_status, &vdpa_nic->vdpa_dev);
			dev_warn(&vdev->dev, "%s: Current vDPA State: %s\n",
				 __func__,
				 get_vdpa_state_str(vdpa_nic->vdpa_state));
			break;
		}
	}
}

static void ef100_vdpa_get_config(struct vdpa_device *vdev, unsigned int offset,
				  void *buf, unsigned int len)
{
	struct ef100_vdpa_nic *vdpa_nic = get_vdpa_nic(vdev);

#ifdef EFX_NOT_UPSTREAM
	dev_info(&vdev->dev, "%s: offset:%u len:%u\n", __func__, offset, len);
#endif
	if ((offset + len) > sizeof(struct virtio_net_config)) {
		dev_err(&vdev->dev,
			"%s: Offset + len exceeds config size\n", __func__);
		return;
	}
	memcpy(buf, &vdpa_nic->net_config + offset, len);
}

static void ef100_vdpa_set_config(struct vdpa_device *vdev, unsigned int offset,
				  const void *buf, unsigned int len)
{
	dev_info(&vdev->dev, "%s: This callback is not supported\n", __func__);
}

static void ef100_vdpa_free(struct vdpa_device *vdev)
{
	dev_info(&vdev->dev, "%s: Nothing to free\n", __func__);
}

const struct vdpa_config_ops ef100_vdpa_config_ops = {
	.set_vq_address	     = ef100_vdpa_set_vq_address,
	.set_vq_num	     = ef100_vdpa_set_vq_num,
	.kick_vq	     = ef100_vdpa_kick_vq,
	.set_vq_cb	     = ef100_vdpa_set_vq_cb,
	.set_vq_ready	     = ef100_vdpa_set_vq_ready,
	.get_vq_ready	     = ef100_vdpa_get_vq_ready,
	.set_vq_state	     = ef100_vdpa_set_vq_state,
	.get_vq_state	     = ef100_vdpa_get_vq_state,
	.get_vq_notification = ef100_vdpa_get_vq_notification,
	.get_vq_irq          = ef100_get_vq_irq,
	.get_vq_align	     = ef100_vdpa_get_vq_align,
	.get_features	     = ef100_vdpa_get_features,
	.set_features	     = ef100_vdpa_set_features,
	.set_config_cb	     = ef100_vdpa_set_config_cb,
	.get_vq_num_max      = ef100_vdpa_get_vq_num_max,
	.get_device_id	     = ef100_vdpa_get_device_id,
	.get_vendor_id	     = ef100_vdpa_get_vendor_id,
	.get_status	     = ef100_vdpa_get_status,
	.set_status	     = ef100_vdpa_set_status,
	.get_config	     = ef100_vdpa_get_config,
	.set_config	     = ef100_vdpa_set_config,
	.get_generation      = NULL,
	.set_map             = NULL,
	.dma_map	     = NULL,
	.dma_unmap           = NULL,
	.free	             = ef100_vdpa_free,
};
#endif
#endif
