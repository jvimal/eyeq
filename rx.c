
#include <linux/netfilter_bridge.h>
#include "tx.h"
#include "rx.h"
#include "vq.h"

extern char *iso_param_dev;
extern struct net_device *iso_netdev;
struct nf_hook_ops hook_in;

int iso_rx_init() {
	printk(KERN_INFO "perfiso: Init RX path\n");
	iso_vqs_init();

	hook_in.hook = iso_rx_bridge;
	hook_in.hooknum= NF_BR_PRE_ROUTING;
	hook_in.pf = PF_BRIDGE;
	hook_in.priority = NF_BR_PRI_FIRST;

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
	struct iso_tx_class *txc;
	iso_class_t klass;
	struct iso_per_dest_state *state;
	struct iso_vq *vq;

	/* out will be NULL if this is PRE_ROUTING */
	if(in != iso_netdev)
		return NF_ACCEPT;

	rcu_read_lock();

	/* Pick VQ */
	klass = iso_rx_classify(skb);
	vq = iso_vq_find(klass);
	if(vq == NULL)
		goto accept;

	iso_vq_enqueue(vq, skb);

	txc = iso_txc_find(klass);
	if(txc == NULL)
		goto accept;

	state = iso_state_get(txc, skb);

 accept:
	rcu_read_unlock();
	return NF_ACCEPT;
}

inline iso_class_t iso_rx_classify(struct sk_buff *skb) {
	/* Classify just like TX context would have */
	iso_class_t klass;
#if defined ISO_TX_CLASS_DEV
	klass = skb->dev;
#elif defined ISO_TX_CLASS_ETHER_SRC
	memcpy((void *)&klass, eth_hdr(skb)->h_dest, ETH_ALEN);
#endif
	return klass;
}


#if defined ISO_TX_CLASS_DEV
int iso_vq_dev_install(char *name) {
	struct iso_vq *vq;
	struct net_device *dev;
	int ret = 0;

	rcu_read_lock();
	dev = dev_get_by_name_rcu(&init_net, name);

	if(dev == NULL) {
		printk(KERN_INFO "perfiso: dev %s not found!\n", name);
		ret = -1;
		goto err;
	}

	/* Check if we have already created */
	vq = iso_vq_find(dev);
	if(vq != NULL) {
		dev_put(dev);
		ret = -1;
		goto err;
	}

	vq = iso_vq_alloc(dev);

	if(vq == NULL) {
		dev_put(dev);
		printk(KERN_INFO "perfiso: Could not allocate vq\n");
		ret = -1;
		goto err;
	}

 err:
	rcu_read_unlock();
	return ret;
}
#elif defined ISO_TX_CLASS_ETHER_SRC
int iso_vq_ether_src_install(char *hwaddr) {
	iso_class_t ether_src;
	int ret = 0;
	struct iso_vq *vq;

	ret = -!mac_pton(hwaddr, (u8*)&ether_src);

	if(ret) {
		printk(KERN_INFO "perfiso: Cannot parse ether address from %s\n", hwaddr);
		goto end;
	}

	/* Check if we have already created */
	vq = iso_vq_find(ether_src);
	if(vq != NULL) {
		ret = -1;
		goto end;
	}

	vq = iso_vq_alloc(ether_src);

	if(vq == NULL) {
		ret = -1;
		goto end;
	}

 end:
	return ret;
}
#endif

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */

