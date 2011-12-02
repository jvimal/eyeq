#include <linux/netfilter_bridge.h>

/* All this from bridging code */
static inline void nf_bridge_update_protocol(struct sk_buff *skb)
{
	if (skb->nf_bridge->mask & BRNF_8021Q)
		skb->protocol = htons(ETH_P_8021Q);
	else if (skb->nf_bridge->mask & BRNF_PPPoE)
		skb->protocol = htons(ETH_P_PPP_SES);
}

int nf_bridge_copy_header(struct sk_buff *skb)
{
	int err;
	unsigned int header_size;

	nf_bridge_update_protocol(skb);
	header_size = ETH_HLEN + nf_bridge_encap_header_len(skb);
	err = skb_cow_head(skb, header_size);
	if (err)
		return err;

	skb_copy_to_linear_data_offset(skb, -header_size,
								   skb->nf_bridge->data, header_size);
	__skb_push(skb, nf_bridge_encap_header_len(skb));
	return 0;
}

static inline unsigned packet_length(const struct sk_buff *skb)
{
	return skb->len - (skb->protocol == htons(ETH_P_8021Q) ? VLAN_HLEN : 0);
}

/* This is what "br_dev_queue_push_xmit" would do */
inline void skb_xmit(struct sk_buff *skb) {
	/* ip_fragment doesn't copy the MAC header */
	if(nf_bridge_maybe_copy_header(skb) ||
	   (packet_length(skb) > skb->dev->mtu && !skb_is_gso(skb))) {
		kfree_skb(skb);
	} else {
		skb_push(skb, ETH_HLEN);
		dev_queue_xmit(skb);
	}
}
