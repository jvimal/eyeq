
#include <linux/netfilter_bridge.h>
#include <linux/if_ether.h>

#include "tx.h"
#include "vq.h"

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
	struct iso_tx_class *txc;
	struct iso_per_dest_state *state;
	struct iso_rl *rl;
	struct iso_rl_queue *q;
	struct iso_vq *vq;
	enum iso_verdict verdict;

	/* out shouldn't be NULL, but let's be careful anyway */
	if(!out || strcmp(out->name, iso_param_dev) != 0)
		return NF_ACCEPT;

	txc = iso_txc_find(iso_txc_classify(skb));
	if(txc == NULL)
		goto accept;

	state = iso_state_get(txc, skb);
	if(unlikely(state == NULL)) {
		/* printk(KERN_INFO "perfiso: running out of memory!\n"); */
		/* XXX: Temporary: could be an L2 packet... */
		goto accept;
	}

	rl = state->rl;
	vq = txc->vq;

	/* Enqueue in RL */
	verdict = iso_rl_enqueue(rl, skb);
	q = per_cpu_ptr(rl->queue, smp_processor_id());

	if(iso_vq_over_limits(vq))
		q->feedback_backlog++;

	iso_rl_dequeue((unsigned long) q);

	/* If accepted, steal the buffer, else drop it */
	if(verdict == ISO_VERDICT_SUCCESS)
		return NF_STOLEN;

	if(verdict == ISO_VERDICT_DROP)
		return NF_DROP;

 accept:
	return NF_ACCEPT;
}

struct iso_per_dest_state *iso_state_get(struct iso_tx_class *txc, struct sk_buff *skb) {
	struct ethhdr *eth;
	struct iphdr *iph;
	struct iso_per_dest_state *state = NULL;
	struct hlist_head *head;
	struct hlist_node *node;

	u32 dst, hash;

	skb_reset_mac_header(skb);
	eth = eth_hdr(skb);
	__skb_pull(skb, sizeof(struct ethhdr));

	if(unlikely(eth->h_proto != htons(ETH_P_IP))) {
		/* TODO: l2 packet, map all to a single rate state and RL */
		__skb_push(skb, sizeof(struct ethhdr));
		/* Right now, we just pass it thru */
		return NULL;
	}

	skb_reset_network_header(skb);
	iph = ip_hdr(skb);
	dst = ntohl(iph->daddr);
	__skb_push(skb, sizeof(struct ethhdr));

	hash = dst & (ISO_MAX_STATE_BUCKETS - 1);
	head = &txc->state_bucket[hash];

	state = NULL;
	hlist_for_each_entry_rcu(state, node, head, hash_node) {
		if(state->ip_key == dst)
			break;
	}

	if(likely(state != NULL))
		return state;

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if(likely(state != NULL)) {
		state->ip_key = dst;
		state->rl = iso_pick_rl(txc, dst);
	}

	return state;
}

struct iso_rl *iso_pick_rl(struct iso_tx_class *txc, __le32 ip) {
	struct iso_rl *rl = NULL;
	struct hlist_head *head;
	struct hlist_node *node;

	head = &txc->rl_bucket[ip & (ISO_MAX_RL_BUCKETS - 1)];
	hlist_for_each_entry_rcu(rl, node, head, hash_node) {
		if(rl->ip == ip)
			return rl;
	}

	rl = kmalloc(sizeof(*rl), GFP_KERNEL);
	if(likely(rl != NULL)) {
		iso_rl_init(rl);
		rl->ip = ip;
		hlist_add_head_rcu(&rl->hash_node, head);
	}

	return rl;
}

void iso_txc_init(struct iso_tx_class *txc) {
	int i;
	for(i = 0; i < ISO_MAX_RL_BUCKETS; i++)
		INIT_HLIST_HEAD(&txc->rl_bucket[i]);

	for(i = 0; i < ISO_MAX_STATE_BUCKETS; i++)
		INIT_HLIST_HEAD(&txc->state_bucket[i]);

	INIT_LIST_HEAD(&txc->list);
	INIT_HLIST_NODE(&txc->hash_node);
}

static inline struct hlist_head *iso_txc_find_bucket(iso_class_t klass) {
	return &iso_tx_bucket[(((unsigned long)klass) >> 12) & (ISO_MAX_TX_BUCKETS - 1)];
}

struct iso_tx_class *iso_txc_alloc(iso_class_t klass) {
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

	/* Release the class; it could be an interface */
	iso_class_free(txc->klass);

	kfree(txc);
}

/* First attempt: out device classification.  Its address is usually
   aligned, so shift out the zeroes */
inline iso_class_t iso_txc_classify(struct sk_buff *pkt) {
	return pkt->dev;
}

inline void iso_class_free(iso_class_t klass) {
	dev_put((struct net_device *)klass);
}

inline struct iso_tx_class *iso_txc_find(iso_class_t klass) {
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

