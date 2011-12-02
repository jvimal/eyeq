
#include <linux/netfilter_bridge.h>
#include "tx.h"

extern char *iso_param_dev;
struct nf_hook_ops hook_out;

int iso_tx_init() {
	printk(KERN_INFO "perfiso: Init TX path\n");
	hook_out.hook = iso_tx_bridge;
	hook_out.hooknum= NF_BR_POST_ROUTING;
	hook_out.pf = PF_BRIDGE;
	hook_out.priority = NF_BR_PRI_BRNF;

	return nf_register_hook(&hook_out);
}

void iso_tx_exit() {
	nf_unregister_hook(&hook_out);
}

unsigned int iso_tx_bridge(unsigned int hooknum,
			struct sk_buff *skb,
			const struct net_device *in,
			const struct net_device *out,
			int (*okfn)(struct sk_buff *))
{
	/* out shouldn't be NULL, but let's be careful anyway */
	if(!out || strcmp(out->name, iso_param_dev) != 0)
		return NF_ACCEPT;

	return NF_ACCEPT;
}

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */

