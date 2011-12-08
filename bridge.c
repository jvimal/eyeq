#include <linux/netfilter_bridge.h>
#include "rl.h"

extern struct net_device *iso_netdev;
struct nf_hook_ops hook_in;
struct nf_hook_ops hook_out;

/* Bridge specific code */
unsigned int iso_rx_bridge(unsigned int hooknum,
			struct sk_buff *skb,
			const struct net_device *in,
			const struct net_device *out,
			int (*okfn)(struct sk_buff *));

unsigned int iso_tx_bridge(unsigned int hooknum,
			struct sk_buff *skb,
			const struct net_device *in,
			const struct net_device *out,
			int (*okfn)(struct sk_buff *));

int iso_tx_bridge_init(void);
void iso_tx_bridge_exit(void);

int iso_rx_bridge_init(void);
void iso_rx_bridge_exit(void);


enum iso_verdict iso_tx(struct sk_buff *skb, const struct net_device *out);
enum iso_verdict iso_rx(struct sk_buff *skb, const struct net_device *in);

/* This is what "br_dev_queue_push_xmit" would do */
inline void skb_xmit(struct sk_buff *skb) {
    skb_push(skb, ETH_HLEN);
    dev_queue_xmit(skb);
}

int iso_tx_bridge_init() {
	hook_out.hook = iso_tx_bridge;
	hook_out.hooknum= NF_BR_POST_ROUTING;
	hook_out.pf = PF_BRIDGE;
	hook_out.priority = NF_BR_PRI_BRNF;

    return nf_register_hook(&hook_out);
}

int iso_rx_bridge_init() {
	hook_in.hook = iso_rx_bridge;
	hook_in.hooknum= NF_BR_PRE_ROUTING;
	hook_in.pf = PF_BRIDGE;
	hook_in.priority = NF_BR_PRI_FIRST;

    return nf_register_hook(&hook_in);
}

void iso_tx_bridge_exit() {
	nf_unregister_hook(&hook_out);
}

void iso_rx_bridge_exit() {
	nf_unregister_hook(&hook_in);
}


unsigned int iso_rx_bridge(unsigned int hooknum,
			struct sk_buff *skb,
			const struct net_device *in,
			const struct net_device *out,
			int (*okfn)(struct sk_buff *))
{
	enum iso_verdict verdict;
	/* out will be NULL if this is PRE_ROUTING */
	if(in != iso_netdev)
		return NF_ACCEPT;

	verdict = iso_rx(skb, in);

	switch(verdict) {
	case ISO_VERDICT_DROP:
		return NF_DROP;

	default:
	case ISO_VERDICT_SUCCESS:
		return NF_ACCEPT;
	}

	/* Unreachable */
	return NF_DROP;
}

unsigned int iso_tx_bridge(unsigned int hooknum,
			struct sk_buff *skb,
			const struct net_device *in,
			const struct net_device *out,
			int (*okfn)(struct sk_buff *))
{
	enum iso_verdict verdict;

	/* out shouldn't be NULL, but let's be careful anyway */
	if(out != iso_netdev)
		return NF_ACCEPT;

	verdict = iso_tx(skb, out);

	switch(verdict) {
	case ISO_VERDICT_DROP:
		return NF_DROP;

	case ISO_VERDICT_SUCCESS:
		return NF_STOLEN;

	case ISO_VERDICT_PASS:
	default:
		return NF_ACCEPT;
	}

	/* Unreachable */
	return NF_DROP;
}

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
