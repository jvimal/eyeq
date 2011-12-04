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
	ISO_VERDICT_DROP
};

struct iso_rl_queue {
	struct sk_buff *queue[ISO_MAX_QUEUE_LEN_PKT + 1];
	int head;
	int tail;
	int length;
	int first_pkt_size;

	u64 bytes_enqueued;
	u64 feedback_backlog;

	u64 tokens;
	spinlock_t spinlock;
	struct tasklet_struct xmit_timeout;
	struct hrtimer timer;
	struct iso_rl *rl;
};

struct iso_rl {
	__le32 ip;
	u64 rate;
	u64 total_tokens;
	ktime_t last_update_time;
	spinlock_t spinlock;

	struct iso_rl_queue __percpu *queue;
	struct hlist_node hash_node;
};

void iso_rl_init(struct iso_rl *);
void iso_rl_free(struct iso_rl *);
void iso_rl_show(struct iso_rl *, struct seq_file *);
inline int iso_rl_should_refill(struct iso_rl *);
void iso_rl_clock(struct iso_rl *);
enum iso_verdict iso_rl_enqueue(struct iso_rl *, struct sk_buff *, int cpu);
void iso_rl_dequeue(unsigned long _q);
enum hrtimer_restart iso_rl_timeout(struct hrtimer *);
int iso_rl_borrow_tokens(struct iso_rl *, struct iso_rl_queue *);
inline ktime_t iso_rl_gettimeout(void);
inline u64 iso_rl_singleq_burst(struct iso_rl *);

inline void skb_xmit(struct sk_buff *skb);

static inline int skb_size(struct sk_buff *skb) {
	return skb->len;
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

#endif /* __RL_H__ */

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
