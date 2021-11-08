/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2005-2017 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/pci.h>
#include <linux/tcp.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/ipv6.h>
#include <linux/slab.h>
#include <net/ipv6.h>
#include <net/tcp.h>
#include <linux/if_ether.h>
#if !defined(EFX_USE_KCOMPAT)
#include <linux/highmem.h>
#else
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 20)
#include <linux/highmem.h>
#endif
#endif
#include <linux/moduleparam.h>
#include <linux/cache.h>
#include "net_driver.h"
#include "efx.h"
#include "io.h"
#include "nic.h"
#include "tx.h"
#include "workarounds.h"
#include "ef10_regs.h"
#ifdef CONFIG_SFC_TRACING
#include <trace/events/sfc.h>
#endif

/* Efx TCP segmentation acceleration.
 *
 * Why?  Because by doing it here in the driver we can go significantly
 * faster than the GSO.
 *
 * Requires TX checksum offload support.
 */

#define PTR_DIFF(p1, p2)  ((u8 *)(p1) - (u8 *)(p2))

/**
 * struct tso_state - TSO state for an SKB
 * @out_len: Remaining length in current segment
 * @seqnum: Current sequence number
 * @ipv4_id: Current IPv4 ID, host endian
 * @packet_space: Remaining space in current packet
 * @dma_addr: DMA address of current position
 * @in_len: Remaining length in current SKB fragment
 * @unmap_len: Length of SKB fragment
 * @unmap_addr: DMA address of SKB fragment
 * @dma_flags: TX buffer flags for DMA mapping - %EFX_TX_BUF_MAP_SINGLE or 0
 * @protocol: Network protocol (after any VLAN header)
 * @ip_off: Offset of IP header
 * @tcp_off: Offset of TCP header
 * @header_len: Number of bytes of header
 * @ip_base_len: IPv4 tot_len or IPv6 payload_len, before TCP payload
 * @header_dma_addr: Header DMA address, when using option descriptors
 * @header_unmap_len: Header DMA mapped length, or 0 if not using option
 *	descriptors
 *
 * The state used during segmentation.  It is put into this data structure
 * just to make it easy to pass into inline functions.
 */
struct tso_state {
	/* Output position */
	unsigned int out_len;
	unsigned int seqnum;
	u16 ipv4_id;
	unsigned int packet_space;

	/* Input position */
	dma_addr_t dma_addr;
	unsigned int in_len;
	unsigned int unmap_len;
	dma_addr_t unmap_addr;
	unsigned short dma_flags;

	__be16 protocol;
	unsigned int ip_off;
	unsigned int tcp_off;
	unsigned int header_len;
	unsigned int ip_base_len;
	dma_addr_t header_dma_addr;
	unsigned int header_unmap_len;
};

static inline void prefetch_ptr(struct efx_tx_queue *tx_queue)
{
	unsigned int insert_ptr = efx_tx_queue_get_insert_index(tx_queue);
	char *ptr;

	ptr = (char *) (tx_queue->buffer + insert_ptr);
	prefetch(ptr);
	prefetch(ptr + 0x80);

	ptr = (char *) (((efx_qword_t *)tx_queue->txd.buf.addr) + insert_ptr);
	prefetch(ptr);
	prefetch(ptr + 0x80);
}

/**
 * efx_tx_queue_insert - push descriptors onto the TX queue
 * @tx_queue:		Efx TX queue
 * @dma_addr:		DMA address of fragment
 * @len:		Length of fragment
 * @final_buffer:	The final buffer inserted into the queue
 *
 * Push descriptors onto the TX queue.
 */
static void efx_tx_queue_insert(struct efx_tx_queue *tx_queue,
				dma_addr_t dma_addr, unsigned int len,
				struct efx_tx_buffer **final_buffer)
{
	struct efx_tx_buffer *buffer;
	unsigned int dma_len;

	EFX_WARN_ON_ONCE_PARANOID(len <= 0);

	while (1) {
		buffer = efx_tx_queue_get_insert_buffer(tx_queue);
		++tx_queue->insert_count;

		EFX_WARN_ON_ONCE_PARANOID(tx_queue->insert_count -
					  tx_queue->read_count >=
					  tx_queue->efx->txq_entries);

		buffer->dma_addr = dma_addr;

		dma_len = tx_queue->efx->type->tx_limit_len(tx_queue,
				dma_addr, len);

		/* If there's space for everything this is our last buffer. */
		if (dma_len >= len)
			break;

		buffer->len = dma_len;
		buffer->flags = EFX_TX_BUF_CONT;
		dma_addr += dma_len;
		len -= dma_len;
	}

