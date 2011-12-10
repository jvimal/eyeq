
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
		q->head = q->tail = q->length = 0;
		q->first_pkt_size = 0;
		q->bytes_enqueued = 0;
		q->feedback_backlog = 0;
		q->tokens = 0;

		spin_lock_init(&q->spinlock);
		tasklet_init(&q->xmit_timeout, iso_rl_dequeue, (unsigned long)q);

		hrtimer_init(&q->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		q->timer.function = iso_rl_timeout;

		q->rl = rl;
	}

	INIT_LIST_HEAD(&rl->prealloc_list);
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

/* Called with rcu lock */
void iso_rl_show(struct iso_rl *rl, struct seq_file *s) {
	struct iso_rl_queue *q;
	int i, first = 1;

	seq_printf(s, "ip %x   rate %llu   total_tokens %llu   last %llx   %p\n",
			   rl->ip, rl->rate, rl->total_tokens, *(u64 *)&rl->last_update_time, rl);

	for_each_online_cpu(i) {
		if(first) {
			seq_printf(s, "\tcpu   head   tail   len"
					   "   first_len   queued   fbacklog   tokens\n");
			first = 0;
		}
		q = per_cpu_ptr(rl->queue, i);

		if(q->tokens > 0 || q->length > 0) {
			seq_printf(s, "\t%3d   %4d   %4d   %3d   %3d   %10llu   %6llu   %10llu\n",
					   i, q->head, q->tail, q->length, q->first_pkt_size,
					   q->bytes_enqueued, q->feedback_backlog, q->tokens);
		}
	}
}

/* This function could be called from HARDIRQ context */
void iso_rl_clock(struct iso_rl *rl) {
	unsigned long flags;
	u64 cap;
	u32 us;
	ktime_t now;

	if(!iso_rl_should_refill(rl))
		return;

	if(!spin_trylock_irqsave(&rl->spinlock, flags))
		return;

	now = ktime_get();
	us = ktime_us_delta(now, rl->last_update_time);
	rl->total_tokens += (rl->rate * us) >> 3;

	/* This is needed if we have TSO.  MIN_BURST_BYTES will be ~64K */
	cap = max((rl->rate * ISO_MAX_BURST_TIME_US) >> 3, (u64)ISO_MIN_BURST_BYTES);
	rl->total_tokens = min(cap, rl->total_tokens);

	rl->last_update_time = now;

	spin_unlock_irqrestore(&rl->spinlock, flags);
}

enum iso_verdict iso_rl_enqueue(struct iso_rl *rl, struct sk_buff *pkt, int cpu) {
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
	q->bytes_enqueued += skb_size(pkt);

	verdict = ISO_VERDICT_SUCCESS;
	goto done;

 drop:
	verdict = ISO_VERDICT_DROP;

 done:
	spin_unlock(&q->spinlock);
	return verdict;
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

	if(unlikely(q->tokens < q->first_pkt_size)) {
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
	q->first_pkt_size = size;
	timeout = 1;

	while(sum <= q->tokens) {
		q->tokens -= size;
		q->head = (q->head + 1) & ISO_MAX_QUEUE_LEN_PKT;
		q->length--;
		q->bytes_enqueued -= size;

		if(q->feedback_backlog) {
			if(!skb_set_feedback(pkt))
				q->feedback_backlog = 0;
		}

		skb_xmit(pkt);

		if(q->length == 0) {
			timeout = 0;
			break;
		}

		pkt = q->queue[q->head];
		sum += (size = skb_size(pkt));
		q->first_pkt_size = size;
	}

unlock:
	spin_unlock(&q->spinlock);
	if(timeout) {
		hrtimer_start(&q->timer, iso_rl_gettimeout(), HRTIMER_MODE_REL);
	}
}

/* HARDIRQ timeout */
enum hrtimer_restart iso_rl_timeout(struct hrtimer *timer) {
	/* schedue xmit tasklet to go into softirq context */
	struct iso_rl_queue *q = container_of(timer, struct iso_rl_queue, timer);
	iso_rl_clock(q->rl);
	tasklet_schedule(&q->xmit_timeout);
	return HRTIMER_NORESTART;
}

int iso_rl_borrow_tokens(struct iso_rl *rl, struct iso_rl_queue *q) {
	unsigned long flags;
	int timeout = 1;
	u64 borrow, cap;

	spin_lock_irqsave(&rl->spinlock, flags);
	borrow = max(iso_rl_singleq_burst(rl), (u64)q->first_pkt_size);

	if(rl->total_tokens > borrow) {
		rl->total_tokens -= borrow;
		q->tokens += borrow;
		cap = max((rl->rate * ISO_MAX_BURST_TIME_US) >> 3, (u64)ISO_MIN_BURST_BYTES);
		q->tokens = min(cap, q->tokens);

		timeout = 0;
	}

	spin_unlock_irqrestore(&rl->spinlock, flags);
	return timeout;
}

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
