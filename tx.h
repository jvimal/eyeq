#ifndef __TX_H__
#define __TX_H__

#include <linux/rculist.h>
#include <linux/types.h>
#include <linux/seq_file.h>
#include <linux/workqueue.h>
#include "rl.h"
#include "rc.h"

/*
 * Classification can be based on skb->dev, src hwaddr, ip, tcp, etc.
 * It is your job to ensure that exactly ONE of the #defines are
 * defined.
 */
// #define ISO_TX_CLASS_ETHER_SRC
// #define ISO_TX_CLASS_DEV
// #define ISO_TX_CLASS_MARK

struct seq_file;

#if defined ISO_TX_CLASS_DEV
typedef struct net_device *iso_class_t;
#elif defined ISO_TX_CLASS_ETHER_SRC
typedef struct {
	u8 addr[ETH_ALEN];
}iso_class_t;
#elif defined ISO_TX_CLASS_MARK
typedef u32 iso_class_t;
#endif

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

	/* Allocate from process context */
	struct work_struct allocator;
};

extern struct hlist_head iso_tx_bucket[ISO_MAX_TX_BUCKETS];

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

inline iso_class_t iso_txc_classify(struct sk_buff *);
inline void iso_class_free(iso_class_t);
inline int iso_class_cmp(iso_class_t a, iso_class_t b);
inline u32 iso_class_hash(iso_class_t);
inline void iso_class_show(iso_class_t, char *);
inline iso_class_t iso_class_parse(char*);
inline struct iso_tx_class *iso_txc_find(iso_class_t);

void iso_state_init(struct iso_per_dest_state *);
struct iso_per_dest_state *iso_state_get(struct iso_tx_class *, struct sk_buff *, int rx);
struct iso_rl *iso_pick_rl(struct iso_tx_class *txc, __le32);
void iso_state_free(struct iso_per_dest_state *);

#endif /* __TX_H__ */

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */

