
#include <linux/netfilter_bridge.h>
#include <linux/if_ether.h>
#include "tx.h"
#include "vq.h"

extern char *iso_param_dev;
extern struct net_device *iso_netdev;
struct hlist_head iso_tx_bucket[ISO_MAX_TX_BUCKETS];

int iso_tx_bridge_init(void);
void iso_tx_bridge_exit(void);

int iso_tx_init() {
	printk(KERN_INFO "perfiso: Init TX path\n");

	return iso_tx_bridge_init();
}

void iso_tx_exit() {
	int i;
	struct hlist_head *head;
	struct hlist_node *node;
	struct iso_tx_class *txc;

	iso_tx_bridge_exit();

	rcu_read_lock();

	for(i = 0; i < ISO_MAX_TX_BUCKETS; i++) {
		head = &iso_tx_bucket[i];
		hlist_for_each_entry_rcu(txc, node, head, hash_node) {
			iso_txc_free(txc);
		}
	}

	rcu_read_unlock();
}

/* Called with rcu lock */
void iso_txc_show(struct iso_tx_class *txc, struct seq_file *s) {
	int i, nth;
	struct hlist_node *node;
	struct hlist_head *head;
	struct iso_rl *rl;
	struct iso_per_dest_state *state;

	char buff[128];
	char vqc[128];

	iso_class_show(txc->klass, buff);
	if(txc->vq) {
		iso_class_show(txc->vq->klass, vqc);
	} else {
		sprintf(vqc, "(none)");
	}

	seq_printf(s, "txc klass %s   assoc vq %s\n", buff, vqc);

	seq_printf(s, "per dest state:\n");
	for(i = 0; i < ISO_MAX_STATE_BUCKETS; i++) {
		head = &txc->state_bucket[i];
		hlist_for_each_entry_rcu(state, node, head, hash_node) {
			seq_printf(s, "ip %x   rl %p\n", state->ip_key, state->rl);
			iso_rc_show(&state->tx_rc, s);
		}
	}

	seq_printf(s, "rate limiters:\n");
	for(i = 0; i < ISO_MAX_RL_BUCKETS; i++) {
		head = &txc->rl_bucket[i];
		nth = 0;

		hlist_for_each_entry_rcu(rl, node, head, hash_node) {
			if(nth == 0) {
				seq_printf(s, "%d ", i);
			}
			iso_rl_show(rl, s);
			nth++;
		}
	}
	seq_printf(s, "\n");
}

enum iso_verdict iso_tx(struct sk_buff *skb, const struct net_device *out)
{
	struct iso_tx_class *txc;
	struct iso_per_dest_state *state;
	struct iso_rl *rl;
	struct iso_rl_queue *q;
	struct iso_vq *vq;
	enum iso_verdict verdict;
	int cpu = smp_processor_id();

	rcu_read_lock();
	txc = iso_txc_find(iso_txc_classify(skb));
	if(txc == NULL)
		goto accept;

	state = iso_state_get(txc, skb, 0);
	if(unlikely(state == NULL)) {
		/* printk(KERN_INFO "perfiso: running out of memory!\n"); */
		/* XXX: Temporary: could be an L2 packet... */
		goto accept;
	}

	rl = state->rl;
	vq = txc->vq;

	/* Enqueue in RL */
	verdict = iso_rl_enqueue(rl, skb, cpu);
	q = per_cpu_ptr(rl->queue, cpu);

	if(likely(vq)) {
		if(iso_vq_over_limits(vq))
			q->feedback_backlog++;
	}

	tasklet_schedule(&q->xmit_timeout);
 accept:
	rcu_read_unlock();
	return verdict;
}

/* Called with rcu lock */
struct iso_per_dest_state
*iso_state_get(struct iso_tx_class *txc,
			   struct sk_buff *skb,
			   int rx)
{
	struct ethhdr *eth;
	struct iphdr *iph;
	struct iso_per_dest_state *state = NULL;
	struct hlist_head *head;
	struct hlist_node *node;

	u32 ip, hash;

	eth = eth_hdr(skb);

	if(unlikely(eth->h_proto != htons(ETH_P_IP))) {
		/* TODO: l2 packet, map all to a single rate state and RL */
		/* Right now, we just pass it thru */
		return NULL;
	}

	iph = ip_hdr(skb);

	ip = ntohl(iph->daddr);
	if(rx) ip = ntohl(iph->saddr);

	hash = ip & (ISO_MAX_STATE_BUCKETS - 1);
	head = &txc->state_bucket[hash];

	state = NULL;
	hlist_for_each_entry_rcu(state, node, head, hash_node) {
		if(state->ip_key == ip)
			break;
	}

	if(likely(state != NULL))
		return state;

	state = kmalloc(sizeof(*state), GFP_ATOMIC);
	if(likely(state != NULL)) {
		state->ip_key = ip;
		state->rl = iso_pick_rl(txc, ip);
		iso_rc_init(&state->tx_rc);
		INIT_HLIST_NODE(&state->hash_node);
		hlist_add_head_rcu(&state->hash_node, head);
	}

	return state;
}

void iso_state_free(struct iso_per_dest_state *state) {
	kfree(state);
}

struct iso_rl *iso_pick_rl(struct iso_tx_class *txc, __le32 ip) {
	struct iso_rl *rl = NULL;
	struct hlist_head *head;
	struct hlist_node *node;
	rcu_read_lock();

