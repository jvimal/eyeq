#ifndef __TX_H__
#define __TX_H__

#include <linux/rculist.h>
#include <linux/types.h>
#include <linux/seq_file.h>
#include <linux/workqueue.h>
#include "rl.h"
#include "rc.h"

#ifdef QDISC
#include <net/pkt_sched.h>
#include "qdisc.h"
#endif

struct seq_file;

#include "class.h"

/* Per-dest state */
struct iso_per_dest_state {
	u32 ip_key;
	struct iso_rl *rl;

	/* Tx and Rx state = stats + control variables */
	struct iso_rc_state tx_rc;
	struct hlist_node hash_node;
	struct list_head prealloc_list;
};

/* The unit of fairness */
struct iso_tx_class {
	iso_class_t klass;

	struct hlist_head rl_bucket[ISO_MAX_RL_BUCKETS];
	struct hlist_head state_bucket[ISO_MAX_STATE_BUCKETS];

	struct iso_vq *vq;
	struct list_head list;
	struct hlist_node hash_node;
	spinlock_t writelock;

	struct list_head prealloc_state_list;
	struct list_head prealloc_rl_list;
	int freelist_count;

	/* Rate limiter assigned to this TX class as a whole */
	struct iso_rl rl;
	int weight;
	int active;
	u32 tx_rate, tx_rate_smooth;
	u32 min_rate;

	/* Allocate from process context */
	struct work_struct allocator;
	struct iso_tx_context *txctx;
};

/*
 * Create one per device.  We do not support multiple interface
 * fairness yet.
 */
struct iso_tx_context {
	struct net_device *netdev;
	struct iso_rl_cb __percpu *rlcb;
	int __prev_ISO_GSO_MAX_SIZE;
	netdev_tx_t (*xmit)(struct sk_buff *, struct net_device *);
	/* Store a list of all tx contexts (devices) in the system */
	struct list_head list;

	struct hlist_head iso_tx_bucket[ISO_MAX_TX_BUCKETS];
	struct list_head txc_list;
	ktime_t txc_last_update_time;
	int txc_total_weight;
	spinlock_t txc_spinlock;

	u64 tx_bytes;
	u32 tx_rate;
	/* RCP state */
	u32 rate;
};

enum iso_create_t {
	ISO_DONT_CREATE_RL = 0,
	ISO_CREATE_RL = 1,
};

//extern struct hlist_head iso_tx_bucket[ISO_MAX_TX_BUCKETS];
//extern struct list_head txc_list;
extern struct list_head txctx_list;
//extern int txc_total_weight;
//extern spinlock_t txc_spinlock;

struct iso_tx_context *iso_txctx_dev(const struct net_device *dev);
#ifndef QDISC
extern struct iso_tx_context global_txcontext;
#endif

#define for_each_txc(txc, context) list_for_each_entry_safe(txc, txc_next, &context->txc_list, list)
#define for_each_tx_context(txctx) list_for_each_entry_safe(txctx, txctx_next, &txctx_list, list)

int iso_tx_init(struct iso_tx_context *);
void iso_tx_exit(struct iso_tx_context *);

enum iso_verdict iso_tx(struct sk_buff *skb, const struct net_device *out, struct iso_tx_context *);

void iso_txc_init(struct iso_tx_class *);
struct iso_tx_class *iso_txc_alloc(iso_class_t, struct iso_tx_context *);
void iso_txc_free(struct iso_tx_class *);
void iso_txc_show(struct iso_tx_class *, struct seq_file *);

#if defined ISO_TX_CLASS_DEV
int iso_txc_dev_install(char *);
#elif defined ISO_TX_CLASS_ETHER_SRC
int iso_txc_ether_src_install(char *hwaddr);
#elif defined ISO_TX_CLASS_MARK
int iso_txc_mark_install(char *mark);
#endif

int iso_txc_install(char *klass, struct iso_tx_context *);
void iso_txc_prealloc(struct iso_tx_class *, int);
void iso_txc_allocator(struct work_struct *);
inline void iso_txc_tick(struct iso_tx_context *);
static inline void iso_txc_recompute_rates(struct iso_tx_context *);

void iso_state_init(struct iso_per_dest_state *);
struct iso_per_dest_state *iso_state_get(struct iso_tx_class *, struct sk_buff *, int rx, enum iso_create_t);
struct iso_rl *iso_pick_rl(struct iso_tx_class *txc, __le32);
void iso_state_free(struct iso_per_dest_state *);

static inline struct hlist_head *iso_txc_find_bucket(iso_class_t klass, struct iso_tx_context *context) {
	return &context->iso_tx_bucket[iso_class_hash(klass) & (ISO_MAX_TX_BUCKETS - 1)];
}

static inline struct iso_tx_class *iso_txc_find(iso_class_t klass, struct iso_tx_context *context) {
	struct hlist_head *head = iso_txc_find_bucket(klass, context);
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

static inline void iso_txc_recompute_rates(struct iso_tx_context *context) {
	struct iso_tx_class *txc, *txc_next;
	unsigned long flags;

	if (context->txc_total_weight == 0) {
		printk(KERN_INFO "%s warning: context has zero weight.\n", __FUNCTION__);
		return;
	}

	spin_lock_irqsave(&context->txc_spinlock, flags);
	for_each_txc(txc, context) {
		txc->min_rate = txc->weight * ISO_MAX_TX_RATE / context->txc_total_weight;
		txc->rl.rate = txc->min_rate;
	}
	spin_unlock_irqrestore(&context->txc_spinlock, flags);
}

static inline void iso_enable_ecn(struct sk_buff *skb)
{
	struct iphdr *iph = ip_hdr(skb);
	ipv4_change_dsfield(iph, 0xff, iph->tos | INET_ECN_ECT_0);
}

static inline void iso_clear_ecn(struct sk_buff *skb)
{
	struct iphdr *iph = ip_hdr(skb);
	ipv4_change_dsfield(iph, 0xff, iph->tos & ~0x3);
}

#endif /* __TX_H__ */

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */

