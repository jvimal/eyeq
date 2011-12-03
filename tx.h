
#include <linux/rculist.h>
#include <linux/types.h>

#include "rl.h"

typedef struct net_device *iso_class_t;

/* Per-dest state */
struct iso_per_dest_state {
	u32 ip_key;
	struct iso_rl *rl;
	struct iso_vq *vq;

	/* Tx and Rx state = stats + control variables */
	struct {
		u64 rfair;
		u64 alpha;

		ktime_t last_rfair_change_time;
		ktime_t last_rfair_decrease_time;
		ktime_t last_feedback_time;
	} tx;

	struct hlist_node hash_node;
};

/* The unit of fairness */
struct iso_tx_class {
	iso_class_t klass;
	struct hlist_head rl_bucket[ISO_MAX_RL_BUCKETS];
	struct hlist_head state_bucket[ISO_MAX_STATE_BUCKETS];

	struct list_head list;
	struct hlist_node hash_node;
};

extern struct hlist_head iso_tx_bucket[ISO_MAX_TX_BUCKETS];

int iso_tx_init(void);
void iso_tx_exit(void);
unsigned int iso_tx_bridge(unsigned int hooknum,
						   struct sk_buff *skb,
						   const struct net_device *in,
						   const struct net_device *out,
						   int (*okfn)(struct sk_buff *));


void iso_txc_init(struct iso_tx_class *);
struct iso_tx_class *iso_txc_alloc(iso_class_t);
void iso_txc_free(struct iso_tx_class *);

inline iso_class_t iso_txc_classify(struct sk_buff *);
inline void iso_class_free(iso_class_t);
inline struct iso_tx_class *iso_txc_find(iso_class_t);

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */

