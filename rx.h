#ifndef __RX_H__
#define __RX_H__

#include "tx.h"

int iso_rx_init(void);
void iso_rx_exit(void);

enum iso_verdict iso_rx(struct sk_buff *skb, const struct net_device *out);

static inline iso_class_t iso_rx_classify(struct sk_buff *);

int iso_vq_install(char *);

static inline int iso_generate_feedback(int bit, struct sk_buff *pkt);
static inline int iso_is_generated_feedback(struct sk_buff *);


extern char *iso_param_dev;
extern struct net_device *iso_netdev;

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
	skb = netdev_alloc_skb(iso_netdev, ISO_FEEDBACK_PACKET_SIZE);
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

#ifdef DIRECT
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

