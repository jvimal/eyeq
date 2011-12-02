
#include "rl.h"

void iso_rl_init(struct iso_rl *rl) {
	int i;
	rl->rate = ISO_RFAIR_INITIAL;
	rl->total_tokens = 1;
	rl->last_update_time = ktime_get();
	rl->queue = alloc_percpu(struct iso_rl_queue);
	spin_lock_init(&rl->spinlock);

	for_each_possible_cpu(i) {
		struct iso_rl_queue *q = per_cpu_ptr(rl->queue, i);
		q->head = q->tail = 0;
		q->bytes_enqueued = 0;
		q->tokens = 0;

		spin_lock_init(&q->spinlock);
		tasklet_init(&q->xmit_timeout, iso_rl_dequeue, (unsigned long)q);

		hrtimer_init(&q->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		q->timer.function = iso_rl_timeout;

		q->rl = rl;
	}
}

void iso_rl_free(struct iso_rl *rl) {
	int i;
	for_each_possible_cpu(i) {
		struct iso_rl_queue *q = per_cpu_ptr(rl->queue, i);
		hrtimer_cancel(&q->timer);
		tasklet_kill(&q->xmit_timeout);
		/* TODO: Flush the queue? */
	}
	free_percpu(rl->queue);
	kfree(rl);
}

static int iso_rl_should_refill(struct iso_rl *rl) {
	ktime_t now = ktime_get();
	if(ktime_us_delta(now, rl->last_update_time) > ISO_RL_UPDATE_INTERVAL_US)
		return 1;
	return 0;
}

inline u64 iso_rl_cap_tokens(u64 tokens) {
	return min(tokens, (u64)ISO_MIN_BURST_BYTES);
}

/* This function could be called from HARDIRQ context */
void iso_rl_clock(struct iso_rl *rl) {
	unsigned long flags;
	u32 us;
	ktime_t now;

	if(!iso_rl_should_refill(rl))
		return;

	if(!spin_trylock_irqsave(&rl->spinlock, flags))
		return;

	us = ktime_us_delta(now, rl->last_update_time);
	rl->total_tokens += iso_rl_cap_tokens((rl->rate * us) >> 3);
	rl->last_update_time = now;

	spin_unlock_irqrestore(&rl->spinlock, flags);
}

enum iso_verdict iso_rl_enqueue(struct iso_rl *rl, struct sk_buff *pkt) {
	// enqueue to cpu's queue
	int cpu = smp_processor_id();
	struct iso_rl_queue *q = per_cpu_ptr(rl->queue, cpu);
	enum iso_verdict verdict;

	iso_rl_clock(rl);

	spin_lock(&q->spinlock);
	if(q->length == ISO_MAX_QUEUE_LEN_PKT+1) {
		goto drop;
	}

	q->queue[q->tail++] = pkt;
	q->tail = q->tail & ISO_MAX_QUEUE_LEN_PKT;
	q->length++;
	verdict = ISO_VERDICT_SUCCESS;
	goto done;

 drop:
	verdict = ISO_VERDICT_DROP;

 done:
	spin_unlock(&q->spinlock);
	iso_rl_dequeue((unsigned long)q);
	return verdict;
}

inline ktime_t iso_rl_gettimeout() {
	return ktime_set(0, ISO_TOKENBUCKET_TIMEOUT_NS);
}

/* This function MUST be executed with interrupts enabled */
void iso_rl_dequeue(unsigned long _q) {
	int timeout = 0;
	u64 sum = 0;
	u32 size;
	struct sk_buff *pkt;
	struct iso_rl_queue *q = (struct iso_rl_queue *)_q;
	struct iso_rl *rl = q->rl;

	/* Try to borrow from the global token pool; if that fails,
	   program the timeout for this queue */

	if(unlikely(q->tokens == 0)) {
		timeout = iso_rl_borrow_tokens(rl, q);
		if(timeout) {
			hrtimer_start(&q->timer, iso_rl_gettimeout(), HRTIMER_MODE_REL);
			return;
		}
	}

	/* Some other thread is trying to dequeue, so let's not spin unnecessarily */
	if(unlikely(!spin_trylock(&q->spinlock)))
		return;

	if(q->length == 0)
		goto unlock;

	pkt = q->queue[q->head];
	sum = size = skb_size(pkt);

	while(sum < q->tokens) {
		q->tokens -= size;
		q->head = (q->head + 1) & ISO_MAX_QUEUE_LEN_PKT;
		q->length--;
		skb_xmit(pkt);

		if(q->length == 0)
			break;

		pkt = q->queue[q->head];
		sum += (size = skb_size(pkt));
	}

unlock:
	spin_unlock(&q->spinlock);
}

/* HARDIRQ timeout */
enum hrtimer_restart iso_rl_timeout(struct hrtimer *timer) {
	/* schedue xmit tasklet to go into softirq context */
	struct iso_rl_queue *q = container_of(timer, struct iso_rl_queue, timer);
	iso_rl_clock(q->rl);
	tasklet_schedule(&q->xmit_timeout);
	return HRTIMER_NORESTART;
}

inline u64 iso_rl_singleq_burst(struct iso_rl *rl) {
	return rl->rate * ISO_MAX_BURST_TIME_US / ISO_BURST_FACTOR;
}

int iso_rl_borrow_tokens(struct iso_rl *rl, struct iso_rl_queue *q) {
	unsigned long flags;
	int timeout = 1;
	spin_lock_irqsave(&rl->spinlock, flags);

	if(rl->total_tokens > iso_rl_singleq_burst(rl)) {
		rl->total_tokens -= iso_rl_singleq_burst(rl);
		q->tokens += iso_rl_singleq_burst(rl);
		timeout = 0;
	}

	spin_unlock_irqrestore(&rl->spinlock, flags);
	return timeout;
}

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
