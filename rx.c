
#include <linux/netfilter_bridge.h>
#include "rx.h"
#include "vq.h"

extern char *iso_param_dev;
struct nf_hook_ops hook_in;

int iso_rx_init() {
	printk(KERN_INFO "perfiso: Init RX path\n");
	iso_vqs_init();

	hook_in.hook = iso_rx_bridge;
	hook_in.hooknum= NF_BR_PRE_ROUTING;
	hook_in.pf = PF_BRIDGE;
	hook_in.priority = NF_BR_PRI_BRNF;

	return nf_register_hook(&hook_in);
}

void iso_rx_exit() {
	nf_unregister_hook(&hook_in);
}

/* There could be other ways of receiving packets */
unsigned int iso_rx_bridge(unsigned int hooknum,
			struct sk_buff *skb,
			const struct net_device *in,
			const struct net_device *out,
			int (*okfn)(struct sk_buff *))
{
	/* out will be NULL if this is PRE_ROUTING */
	if(!in || strcmp(in->name, iso_param_dev) != 0)
		return NF_ACCEPT;

	return NF_ACCEPT;
}

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */

