
#include <linux/netfilter_bridge.h>
#include "tx.h"

extern char *iso_param_dev;
struct nf_hook_ops hook_out;
struct hlist_head iso_tx_bucket[ISO_MAX_TX_BUCKETS];

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

void iso_txc_init(struct iso_tx_class *tx) {
	int i;
	for(i = 0; i < ISO_MAX_RL_BUCKETS; i++)
		INIT_HLIST_HEAD(&tx->rl_bucket[i]);

	for(i = 0; i < ISO_MAX_STATE_BUCKETS; i++)
		INIT_HLIST_HEAD(&tx->state_bucket[i]);

	INIT_LIST_HEAD(&tx->list);
	INIT_HLIST_NODE(&tx->hash_node);
}

static inline struct hlist_head *iso_txc_find_bucket(unsigned long klass) {
	return &iso_tx_bucket[klass & (ISO_MAX_TX_BUCKETS - 1)];
}

struct iso_tx_class *iso_txc_alloc(unsigned long klass) {
	struct iso_tx_class *txc = kmalloc(sizeof(*txc), GFP_KERNEL);
	struct hlist_head *head;

	if(!txc)
		return NULL;

	iso_txc_init(txc);
	txc->klass = klass;

	head = iso_txc_find_bucket(klass);
	hlist_add_head_rcu(&txc->hash_node, head);
	return txc;
}

void iso_txc_free(struct iso_tx_class *txc) {
	struct hlist_head *head;
	struct hlist_node *n;
	struct iso_rl *rl;
	struct iso_per_dest_state *state;

	int i;

	hlist_del_init_rcu(&txc->hash_node);

	/* Kill each rate limiter */
	for(i = 0; i < ISO_MAX_RL_BUCKETS; i++) {
		head = &txc->rl_bucket[i];
		hlist_for_each_entry_rcu(rl, n, head, hash_node) {
			hlist_del_init_rcu(&rl->hash_node);
			iso_rl_free(rl);
		}
	}

	/* Kill each state */
	for(i = 0; i < ISO_MAX_STATE_BUCKETS; i++) {
		head = &txc->state_bucket[i];
		hlist_for_each_entry_rcu(state, n, head, hash_node) {
			hlist_del_init_rcu(&state->hash_node);
			// iso_state_free(state);
		}
	}

}

/* First attempt: out device classification.  Its address is usually
   aligned, so shift out the zeroes */
inline unsigned long iso_txc_classify(struct sk_buff *pkt) {
	return ((unsigned long)pkt->dev) >> 10;
}

inline struct iso_tx_class *iso_txc_find(unsigned long klass) {
	struct hlist_head *head = iso_txc_find_bucket(klass);
	struct iso_tx_class *txc;
	struct hlist_node *n;

	hlist_for_each_entry_rcu(txc, n, head, hash_node) {
		if(txc->klass == klass)
			return txc;
	}

	return NULL;
}

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */

