#ifndef __RX_H__
#define __RX_H__

#include "tx.h"

struct iso_rx_stats {
	u64 rx_bytes;
	u64 rx_packets;
};

struct iso_rx_context {
	ktime_t vq_last_update_time;
	spinlock_t vq_spinlock;
	struct list_head vq_list;
	struct hlist_head vq_bucket[ISO_MAX_VQ_BUCKETS];
	atomic_t vq_active_rate;

	struct list_head list;
	struct net_device *netdev;

	/* Hierarchical RCP state */
	struct iso_rx_stats __percpu *stats;
	struct iso_rx_stats global_stats;
	struct iso_rx_stats global_stats_last;
	ktime_t last_stats_update_time;
	ktime_t last_rcp_time;
	u32 rcp_rate;
	u32 rx_rate;
};

struct iso_rx_context *iso_rxctx_dev(const struct net_device *dev);

#ifndef QDISC
extern struct iso_rx_context global_rxcontext;
#endif

extern struct list_head rxctx_list;
#define for_each_rx_context(rxctx) list_for_each_entry_safe(rxctx, rxctx_next, &rxctx_list, list)

int iso_rx_init(struct iso_rx_context *);
void iso_rx_exit(struct iso_rx_context *);

enum iso_verdict iso_rx(struct sk_buff *skb, const struct net_device *out, struct iso_rx_context *rxctx);

static inline iso_class_t iso_rx_classify(struct sk_buff *);

int iso_vq_install(char *, struct iso_rx_context *);

static inline int iso_generate_feedback(int bit, struct sk_buff *pkt);
static inline int iso_is_generated_feedback(struct sk_buff *);


/* Create a feebdack packet and prepare for transmission.  Returns 1 if successful. */
static inline int iso_generate_feedback(int bit, struct sk_buff *pkt) {
	struct sk_buff *skb;
	struct ethhdr *eth_to, *eth_from;
	struct iphdr *iph_to, *iph_from;

	eth_from = eth_hdr(pkt);
	if(unlikely(eth_from->h_proto != __constant_htons(ETH_P_IP)))
		return 0;

	/* XXX: netdev_alloc_skb's meant to allocate packets for receiving.
	 * Is it okay to use for transmitting?
	 */
	skb = netdev_alloc_skb(pkt->dev, ISO_FEEDBACK_PACKET_SIZE);
	if(likely(skb)) {
		skb_set_queue_mapping(skb, 0);
		skb->len = ISO_FEEDBACK_PACKET_SIZE;
		skb->protocol = __constant_htons(ETH_P_IP);
		skb->pkt_type = PACKET_OUTGOING;

		skb_reset_mac_header(skb);
		skb_set_tail_pointer(skb, ISO_FEEDBACK_PACKET_SIZE);
		eth_to = eth_hdr(skb);

		memcpy(eth_to->h_source, eth_from->h_dest, ETH_ALEN);
		memcpy(eth_to->h_dest, eth_from->h_source, ETH_ALEN);
		eth_to->h_proto = eth_from->h_proto;

		skb_pull(skb, ETH_HLEN);
		skb_reset_network_header(skb);
		iph_to = ip_hdr(skb);
		iph_from = ip_hdr(pkt);

		iph_to->ihl = 5;
		iph_to->version = 4;
		iph_to->tos = 0x2 | (bit ? ISO_ECN_REFLECT_MASK : 0);
		iph_to->tot_len = __constant_htons(ISO_FEEDBACK_HEADER_SIZE);
		iph_to->id = bit; //iph_from->id;
		iph_to->frag_off = 0;
		iph_to->ttl = ISO_FEEDBACK_PACKET_TTL;
		iph_to->protocol = (u8)ISO_FEEDBACK_PACKET_IPPROTO;
		iph_to->saddr = iph_from->daddr;
		iph_to->daddr = iph_from->saddr;

		/* NB: this function doesn't "send" the packet */
		ip_send_check(iph_to);

#if defined(QDISC) || defined(DIRECT)
		skb_push(skb, ETH_HLEN);
#endif
		/* Driver owns the buffer now; we don't need to free it */
		skb_xmit(skb);
		return 1;
	}

	return 0;
}

static inline int iso_is_generated_feedback(struct sk_buff *skb) {
	struct ethhdr *eth;
	struct iphdr *iph;
	eth = eth_hdr(skb);
	if(likely(eth->h_proto == __constant_htons(ETH_P_IP))) {
		iph = ip_hdr(skb);
		if(unlikely(iph->protocol == ISO_FEEDBACK_PACKET_IPPROTO))
			return 1;
	}
	return 0;
}

static inline int iso_is_feedback_marked(struct sk_buff *skb) {
	struct ethhdr *eth;
	struct iphdr *iph;
	eth = eth_hdr(skb);
	if(likely(eth->h_proto == __constant_htons(ETH_P_IP))) {
		iph = ip_hdr(skb);
		if(unlikely(iph->protocol == ISO_FEEDBACK_PACKET_IPPROTO))
			return (iph->id);
	}
	return 0;
}

#endif /* __RX_H__ */

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */

