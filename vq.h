#include <linux/version.h>
#include <linux/skbuff.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/ktime.h>
#include <net/ip.h>
#include <net/inet_ecn.h>
#include <net/tcp.h>
#include <net/dst.h>
#include <linux/hash.h>
#include <linux/crc16.h>
#include <linux/completion.h>
#include <linux/hrtimer.h>
#include <linux/random.h>
#include <asm/atomic.h>
#include <linux/spinlock.h>

#ifndef __VQ_H__
#define __VQ_H__


#include "params.h"
#include "tx.h"
#include "rx.h"

/*
 * We represent alpha, the fraction of ECN marked packets, as
 * numerator/denominator. We ensure that @numerator is an integer in
 * the range [0, 1 << ECN_ALPHA_FRAC_SHIFT].
 */
#define ECN_ALPHA_FRAC_SHIFT (10)
#define MUL15(x) (((x) << 4) - (x))
#define DIV16(x) ((x) >> 4)
#define EWMA_G16(old, new) DIV16(MUL15(old) + new)

struct iso_vq_stats {
	u64 bytes_queued;
	u64 network_marked;
	u64 rx_packets;
	u64 rx_bytes;

	ktime_t last_feedback_gen_time;
	u32 rx_since_last_feedback;
	u32 rx_marked_since_last_feedback;
};

struct iso_vq {
	u8 enabled;
	u8 is_static;

	u64 rate;
	u64 total_bytes_queued;
	u64 feedback_rate;
	u64 rx_rate;
	u64 last_rx_bytes;
	u64 weight;
	/* Fraction of marked packets = alpha/1024. */
	u32 alpha;

	ktime_t last_update_time, last_borrow_time;

	struct iso_vq_stats __percpu *percpu_stats;
	spinlock_t spinlock;

	iso_class_t klass;
	struct list_head list;
	struct hlist_node hash_node;

	/* The number of tx classes referring to this VQ */
	atomic_t refcnt;
	struct iso_rx_context *rxctx;
};

/*
extern struct list_head vq_list;
extern s64 vq_total_tokens;
extern ktime_t vq_last_update_time, vq_last_check_time;
extern atomic_t vq_active_rate;
extern struct hlist_head vq_bucket[ISO_MAX_VQ_BUCKETS];
*/

#define for_each_vq(vq, ctx) list_for_each_entry_safe(vq, vq_next, &ctx->vq_list, list)
#define ISO_VQ_DEFAULT_RATE_MBPS (100) /* This parameter shouldn't matter */

void iso_vqs_init(struct iso_rx_context *);
void iso_vqs_exit(struct iso_rx_context *);
int iso_vq_init(struct iso_vq *);
struct iso_vq *iso_vq_alloc(iso_class_t, struct iso_rx_context *);
void iso_vq_free(struct iso_vq *);
void iso_vq_enqueue(struct iso_vq *, struct sk_buff *);
void iso_vq_drain(struct iso_vq *, u64);
static inline int iso_vq_over_limits(struct iso_vq *);
void iso_vq_calculate_rates(struct iso_rx_context *);
void iso_vq_check_idle(struct iso_rx_context *);

static inline struct iso_vq *iso_vq_find(iso_class_t, struct iso_rx_context *);
void iso_vq_show(struct iso_vq *, struct seq_file *);

/* Called with rcu lock */
static inline struct iso_vq *iso_vq_find(iso_class_t klass, struct iso_rx_context *rxctx) {
	u32 hash = iso_class_hash(klass);
	struct hlist_head *head = &rxctx->vq_bucket[hash & (ISO_MAX_VQ_BUCKETS - 1)];
	struct iso_vq *vq;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
	struct hlist_node *node;
	hlist_for_each_entry_rcu(vq, node, head, hash_node) {
#else
	hlist_for_each_entry_rcu(vq, head, hash_node) {
#endif
		if(iso_class_cmp(vq->klass, klass) == 0)
			return vq;
	}

	return NULL;
}


static inline int iso_vq_over_limits(struct iso_vq *vq) {
	return vq->feedback_rate;
}


#endif /* __VQ_H__ */

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
