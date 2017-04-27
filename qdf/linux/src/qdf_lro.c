/*
 * Copyright (c) 2015-2017 The Linux Foundation. All rights reserved.
 *
 * Previously licensed under the ISC license by Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This file was originally distributed by Qualcomm Atheros, Inc.
 * under proprietary terms before Copyright ownership was assigned
 * to the Linux Foundation.
 */

/**
 * DOC: qdf_lro.c
 * QCA driver framework(QDF) Large Receive Offload
 */

#include <qdf_lro.h>
#include <qdf_trace.h>
#include <qdf_types.h>

#include <linux/list.h>
#include <net/tcp.h>

/**
 * qdf_lro_desc_pool_init() - Initialize the free pool of LRO
 * descriptors
 * @lro_desc_pool: free pool of the LRO descriptors
 * @lro_mgr: LRO manager
 *
 * Initialize a list that holds the free LRO descriptors
 *
 * Return: none
 */
static void qdf_lro_desc_pool_init(struct qdf_lro_desc_pool *lro_desc_pool,
	 struct net_lro_mgr *lro_mgr)
{
	int i;

	INIT_LIST_HEAD(&lro_desc_pool->lro_free_list_head);

	for (i = 0; i < QDF_LRO_DESC_POOL_SZ; i++) {
		lro_desc_pool->lro_desc_array[i].lro_desc =
			 &lro_mgr->lro_arr[i];
		list_add_tail(&lro_desc_pool->lro_desc_array[i].lro_node,
			 &lro_desc_pool->lro_free_list_head);
	}
}

/**
 * qdf_lro_desc_info_init() - Initialize the LRO descriptors
 * @qdf_info: QDF LRO data structure
 *
 * Initialize the free pool of LRO descriptors and the entries
 * of the hash table
 *
 * Return: none
 */
static void qdf_lro_desc_info_init(struct qdf_lro_s *qdf_info)
{
	int i;

	/* Initialize pool of free LRO desc.*/
	qdf_lro_desc_pool_init(&qdf_info->lro_desc_info.lro_desc_pool,
		 qdf_info->lro_mgr);

	/* Initialize the hash table of LRO desc.*/
	for (i = 0; i < QDF_LRO_DESC_TABLE_SZ; i++) {
		/* initialize the flows in the hash table */
		INIT_LIST_HEAD(&qdf_info->lro_desc_info.
			 lro_hash_table[i].lro_desc_list);
	}

}

/**
 * qdf_lro_get_skb_header() - LRO callback function
 * @skb: network buffer
 * @ip_hdr: contains a pointer to the IP header
 * @tcpudp_hdr: contains a pointer to the TCP header
 * @hdr_flags: indicates if this is a TCP, IPV4 frame
 * @priv: private driver specific opaque pointer
 *
 * Get the IP and TCP headers from the skb
 *
 * Return: 0 - success, < 0 - failure
 */
static int qdf_lro_get_skb_header(struct sk_buff *skb, void **ip_hdr,
	void **tcpudp_hdr, u64 *hdr_flags, void *priv)
{
	if (QDF_NBUF_CB_RX_IPV6_PROTO(skb)) {
		hdr_flags = 0;
		return -EINVAL;
	}

	*hdr_flags |= (LRO_IPV4 | LRO_TCP);
	(*ip_hdr) = skb->data;
	(*tcpudp_hdr) = skb->data + QDF_NBUF_CB_RX_TCP_OFFSET(skb);
	return 0;
}

/**
 * qdf_lro_init() - LRO initialization function
 *
 * Return: LRO context
 */
qdf_lro_ctx_t qdf_lro_init(void)
{
	struct qdf_lro_s *lro_ctx;
	size_t lro_info_sz, lro_mgr_sz, desc_arr_sz, desc_pool_sz;
	size_t hash_table_sz;
	uint8_t *lro_mem_ptr;

	/*
	* Allocate all the LRO data structures at once and then carve
	* them up as needed
	*/
	lro_info_sz = sizeof(struct qdf_lro_s);
	lro_mgr_sz = sizeof(struct net_lro_mgr);
	desc_arr_sz =
		 (QDF_LRO_DESC_POOL_SZ * sizeof(struct net_lro_desc));
	desc_pool_sz =
		 (QDF_LRO_DESC_POOL_SZ * sizeof(struct qdf_lro_desc_entry));
	hash_table_sz =
		 (sizeof(struct qdf_lro_desc_table) * QDF_LRO_DESC_TABLE_SZ);

	lro_mem_ptr = qdf_mem_malloc(lro_info_sz + lro_mgr_sz + desc_arr_sz +
					desc_pool_sz + hash_table_sz);

	if (unlikely(!lro_mem_ptr)) {
		QDF_TRACE(QDF_MODULE_ID_QDF, QDF_TRACE_LEVEL_ERROR,
			 "Unable to allocate memory for LRO");
		return NULL;
	}

	lro_ctx = (struct qdf_lro_s *)lro_mem_ptr;
	lro_mem_ptr += lro_info_sz;
	/* LRO manager */
	lro_ctx->lro_mgr = (struct net_lro_mgr *)lro_mem_ptr;
	lro_mem_ptr += lro_mgr_sz;

	/* LRO decriptor array */
	lro_ctx->lro_mgr->lro_arr = (struct net_lro_desc *)lro_mem_ptr;
	lro_mem_ptr += desc_arr_sz;

	/* LRO descriptor pool */
	lro_ctx->lro_desc_info.lro_desc_pool.lro_desc_array =
		 (struct qdf_lro_desc_entry *)lro_mem_ptr;
	lro_mem_ptr += desc_pool_sz;

	/* hash table to store the LRO descriptors */
	lro_ctx->lro_desc_info.lro_hash_table =
		 (struct qdf_lro_desc_table *)lro_mem_ptr;

	/* Initialize the LRO descriptors */
	qdf_lro_desc_info_init(lro_ctx);

	/* LRO TODO - NAPI or RX thread */
	/* lro_ctx->lro_mgr->features = LRO_F_NI */

	lro_ctx->lro_mgr->ip_summed_aggr = CHECKSUM_UNNECESSARY;
	lro_ctx->lro_mgr->max_aggr = QDF_LRO_MAX_AGGR_SIZE;
	lro_ctx->lro_mgr->get_skb_header = qdf_lro_get_skb_header;
	lro_ctx->lro_mgr->ip_summed = CHECKSUM_UNNECESSARY;
	lro_ctx->lro_mgr->max_desc = QDF_LRO_DESC_POOL_SZ;

	return lro_ctx;
}

/**
 * qdf_lro_deinit() - LRO deinitialization function
 * @lro_ctx: LRO context
 *
 * Return: nothing
 */
void qdf_lro_deinit(qdf_lro_ctx_t lro_ctx)
{
	if (unlikely(lro_ctx)) {
		QDF_TRACE(QDF_MODULE_ID_QDF, QDF_TRACE_LEVEL_ERROR,
			 "LRO instance %p is being freed", lro_ctx);
		qdf_mem_free(lro_ctx);
	}
}

/**
 * qdf_lro_tcp_flow_match() - function to check for a flow match
 * @iph: IP header
 * @tcph: TCP header
 * @lro_desc: LRO decriptor
 *
 * Checks if the descriptor belongs to the same flow as the one
 * indicated by the TCP and IP header.
 *
 * Return: true - flow match, false - flow does not match
 */
static inline bool qdf_lro_tcp_flow_match(struct net_lro_desc *lro_desc,
	 struct iphdr *iph,
	 struct tcphdr *tcph)
{
	if ((lro_desc->tcph->source != tcph->source) ||
		 (lro_desc->tcph->dest != tcph->dest) ||
		 (lro_desc->iph->saddr != iph->saddr) ||
		 (lro_desc->iph->daddr != iph->daddr))
		return false;

	return true;

}

/**qdf_lro_desc_find() - LRO descriptor look-up function
 *
 * @lro_ctx: LRO context
 * @skb: network buffer
 * @iph: IP header
 * @tcph: TCP header
 * @flow_id: toeplitz hash
 *
 * Look-up the LRO descriptor in the hash table based on the
 * flow ID toeplitz. If the flow is not found, allocates a new
 * LRO descriptor and places it in the hash table
 *
 * Return: lro descriptor
 */
static struct net_lro_desc *qdf_lro_desc_find(struct qdf_lro_s *lro_ctx,
	 struct sk_buff *skb, struct iphdr *iph, struct tcphdr *tcph,
	 uint32_t flow_hash)
{
	uint32_t i;
	struct qdf_lro_desc_table *lro_hash_table;
	struct list_head *ptr;
	struct qdf_lro_desc_entry *entry;
	struct qdf_lro_desc_pool *free_pool;
	struct qdf_lro_desc_info *desc_info = &lro_ctx->lro_desc_info;

	i = flow_hash & QDF_LRO_DESC_TABLE_SZ_MASK;

	lro_hash_table = &desc_info->lro_hash_table[i];

	if (unlikely(!lro_hash_table)) {
		QDF_TRACE(QDF_MODULE_ID_QDF, QDF_TRACE_LEVEL_ERROR,
			 "Invalid hash entry");
		QDF_ASSERT(0);
		return NULL;
	}

	/* Check if this flow exists in the descriptor list */
	list_for_each(ptr, &lro_hash_table->lro_desc_list) {
		struct net_lro_desc *tmp_lro_desc = NULL;

		entry = list_entry(ptr, struct qdf_lro_desc_entry, lro_node);
		tmp_lro_desc = entry->lro_desc;
		if (tmp_lro_desc->active) {
			if (qdf_lro_tcp_flow_match(tmp_lro_desc, iph, tcph)) {
				return entry->lro_desc;
			}
		}
	}

	/* no existing flow found, a new LRO desc needs to be allocated */
	free_pool = &lro_ctx->lro_desc_info.lro_desc_pool;
	entry = list_first_entry_or_null(
		 &free_pool->lro_free_list_head,
		 struct qdf_lro_desc_entry, lro_node);
	if (unlikely(!entry)) {
		QDF_TRACE(QDF_MODULE_ID_QDF, QDF_TRACE_LEVEL_ERROR,
			 "Could not allocate LRO desc!");
		return NULL;
	}

	list_del_init(&entry->lro_node);

	if (unlikely(!entry->lro_desc)) {
		QDF_TRACE(QDF_MODULE_ID_QDF, QDF_TRACE_LEVEL_ERROR,
			 "entry->lro_desc is NULL!");
		return NULL;
	}

	memset(entry->lro_desc, 0, sizeof(struct net_lro_desc));

	/*
	 * lro_desc->active should be 0 and lro_desc->tcp_rcv_tsval
	 * should be 0 for newly allocated lro descriptors
	 */
	list_add_tail(&entry->lro_node,
		 &lro_hash_table->lro_desc_list);

	return entry->lro_desc;
}

/**qdf_lro_update_info() - Update the LRO information
 *
 * @lro_ctx: LRO context
 * @nbuf: network buffer
 *
 * Look-up the LRO descriptor based on the LRO information and
 * the network buffer provided. Update the skb cb with the
 * descriptor found
 *
 * Return: true: LRO eligible false: LRO ineligible
 */
bool qdf_lro_update_info(qdf_lro_ctx_t lro_ctx, qdf_nbuf_t nbuf)
{
	struct iphdr *iph;
	struct tcphdr *tcph;
	struct net_lro_desc *lro_desc = NULL;

	iph = (struct iphdr *)((char *)nbuf->data + ETH_HLEN);
	tcph = (struct tcphdr *)((char *)nbuf->data + ETH_HLEN +
			 QDF_NBUF_CB_RX_TCP_OFFSET(nbuf));
	QDF_NBUF_CB_RX_LRO_DESC(nbuf) = NULL;

	lro_desc = qdf_lro_desc_find(lro_ctx, nbuf, iph, tcph,
		 QDF_NBUF_CB_RX_FLOW_ID_TOEPLITZ(nbuf));
	if (unlikely(!lro_desc)) {
		QDF_TRACE(QDF_MODULE_ID_QDF, QDF_TRACE_LEVEL_ERROR,
			 "finding the LRO desc failed");
		return false;
	}

	/* if this is not the first skb, check the timestamp option */
	if (lro_desc->tcp_rcv_tsval) {
		if (tcph->doff == 8) {
			__be32 *topt = (__be32 *)(tcph + 1);

			if (*topt != htonl((TCPOPT_NOP << 24)
				 |(TCPOPT_NOP << 16)
				 | (TCPOPT_TIMESTAMP << 8)
				 | TCPOLEN_TIMESTAMP))
				return true;

			/* timestamp should be in right order */
			topt++;
			if (after(ntohl(lro_desc->tcp_rcv_tsval),
					 ntohl(*topt)))
				return false;

			/* timestamp reply should not be zero */
			topt++;
			if (*topt == 0)
				return false;
		}
	}

	QDF_NBUF_CB_RX_LRO_DESC(nbuf) = (void *)lro_desc;
	return true;
}

/**
 * qdf_lro_desc_free() - Free the LRO descriptor
 * @desc: LRO descriptor
 * @lro_ctx: LRO context
 *
 * Return the LRO descriptor to the free pool
 *
 * Return: none
 */
static void qdf_lro_desc_free(struct net_lro_desc *desc,
	 qdf_lro_ctx_t lro_ctx)
{
	struct qdf_lro_desc_entry *entry;
	struct net_lro_mgr *lro_mgr;
	struct net_lro_desc *arr_base;
	struct qdf_lro_desc_info *desc_info;
	int i;

	qdf_assert(desc);
	qdf_assert(lro_ctx);

	if (unlikely(!desc || !lro_ctx)) {
		QDF_TRACE(QDF_MODULE_ID_QDF, QDF_TRACE_LEVEL_ERROR,
			 "invalid input");
		return;
	}

	lro_mgr = lro_ctx->lro_mgr;
	arr_base = lro_mgr->lro_arr;
	i = desc - arr_base;

	if (unlikely(i >= QDF_LRO_DESC_POOL_SZ)) {
		QDF_TRACE(QDF_MODULE_ID_QDF, QDF_TRACE_LEVEL_ERROR,
			 "invalid index %d", i);
		return;
	}

	desc_info =  &lro_ctx->lro_desc_info;
	entry = &desc_info->lro_desc_pool.lro_desc_array[i];

	list_del_init(&entry->lro_node);

	list_add_tail(&entry->lro_node, &desc_info->
		 lro_desc_pool.lro_free_list_head);
}

/**
 * qdf_lro_flow_free() - Free the LRO flow resources
 * @nbuf: network buffer
 *
 * Return the LRO descriptor to the free pool
 *
 * Return: none
 */
void qdf_lro_flow_free(qdf_nbuf_t nbuf)
{
	struct net_lro_desc *desc = QDF_NBUF_CB_RX_LRO_DESC(nbuf);
	qdf_lro_ctx_t ctx = QDF_NBUF_CB_RX_LRO_CTX(nbuf);

	qdf_lro_desc_free(desc, ctx);
}

/**
 * qdf_lro_flush() - LRO flush API
 * @lro_ctx: LRO context
 *
 * Flush all the packets aggregated in the LRO manager for all
 * the flows
 *
 * Return: none
 */
void qdf_lro_flush(qdf_lro_ctx_t lro_ctx)
{
	struct net_lro_mgr *lro_mgr = lro_ctx->lro_mgr;
	int i;

	for (i = 0; i < lro_mgr->max_desc; i++) {
		if (lro_mgr->lro_arr[i].active) {
			qdf_lro_desc_free(&lro_mgr->lro_arr[i], lro_ctx);
			lro_flush_desc(lro_mgr, &lro_mgr->lro_arr[i]);
		}
	}
}

/**
 * qdf_lro_get_desc() - LRO descriptor look-up function
 * @iph: IP header
 * @tcph: TCP header
 * @lro_arr: Array of LRO decriptors
 * @lro_mgr: LRO manager
 *
 * Looks-up the LRO descriptor for a given flow
 *
 * Return: LRO descriptor
 */
static struct net_lro_desc *qdf_lro_get_desc(struct net_lro_mgr *lro_mgr,
	 struct net_lro_desc *lro_arr,
	 struct iphdr *iph,
	 struct tcphdr *tcph)
{
	int i;

	for (i = 0; i < lro_mgr->max_desc; i++) {
		if (lro_arr[i].active)
			if (qdf_lro_tcp_flow_match(&lro_arr[i], iph, tcph))
				return &lro_arr[i];
	}

	return NULL;
}

/**
 * qdf_lro_flush_pkt() - function to flush the LRO flow
 * @iph: IP header
 * @tcph: TCP header
 * @lro_ctx: LRO context
 *
 * Flush all the packets aggregated in the LRO manager for the
 * flow indicated by the TCP and IP header
 *
 * Return: none
 */
void qdf_lro_flush_pkt(struct iphdr *iph,
	 struct tcphdr *tcph, qdf_lro_ctx_t lro_ctx)
{
	struct net_lro_desc *lro_desc;
	struct net_lro_mgr *lro_mgr = lro_ctx->lro_mgr;

	lro_desc = qdf_lro_get_desc(lro_mgr, lro_mgr->lro_arr, iph, tcph);

	if (lro_desc) {
		/* statistics */
		qdf_lro_desc_free(lro_desc, lro_ctx);
		lro_flush_desc(lro_mgr, lro_desc);
	}
}