	EFX_WARN_ON_ONCE_PARANOID(!len);
	buffer->len = len;
	*final_buffer = buffer;
}

/*
 * Verify that our various assumptions about sk_buffs and the conditions
 * under which TSO will be attempted hold true.  Also determine and fill
 * in the protocol number through the provided pointer.
 */
static int efx_tso_check_protocol(struct sk_buff *skb, __be16 *protocol)
{
	*protocol = skb->protocol;

	if (((struct ethhdr *)skb->data)->h_proto != *protocol)
		return -EINVAL;

#if !defined(EFX_USE_KCOMPAT) || defined(EFX_USE_NETDEV_VLAN_FEATURES) || defined(NETIF_F_VLAN_TSO)
	if (*protocol == htons(ETH_P_8021Q)) {
		struct vlan_ethhdr *veh = (struct vlan_ethhdr *)skb->data;

		*protocol = veh->h_vlan_encapsulated_proto;
	}
#endif

	/* For SW TSO or TSOv1 we can only deal with TCP in IPv4 or IPv6 */
	if (*protocol == htons(ETH_P_IP)) {
		if (ip_hdr(skb)->protocol != IPPROTO_TCP)
			return -EINVAL;
	} else if (*protocol == htons(ETH_P_IPV6)) {
		if (ipv6_hdr(skb)->nexthdr != NEXTHDR_TCP)
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	/* All headers (incl. TCP header) must be in linear data area.  This
	 * should always be the case, so warn if not.
	 */
	if (WARN_ON((PTR_DIFF(tcp_hdr(skb), skb->data) +
		     (tcp_hdr(skb)->doff << 2u)) >
		    skb_headlen(skb)))
		return -EINVAL;

	return 0;
}

static u8 *efx_tsoh_get_buffer(struct efx_tx_queue *tx_queue,
			       struct efx_tx_buffer *buffer, unsigned int len)
{
	u8 *result;

	EFX_WARN_ON_ONCE_PARANOID(buffer->len);
	EFX_WARN_ON_ONCE_PARANOID(buffer->flags);
	EFX_WARN_ON_ONCE_PARANOID(buffer->unmap_len);

	result = efx_tx_get_copy_buffer_limited(tx_queue, buffer, len);

	if (result) {
		buffer->flags = EFX_TX_BUF_CONT;
	} else {
		buffer->buf = kmalloc(NET_IP_ALIGN + len, GFP_ATOMIC);
		if (unlikely(!buffer->buf))
			return NULL;
		tx_queue->tso_long_headers++;
		result = (u8 *)buffer->buf + NET_IP_ALIGN;
		buffer->flags = EFX_TX_BUF_CONT | EFX_TX_BUF_HEAP;
	}

	buffer->len = len;

	return result;
}

/*
 * Put a TSO header into the TX queue.
 *
 * This is special-cased because we know that it is small enough to fit in
 * a single fragment, and we know it doesn't cross a page boundary.  It
 * also allows us to not worry about end-of-packet etc.
 */
static int efx_tso_put_header(struct efx_tx_queue *tx_queue,
			      struct efx_tx_buffer *buffer, u8 *header)
{
	if (unlikely(buffer->flags & EFX_TX_BUF_HEAP)) {
		buffer->dma_addr = dma_map_single(&tx_queue->efx->pci_dev->dev,
						  header, buffer->len,
						  DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(&tx_queue->efx->pci_dev->dev,
					       buffer->dma_addr))) {
			kfree(buffer->buf);
			buffer->len = 0;
			buffer->flags = 0;
			return -ENOMEM;
		}
		buffer->unmap_len = buffer->len;
		buffer->dma_offset = 0;
		buffer->flags |= EFX_TX_BUF_MAP_SINGLE;
	}

	++tx_queue->insert_count;
	return 0;
}


/* Parse the SKB header and initialise state. */
static int tso_start(struct tso_state *st, struct efx_nic *efx,
		     struct efx_tx_queue *tx_queue,
		     const struct sk_buff *skb)
{
	struct device *dma_dev = &efx->pci_dev->dev;
	unsigned int header_len, in_len;
	bool use_opt_desc = false;
	dma_addr_t dma_addr;

	if (tx_queue->tso_version == 1)
		use_opt_desc = true;

	st->ip_off = skb_network_header(skb) - skb->data;
	st->tcp_off = skb_transport_header(skb) - skb->data;
	header_len = st->tcp_off + (tcp_hdr(skb)->doff << 2u);
	in_len = skb_headlen(skb) - header_len;
	st->header_len = header_len;
	st->in_len = in_len;
	if (st->protocol == htons(ETH_P_IP)) {
		st->ip_base_len = st->header_len - st->ip_off;
		st->ipv4_id = ntohs(ip_hdr(skb)->id);
	} else {
		st->ip_base_len = st->header_len - st->tcp_off;
		st->ipv4_id = 0;
	}
	st->seqnum = ntohl(tcp_hdr(skb)->seq);

	EFX_WARN_ON_ONCE_PARANOID(tcp_hdr(skb)->urg);
	EFX_WARN_ON_ONCE_PARANOID(tcp_hdr(skb)->syn);
	EFX_WARN_ON_ONCE_PARANOID(tcp_hdr(skb)->rst);

	st->out_len = skb->len - header_len;

	if (!use_opt_desc) {
		st->header_unmap_len = 0;

		if (likely(in_len == 0)) {
			st->dma_flags = 0;
			st->unmap_len = 0;
			return 0;
		}

		dma_addr = dma_map_single(dma_dev, skb->data + header_len,
					  in_len, DMA_TO_DEVICE);
		st->dma_flags = EFX_TX_BUF_MAP_SINGLE;
		st->dma_addr = dma_addr;
		st->unmap_addr = dma_addr;
		st->unmap_len = in_len;
	} else {
		dma_addr = dma_map_single(dma_dev, skb->data,
					  skb_headlen(skb), DMA_TO_DEVICE);
		st->header_dma_addr = dma_addr;
		st->header_unmap_len = skb_headlen(skb);
		st->dma_flags = 0;
		st->dma_addr = dma_addr + header_len;
		st->unmap_len = 0;
	}

	return unlikely(dma_mapping_error(dma_dev, dma_addr)) ? -ENOMEM : 0;
}

static int tso_get_fragment(struct tso_state *st, struct efx_nic *efx,
			    skb_frag_t *frag)
{
	st->unmap_addr = skb_frag_dma_map(&efx->pci_dev->dev, frag, 0,
					  skb_frag_size(frag), DMA_TO_DEVICE);
	if (likely(!dma_mapping_error(&efx->pci_dev->dev, st->unmap_addr))) {
		st->dma_flags = 0;
		st->unmap_len = skb_frag_size(frag);
		st->in_len = skb_frag_size(frag);
		st->dma_addr = st->unmap_addr;
		return 0;
	}
	return -ENOMEM;
}


/**
 * tso_fill_packet_with_fragment - form descriptors for the current fragment
 * @tx_queue:		Efx TX queue
 * @skb:		Socket buffer
 * @st:			TSO state
 *
 * Form descriptors for the current fragment, until we reach the end
 * of fragment or end-of-packet.
 */
static void tso_fill_packet_with_fragment(struct efx_tx_queue *tx_queue,
					  const struct sk_buff *skb,
					  struct tso_state *st)
{
	struct efx_tx_buffer *buffer;
	int n;

	if (st->in_len == 0)
		return;
	if (st->packet_space == 0)
		return;

	EFX_WARN_ON_ONCE_PARANOID(st->in_len <= 0);
	EFX_WARN_ON_ONCE_PARANOID(st->packet_space <= 0);

	n = min(st->in_len, st->packet_space);

	st->packet_space -= n;
	st->out_len -= n;
	st->in_len -= n;

	efx_tx_queue_insert(tx_queue, st->dma_addr, n, &buffer);

	if (st->out_len == 0) {
		/* Transfer ownership of the skb */
		buffer->skb = skb;
		buffer->flags = EFX_TX_BUF_SKB;
	} else if (st->packet_space != 0) {
		buffer->flags = EFX_TX_BUF_CONT;
	}

	if (st->in_len == 0) {
		/* Transfer ownership of the DMA mapping */
		buffer->unmap_len = st->unmap_len;
		buffer->dma_offset = buffer->unmap_len - buffer->len;
		buffer->flags |= st->dma_flags;
		st->unmap_len = 0;
	}

	st->dma_addr += n;
}

/**
 * tso_start_new_packet - generate a new header and prepare for the new packet
 * @tx_queue:		Efx TX queue
 * @skb:		Socket buffer
 * @st:			TSO state
 * @is_first:		true if this is the first packet
 *
 * Generate a new header and prepare for the new packet.  Return 0 on
 * success, or -%ENOMEM if failed to alloc header.
 */
static int tso_start_new_packet(struct efx_tx_queue *tx_queue,
				const struct sk_buff *skb,
				struct tso_state *st,
				bool is_first)
{
	struct efx_tx_buffer *buffer =
		efx_tx_queue_get_insert_buffer(tx_queue);
	bool is_last = st->out_len <= skb_shinfo(skb)->gso_size;
	u8 tcp_flags_mask;

	if (!is_last) {
		st->packet_space = skb_shinfo(skb)->gso_size;
		tcp_flags_mask = TCPHDR_FIN | TCPHDR_PSH;
	} else {
		st->packet_space = st->out_len;
		tcp_flags_mask = 0;
	}

	if (!is_first)
		tcp_flags_mask |= TCPHDR_CWR; /* Congestion control */

	if (!st->header_unmap_len) {
		/* Allocate and insert a DMA-mapped header buffer. */
		struct tcphdr *tsoh_th;
		unsigned int ip_length;
		u8 *header;
		int rc;

		header = efx_tsoh_get_buffer(tx_queue, buffer, st->header_len);
		if (!header)
			return -ENOMEM;

		tsoh_th = (struct tcphdr *)(header + st->tcp_off);

		/* Copy and update the headers. */
		memcpy(header, skb->data, st->header_len);

		tsoh_th->seq = htonl(st->seqnum);
		tcp_flag_byte(tsoh_th) &= ~tcp_flags_mask;

		ip_length = st->ip_base_len + st->packet_space;

		if (st->protocol == htons(ETH_P_IP)) {
			struct iphdr *tsoh_iph =
				(struct iphdr *)(header + st->ip_off);

			tsoh_iph->tot_len = htons(ip_length);
			tsoh_iph->id = htons(st->ipv4_id);
		} else {
			struct ipv6hdr *tsoh_iph =
				(struct ipv6hdr *)(header + st->ip_off);

			tsoh_iph->payload_len = htons(ip_length);
		}

		rc = efx_tso_put_header(tx_queue, buffer, header);
		if (unlikely(rc))
			return rc;
	} else {
		/* Send the original headers with a TSO option descriptor
		 * in front
		 */
		u8 tcp_flags = tcp_flag_byte(tcp_hdr(skb)) & ~tcp_flags_mask;

		buffer->flags = EFX_TX_BUF_OPTION;
		buffer->len = 0;
		buffer->unmap_len = 0;
		EFX_POPULATE_QWORD_5(buffer->option,
				     ESF_DZ_TX_DESC_IS_OPT, 1,
				     ESF_DZ_TX_OPTION_TYPE,
				     ESE_DZ_TX_OPTION_DESC_TSO,
				     ESF_DZ_TX_TSO_TCP_FLAGS, tcp_flags,
				     ESF_DZ_TX_TSO_IP_ID, st->ipv4_id,
				     ESF_DZ_TX_TSO_TCP_SEQNO, st->seqnum);
		++tx_queue->insert_count;

		/* We mapped the headers in tso_start().  Unmap them
		 * when the last segment is completed.
		 */
		buffer = efx_tx_queue_get_insert_buffer(tx_queue);
		buffer->dma_addr = st->header_dma_addr;
		buffer->len = st->header_len;
		if (is_last) {
			buffer->flags = EFX_TX_BUF_CONT | EFX_TX_BUF_MAP_SINGLE;
			buffer->unmap_len = st->header_unmap_len;
			buffer->dma_offset = 0;
			/* Ensure we only unmap them once in case of a
			 * later DMA mapping error and rollback
			 */
			st->header_unmap_len = 0;
		} else {
			buffer->flags = EFX_TX_BUF_CONT;
			buffer->unmap_len = 0;
		}
		++tx_queue->insert_count;
	}

	st->seqnum += skb_shinfo(skb)->gso_size;

	/* Linux leaves suitable gaps in the IP ID space for us to fill. */
	++st->ipv4_id;

	return 0;
}

/**
 * efx_nic_tx_tso_sw - segment and transmit a TSO socket buffer using SW or FATSOv1
 * @tx_queue:		Efx TX queue
 * @skb:		Socket buffer
 * @data_mapped:        Did we map the data? Always set to true
 *                      by this on success.
 *
 * Context: You must hold netif_tx_lock() to call this function.
 *
 * Add socket buffer @skb to @tx_queue, doing TSO or return != 0 if
 * @skb was not enqueued.  In all cases @skb is consumed.  Return
 * %NETDEV_TX_OK.
 */
int efx_nic_tx_tso_sw(struct efx_tx_queue *tx_queue, struct sk_buff *skb,
		      bool *data_mapped)
{
	struct efx_nic *efx = tx_queue->efx;
	int frag_i, rc;
	struct tso_state state;

#if defined(EFX_USE_KCOMPAT) && !defined(EFX_HAVE_GSO_MAX_SEGS)
	/* Since the stack does not limit the number of segments per
	 * skb, we must do so.  Otherwise an attacker may be able to
	 * make the TCP produce skbs that will never fit in our TX
	 * queue, causing repeated resets.
	 */
	if (unlikely(skb_shinfo(skb)->gso_segs > EFX_TSO_MAX_SEGS)) {
		unsigned int excess =
			(skb_shinfo(skb)->gso_segs - EFX_TSO_MAX_SEGS) *
			skb_shinfo(skb)->gso_size;
		if (__pskb_trim(skb, skb->len - excess))
			return -E2BIG;
	}
#endif

	prefetch(skb->data);

	/* Find the packet protocol and sanity-check it */
	rc = efx_tso_check_protocol(skb, &state.protocol);
	if (rc)
		return rc;

	rc = tso_start(&state, efx, tx_queue, skb);
	if (rc)
		goto mem_err;

	if (likely(state.in_len == 0)) {
		/* Grab the first payload fragment. */
		EFX_WARN_ON_ONCE_PARANOID(skb_shinfo(skb)->nr_frags < 1);
		frag_i = 0;
		rc = tso_get_fragment(&state, efx,
				      skb_shinfo(skb)->frags + frag_i);
		if (rc)
			goto mem_err;
	} else {
		/* Payload starts in the header area. */
		frag_i = -1;
	}

	if (tso_start_new_packet(tx_queue, skb, &state, true) < 0)
		goto mem_err;

	prefetch_ptr(tx_queue);

	while (1) {
		tso_fill_packet_with_fragment(tx_queue, skb, &state);

		/* Move onto the next fragment? */
		if (state.in_len == 0) {
			if (++frag_i >= skb_shinfo(skb)->nr_frags)
				/* End of payload reached. */
				break;
			rc = tso_get_fragment(&state, efx,
					      skb_shinfo(skb)->frags + frag_i);
			if (rc)
				goto mem_err;
		}

		/* Start at new packet? */
		if (state.packet_space == 0 &&
		    tso_start_new_packet(tx_queue, skb, &state, false) < 0)
			goto mem_err;
	}

	*data_mapped = true;

	return 0;

 mem_err:
	netif_err(efx, tx_err, efx->net_dev,
		  "Out of memory for TSO headers, or DMA mapping error\n");

	/* Free the DMA mapping we were in the process of writing out */
	if (state.unmap_len) {
		if (state.dma_flags & EFX_TX_BUF_MAP_SINGLE)
			dma_unmap_single(&efx->pci_dev->dev, state.unmap_addr,
					 state.unmap_len, DMA_TO_DEVICE);
		else
			dma_unmap_page(&efx->pci_dev->dev, state.unmap_addr,
				       state.unmap_len, DMA_TO_DEVICE);
	}

	/* Free the header DMA mapping, if using option descriptors */
	if (state.header_unmap_len)
		dma_unmap_single(&efx->pci_dev->dev, state.header_dma_addr,
				 state.header_unmap_len, DMA_TO_DEVICE);

	return -ENOMEM;
}