	head = &txc->rl_bucket[ip & (ISO_MAX_RL_BUCKETS - 1)];
	hlist_for_each_entry_rcu(rl, node, head, hash_node) {
		if(rl->ip == ip)
			return rl;
	}

	rl = kmalloc(sizeof(*rl), GFP_ATOMIC);
	if(likely(rl != NULL)) {
		iso_rl_init(rl);
		rl->ip = ip;
		hlist_add_head_rcu(&rl->hash_node, head);
	}
	rcu_read_unlock();

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
	txc->vq = NULL;
}

static inline struct hlist_head *iso_txc_find_bucket(iso_class_t klass) {
	return &iso_tx_bucket[iso_class_hash(klass) & (ISO_MAX_TX_BUCKETS - 1)];
}

struct iso_tx_class *iso_txc_alloc(iso_class_t klass) {
	struct iso_tx_class *txc = kmalloc(sizeof(*txc), GFP_KERNEL);
	struct hlist_head *head;

	if(!txc)
		return NULL;

	iso_txc_init(txc);
	txc->klass = klass;
	rcu_read_lock();
	head = iso_txc_find_bucket(klass);
	hlist_add_head_rcu(&txc->hash_node, head);
	rcu_read_unlock();
	return txc;
}

/* Called with rcu lock */
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
			iso_state_free(state);
		}
	}

	/* Release the class; it could be an interface */
	iso_class_free(txc->klass);

	if(txc->vq) {
		atomic_dec(&txc->vq->refcnt);
	}

	kfree(txc);
}

/* First attempt: out device classification.  Its address is usually
   aligned, so shift out the zeroes */

#if defined ISO_TX_CLASS_DEV
inline iso_class_t iso_txc_classify(struct sk_buff *pkt) {
	return pkt->dev;
}

inline void iso_class_free(iso_class_t klass) {
	dev_put((struct net_device *)klass);
}

inline int iso_class_cmp(iso_class_t a, iso_class_t b) {
	return (u64)a - (u64)b;
}

inline u32 iso_class_hash(iso_class_t klass) {
	return (u32) ((u64)klass >> 12);
}

inline void iso_class_show(iso_class_t klass, char *buff) {
	sprintf(buff, "%p", klass);
}

/* XXX: should this routine do "dev_put" as well?  Maybe it should, as
 *  we will not be dereferencing iso_class_t anyway?
 */

inline iso_class_t iso_class_parse(char *devname) {
	struct net_device *dev;

	rcu_read_lock();
	dev = dev_get_by_name_rcu(&init_net, devname);
	rcu_read_unlock();

	dev_put(dev);
	return dev;
}
#elif defined ISO_TX_CLASS_ETHER_SRC
inline iso_class_t iso_txc_classify(struct sk_buff *skb) {
	iso_class_t ret;
	memcpy((unsigned char *)&ret, eth_hdr(skb)->h_source, ETH_ALEN);
	return ret;
}

inline void iso_class_free(iso_class_t klass) {}

inline int iso_class_cmp(iso_class_t a, iso_class_t b) {
	return memcmp(&a, &b, sizeof(a));
}

inline u32 iso_class_hash(iso_class_t klass) {
	return jhash((void *)&klass, sizeof(iso_class_t), 0);
}

/* Just lazy, looks weird */
inline void iso_class_show(iso_class_t klass, char *buff) {
#define O "%02x:"
#define OO "%02x"
	u8 *o = &klass.addr[0];
	sprintf(buff, O O O O O OO,
			o[0], o[1], o[2], o[3], o[4], o[5]);
#undef O
#undef OO
}

inline iso_class_t iso_class_parse(char *hwaddr) {
	iso_class_t ret = {.addr = {0}};
	mac_pton(hwaddr, (u8*)&ret);
	return ret;
}
#endif

inline struct iso_tx_class *iso_txc_find(iso_class_t klass) {
	struct hlist_head *head = iso_txc_find_bucket(klass);
	struct iso_tx_class *txc;
	struct hlist_node *n;

	rcu_read_lock();
	hlist_for_each_entry_rcu(txc, n, head, hash_node) {
		if(iso_class_cmp(txc->klass, klass) == 0)
			return txc;
	}
	rcu_read_unlock();

	return NULL;
}

#if defined ISO_TX_CLASS_DEV
int iso_txc_dev_install(char *name) {
	struct iso_tx_class *txc;
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
	txc = iso_txc_find(dev);
	if(txc != NULL) {
		dev_put(dev);
		ret = -1;
		goto err;
	}

	txc = iso_txc_alloc(dev);

	if(txc == NULL) {
		dev_put(dev);
		printk(KERN_INFO "perfiso: Could not allocate tx context\n");
		ret = -1;
		goto err;
	}

 err:
	rcu_read_unlock();
	return ret;
}
#elif defined ISO_TX_CLASS_ETHER_SRC
int iso_txc_ether_src_install(char *hwaddr) {
	iso_class_t ether_src;
	int ret = 0;
	struct iso_tx_class *txc;

	ret = -!mac_pton(hwaddr, (u8*)&ether_src);

	if(ret) {
		printk(KERN_INFO "perfiso: Cannot parse ether address from %s\n", hwaddr);
		goto end;
	}

	/* Check if we have already created */
	txc = iso_txc_find(ether_src);
	if(txc != NULL) {
		ret = -1;
		goto end;
	}

	txc = iso_txc_alloc(ether_src);

	if(txc == NULL) {
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

