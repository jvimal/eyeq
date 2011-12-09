
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

	seq_printf(s, "txc class %s   assoc vq %s   freelist %d\n", buff, vqc, txc->freelist_count);

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
	enum iso_verdict verdict = ISO_VERDICT_PASS;
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

	iso_rl_dequeue((unsigned long)q);
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

 again:
	state = NULL;
	hlist_for_each_entry_rcu(state, node, head, hash_node) {
		if(state->ip_key == ip)
			break;
	}

	if(likely(state != NULL))
		return state;

	if(spin_trylock(&txc->writelock))
		goto again;

	/* Check again; shouldn't we use a rwlock_t? */
	hlist_for_each_entry_rcu(state, node, head, hash_node) {
		if(state->ip_key == ip)
			break;
	}

	if(unlikely(state != NULL))
		goto unlock;

	list_for_each_entry_rcu(state, &txc->prealloc_state_list, prealloc_list) {
		state->ip_key = ip;
		state->rl = iso_pick_rl(txc, ip);
		if(state->rl == NULL)
			break;

		iso_rc_init(&state->tx_rc);
		INIT_HLIST_NODE(&state->hash_node);
		hlist_add_head_rcu(&state->hash_node, head);

		/* remove from prealloc list */
		list_del_rcu(&state->prealloc_list);
		txc->freelist_count--;
		break;
	}

	/* Do we need to reallocate? */
	if(txc->freelist_count <= 10)
		schedule_work(&txc->allocator);

 unlock:
	spin_unlock(&txc->writelock);
	return state;
}

void iso_state_free(struct iso_per_dest_state *state) {
	free_percpu(state->tx_rc.stats);
	kfree(state);
}

/* Called with txc->writelock */
struct iso_rl *iso_pick_rl(struct iso_tx_class *txc, __le32 ip) {
	struct iso_rl *rl = NULL;
	struct hlist_head *head;
	struct hlist_node *node;
	rcu_read_lock();

	head = &txc->rl_bucket[ip & (ISO_MAX_RL_BUCKETS - 1)];
	hlist_for_each_entry_rcu(rl, node, head, hash_node) {
		if(rl->ip == ip)
			goto found;
	}

	rl = NULL;
	list_for_each_entry_rcu(rl, &txc->prealloc_rl_list, prealloc_list) {
		iso_rl_init(rl);
		rl->ip = ip;
		hlist_add_head_rcu(&rl->hash_node, head);
		/* remove from prealloc list */
		list_del_rcu(&rl->prealloc_list);
		break;
	}

 found:
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
	INIT_LIST_HEAD(&txc->prealloc_state_list);
	INIT_LIST_HEAD(&txc->prealloc_rl_list);

	INIT_HLIST_NODE(&txc->hash_node);
	txc->vq = NULL;
	spin_lock_init(&txc->writelock);
	txc->freelist_count = 0;

	INIT_WORK(&txc->allocator, iso_txc_allocator);
}

void iso_txc_allocator(struct work_struct *work) {
	struct iso_tx_class *txc = container_of(work, struct iso_tx_class, allocator);
	iso_txc_prealloc(txc, 256);
}

static inline struct hlist_head *iso_txc_find_bucket(iso_class_t klass) {
	return &iso_tx_bucket[iso_class_hash(klass) & (ISO_MAX_TX_BUCKETS - 1)];
}

/* Can sleep */
struct iso_tx_class *iso_txc_alloc(iso_class_t klass) {
	struct iso_tx_class *txc;
	struct hlist_head *head;

	txc = kmalloc(sizeof(*txc), GFP_KERNEL);
	if(!txc)
		return NULL;

	iso_txc_init(txc);
	txc->klass = klass;

	rcu_read_lock();

	/* Preallocate some perdest state and rate limiters.  256 entries
	 * ought to be enough for everybody ;) */
	iso_txc_prealloc(txc, 256);

	head = iso_txc_find_bucket(klass);
	hlist_add_head_rcu(&txc->hash_node, head);
	rcu_read_unlock();

	return txc;
}

void iso_state_init(struct iso_per_dest_state *state) {
	state->rl = NULL;
	iso_rc_init(&state->tx_rc);
	INIT_LIST_HEAD(&state->prealloc_list);
	INIT_HLIST_NODE(&state->hash_node);
}

void iso_txc_prealloc(struct iso_tx_class *txc, int num) {
	int i;
	struct iso_per_dest_state *state;
	struct iso_rl *rl;
	unsigned long flags;

	printk(KERN_INFO "Preallocating %d RLs and per-dest-states\n", num);

	for(i = 0; i < num; i++) {
		state = kmalloc(sizeof(*state), GFP_KERNEL);
		if(state == NULL)
			break;

		state->tx_rc.stats = alloc_percpu(struct iso_rc_stats);
		if(state->tx_rc.stats == NULL) {
			kfree(state);
			break;
		}

		rl = kmalloc(sizeof(*rl), GFP_KERNEL);
		if(rl == NULL) {
			free_percpu(state->tx_rc.stats);
			kfree(state);
			break;
		}

		iso_state_init(state);
		iso_rl_init(rl);

		spin_lock_irqsave(&txc->writelock, flags);
		txc->freelist_count++;
		list_add_tail_rcu(&state->prealloc_list, &txc->prealloc_state_list);
		list_add_tail_rcu(&rl->prealloc_list, &txc->prealloc_rl_list);
		spin_unlock_irqrestore(&txc->writelock, flags);
	}
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
			/* XXX: Actually, we shouldn't do this; we have to "call_rcu" */
			iso_rl_free(rl);
		}
	}

	/* Kill each state */
	for(i = 0; i < ISO_MAX_STATE_BUCKETS; i++) {
		head = &txc->state_bucket[i];
		hlist_for_each_entry_rcu(state, n, head, hash_node) {
			hlist_del_init_rcu(&state->hash_node);
			/* XXX: Actually, we shouldn't do this; we have to "call_rcu" */
			iso_state_free(state);
		}
	}

	/* Release the class; it could be an interface */
	iso_class_free(txc->klass);

	/* Free preallocated */
	list_for_each_entry_rcu(rl, &txc->prealloc_rl_list, prealloc_list) {
		list_del_rcu(&rl->prealloc_list);
		/* XXX: Actually, we shouldn't do this; we have to "call_rcu" */
		iso_rl_free(rl);
	}

	list_for_each_entry_rcu(state, &txc->prealloc_state_list, prealloc_list) {
		list_del_rcu(&state->prealloc_list);
		/* XXX: Actually, we shouldn't do this; we have to "call_rcu" */
		iso_state_free(state);
	}

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
#elif defined ISO_TX_CLASS_MARK
inline iso_class_t iso_txc_classify(struct sk_buff *skb) {
	return skb->mark;
}

inline void iso_class_free(iso_class_t klass) {}

inline int iso_class_cmp(iso_class_t a, iso_class_t b) {
	return a - b;
}

/* We don't do any bit mixing here; it's for ease of use */
inline u32 iso_class_hash(iso_class_t klass) {
	return klass;
}

/* Just lazy, looks weird */
inline void iso_class_show(iso_class_t klass, char *buff) {
	sprintf(buff, "%d", klass);
}

inline iso_class_t iso_class_parse(char *hwaddr) {
	int ret = 0;
	sscanf(hwaddr, "%d", &ret);
	return ret;
}
#endif

inline struct iso_tx_class *iso_txc_find(iso_class_t klass) {
	struct hlist_head *head = iso_txc_find_bucket(klass);
	struct iso_tx_class *txc;
	struct iso_tx_class *found = NULL;
	struct hlist_node *n;

	rcu_read_lock();
	hlist_for_each_entry_rcu(txc, n, head, hash_node) {
		if(iso_class_cmp(txc->klass, klass) == 0) {
			found = txc;
			break;
		}
	}
	rcu_read_unlock();

	return found;
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
#elif defined ISO_TX_CLASS_MARK
int iso_txc_mark_install(char *mark) {
	iso_class_t m = iso_class_parse(mark);
	struct iso_tx_class *txc;
	int ret = 0;

	/* Check if we have already created */
	txc = iso_txc_find(m);
	if(txc != NULL) {
		ret = -1;
		goto end;
	}

	txc = iso_txc_alloc(m);
	if(txc == NULL) {
		ret = -1;
		goto end;
	}

 end:
	return ret;
}
#endif

int iso_txc_install(char *klass) {
	int ret;
#if defined ISO_TX_CLASS_DEV
	ret = iso_txc_dev_install(klass);
#elif defined ISO_TX_CLASS_ETHER_SRC
	ret = iso_txc_ether_src_install(klass);
#elif defined ISO_TX_CLASS_MARK
	ret = iso_txc_mark_install(klass);
#endif
	return ret;
}
/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */

