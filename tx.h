#ifndef __TX_H__
#define __TX_H__

#include <linux/rculist.h>
#include <linux/types.h>
#include <linux/seq_file.h>
#include <linux/workqueue.h>
#include "rl.h"
#include "rc.h"

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
	u32 vrate;

	/* Allocate from process context */
	struct work_struct allocator;
};

extern struct hlist_head iso_tx_bucket[ISO_MAX_TX_BUCKETS];
extern struct list_head txc_list;
extern int txc_total_weight;
extern spinlock_t txc_spinlock;

#define for_each_txc(txc) list_for_each_entry_safe(txc, txc_next, &txc_list, list)

int iso_tx_init(void);
void iso_tx_exit(void);

enum iso_verdict iso_tx(struct sk_buff *skb, const struct net_device *out);

void iso_txc_init(struct iso_tx_class *);
struct iso_tx_class *iso_txc_alloc(iso_class_t);
void iso_txc_free(struct iso_tx_class *);
void iso_txc_show(struct iso_tx_class *, struct seq_file *);

#if defined ISO_TX_CLASS_DEV
int iso_txc_dev_install(char *);
#elif defined ISO_TX_CLASS_ETHER_SRC
int iso_txc_ether_src_install(char *hwaddr);
#elif defined ISO_TX_CLASS_MARK
int iso_txc_mark_install(char *mark);
#endif

int iso_txc_install(char *klass);
void iso_txc_prealloc(struct iso_tx_class *, int);
void iso_txc_allocator(struct work_struct *);
inline void iso_txc_tick(void);
static inline void iso_txc_recompute_rates(void);

void iso_state_init(struct iso_per_dest_state *);
struct iso_per_dest_state *iso_state_get(struct iso_tx_class *, struct sk_buff *, int rx);
struct iso_rl *iso_pick_rl(struct iso_tx_class *txc, __le32);
void iso_state_free(struct iso_per_dest_state *);

static inline struct hlist_head *iso_txc_find_bucket(iso_class_t klass) {
	return &iso_tx_bucket[iso_class_hash(klass) & (ISO_MAX_TX_BUCKETS - 1)];
}

static inline struct iso_tx_class *iso_txc_find(iso_class_t klass) {
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

static inline void iso_txc_recompute_rates() {
	struct iso_tx_class *txc, *txc_next;
	unsigned long flags;

	spin_lock_irqsave(&txc_spinlock, flags);
	for_each_txc(txc) {
		txc->vrate = txc->rl.rate = txc->weight * ISO_MAX_TX_RATE / txc_total_weight;
	}
	spin_unlock_irqrestore(&txc_spinlock, flags);
}

#endif /* __TX_H__ */

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */

