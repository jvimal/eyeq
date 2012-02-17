#ifndef __RL_H__
#define __RL_H__

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

#include "params.h"

enum iso_verdict {
	ISO_VERDICT_SUCCESS,
	ISO_VERDICT_DROP,
	ISO_VERDICT_PASS,
};

struct iso_rl_queue {
	struct sk_buff *queue[ISO_MAX_QUEUE_LEN_PKT + 1];
	int head;
	int tail;
	int length;
	int first_pkt_size;

	u64 bytes_enqueued;
	u64 bytes_xmit;
	u64 feedback_backlog;

	u64 tokens;
	spinlock_t spinlock;

	int cpu;
	struct iso_rl *rl;
	struct hrtimer *cputimer;
	struct list_head active_list;
};

struct iso_rl {
	u32 rate;
	spinlock_t spinlock;

	__le32 ip;
	u64 total_tokens;
	u64 accum_xmit;
	u64 accum_enqueued;

	ktime_t last_update_time;

	struct iso_rl_queue __percpu *queue;
	struct hlist_node hash_node;
	struct list_head prealloc_list;

	struct iso_tx_class *txc;
};

/* The per-cpu control block for rate limiters */
struct iso_rl_cb {
	spinlock_t spinlock;
	struct hrtimer timer;
	struct tasklet_struct xmit_timeout;
	struct list_head active_list;
	ktime_t last;
	u64 avg_us;
	int cpu;
};

int iso_rl_prep(void);
void iso_rl_exit(void);
void iso_rl_xmit_tasklet(unsigned long _cb);
extern struct iso_rl_cb __percpu *rlcb;

void iso_rl_init(struct iso_rl *);
void iso_rl_free(struct iso_rl *);
void iso_rl_show(struct iso_rl *, struct seq_file *);
static inline int iso_rl_should_refill(struct iso_rl *);
void iso_rl_clock(struct iso_rl *);
enum iso_verdict iso_rl_enqueue(struct iso_rl *, struct sk_buff *, int cpu);
void iso_rl_dequeue(unsigned long _q);
enum hrtimer_restart iso_rl_timeout(struct hrtimer *);
inline int iso_rl_borrow_tokens(struct iso_rl *, struct iso_rl_queue *);
static inline ktime_t iso_rl_gettimeout(void);
static inline u64 iso_rl_singleq_burst(struct iso_rl *);

inline void skb_xmit(struct sk_buff *skb);

static inline int skb_size(struct sk_buff *skb) {
	return ETH_HLEN + skb->len;
}

#define ISO_ECN_REFLECT_MASK (1 << 3)

static inline int skb_set_feedback(struct sk_buff *skb) {
	struct ethhdr *eth;
	struct iphdr *iph;
	u8 newdscp;

	eth = eth_hdr(skb);
	if(unlikely(eth->h_proto != htons(ETH_P_IP)))
		return 1;

	iph = ip_hdr(skb);
	newdscp = iph->tos | ISO_ECN_REFLECT_MASK;
	ipv4_copy_dscp(newdscp, iph);
	return 0;
}

static inline int skb_has_feedback(struct sk_buff *skb) {
	struct ethhdr *eth;
	struct iphdr *iph;

	eth = eth_hdr(skb);
	if(unlikely(eth->h_proto != htons(ETH_P_IP)))
		return 0;

	iph = ip_hdr(skb);
	return iph->tos & ISO_ECN_REFLECT_MASK;
}

static inline ktime_t iso_rl_gettimeout() {
	return ktime_set(0, ISO_TOKENBUCKET_TIMEOUT_NS << 1);
}


static inline u64 iso_rl_singleq_burst(struct iso_rl *rl) {
	return ((rl->rate * ISO_MAX_BURST_TIME_US) >> 3) / ISO_BURST_FACTOR;
}

static inline int iso_rl_should_refill(struct iso_rl *rl) {
	ktime_t now = ktime_get();
	if(ktime_us_delta(now, rl->last_update_time) > ISO_RL_UPDATE_INTERVAL_US)
		return 1;
	return 0;
}

static inline void iso_rl_accum(struct iso_rl *rl) {
	u64 xmit, queued;
	int i;
	struct iso_rl_queue *q;

	xmit = queued = 0;
	for_each_online_cpu(i) {
		q = per_cpu_ptr(rl->queue, i);
		xmit += q->bytes_xmit;
		queued += q->bytes_enqueued;
	}

	rl->accum_xmit = xmit;
	rl->accum_enqueued = queued;
}

#endif /* __RL_H__ */

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
