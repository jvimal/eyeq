
#include "rl.h"
#include "tx.h"

static struct iso_rl_cb __percpu *rlcb;

/* Called the first time when the module is initialised */
int iso_rl_prep() {
	int cpu;

	rlcb = alloc_percpu(struct iso_rl_cb);
	if(rlcb == NULL)
		return -1;

	/* Init everything; but what about hotplug?  Hmm... */
	for_each_possible_cpu(cpu) {
		struct iso_rl_cb *cb = per_cpu_ptr(rlcb, cpu);
		hrtimer_init(&cb->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		cb->timer.function = iso_rl_timeout;

		tasklet_init(&cb->xmit_timeout, iso_rl_xmit_tasklet, (unsigned long)cb);
		INIT_LIST_HEAD(&cb->active_list);

		cb->last = ktime_get();
		cb->avg_us = 0;
		cb->cpu = cpu;
		cb->active = 0;
	}

	return 0;
}

void iso_rl_exit() {
	int cpu;

	for_each_possible_cpu(cpu) {
		struct iso_rl_cb *cb = per_cpu_ptr(rlcb, cpu);
		hrtimer_cancel(&cb->timer);
		tasklet_kill(&cb->xmit_timeout);
	}

	free_percpu(rlcb);
}

void iso_rl_xmit_tasklet(unsigned long _cb) {
	struct iso_rl_cb *cb = (struct iso_rl_cb *)_cb;
	struct iso_rl_queue *q, *qtmp;
	ktime_t last;
	ktime_t dt;

	/* This block is not needed, but just for debugging purposes */
	last = cb->last;
	cb->last = ktime_get();
	cb->avg_us = ktime_us_delta(cb->last, last);

	list_for_each_entry_safe(q, qtmp, &cb->active_list, active_list) {
		list_del_init(&q->active_list);
		iso_rl_clock(q->rl);
		iso_rl_dequeue((unsigned long)q);
	}

	if(!list_empty(&cb->active_list) && !iso_exiting) {
		dt = iso_rl_gettimeout();
		hrtimer_start(&cb->timer, dt, HRTIMER_MODE_REL);
	} else {
		cb->active = 0;
	}
}

void iso_rl_init(struct iso_rl *rl) {
	int i;
	rl->rate = ISO_RFAIR_INITIAL;
	rl->total_tokens = 15000;
	rl->last_update_time = ktime_get();
	rl->queue = alloc_percpu(struct iso_rl_queue);
	rl->accum_xmit = 0;
	rl->accum_enqueued = 0;
	spin_lock_init(&rl->spinlock);

	for_each_possible_cpu(i) {
		struct iso_rl_queue *q = per_cpu_ptr(rl->queue, i);
		struct iso_rl_cb *cb = per_cpu_ptr(rlcb, i);

		q->head = q->tail = q->length = 0;
		q->first_pkt_size = 0;
		q->bytes_enqueued = 0;
		q->bytes_xmit = 0;

		q->feedback_backlog = 0;
		q->tokens = 0;

		spin_lock_init(&q->spinlock);

		q->cpu = i;
		q->rl = rl;
		q->cputimer = &cb->timer;

		INIT_LIST_HEAD(&q->active_list);
	}

	INIT_LIST_HEAD(&rl->prealloc_list);
	rl->txc = NULL;
}

void iso_rl_free(struct iso_rl *rl) {
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
	u64 cap, us;
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
	struct iso_rl_queue *rootq;
	struct iso_rl *rl = q->rl;
	enum iso_verdict verdict;

	/* Try to borrow from the global token pool; if that fails,
	   program the timeout for this queue */

	if(unlikely(q->tokens < q->first_pkt_size)) {
		timeout = iso_rl_borrow_tokens(rl, q);
		if(timeout)
			goto timeout;
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

		if(rl->txc == NULL) {
			skb_xmit(pkt);
			q->bytes_xmit += size;
		} else {
			/* Enqueue in parent tx class's rate limiter */
			verdict = iso_rl_enqueue(&rl->txc->rl, pkt, q->cpu);
			if(verdict == ISO_VERDICT_DROP)
				kfree_skb(pkt);
		}

		if(q->length == 0) {
			timeout = 0;
			break;
		}

		pkt = q->queue[q->head];
		sum += (size = skb_size(pkt));
		q->first_pkt_size = size;
	}

unlock:
	if(rl->txc != NULL) {
		rootq = per_cpu_ptr(rl->txc->rl.queue, q->cpu);
		iso_rl_dequeue((unsigned long)rootq);
	}

	spin_unlock(&q->spinlock);

timeout:
	if(timeout) {
		struct iso_rl_cb *cb = per_cpu_ptr(rlcb, q->cpu);
		/* don't recursively add! */
		if(list_empty(&q->active_list))
			list_add(&q->active_list, &cb->active_list);
		if(!cb->active)
			hrtimer_start(&cb->timer, iso_rl_gettimeout(), HRTIMER_MODE_REL);
	}
}

/* HARDIRQ timeout */
enum hrtimer_restart iso_rl_timeout(struct hrtimer *timer) {
	/* schedue xmit tasklet to go into softirq context */
	struct iso_rl_cb *cb = container_of(timer, struct iso_rl_cb, timer);
	cb->active = 1;
	tasklet_schedule(&cb->xmit_timeout);
	return HRTIMER_NORESTART;
}

inline int iso_rl_borrow_tokens(struct iso_rl *rl, struct iso_rl_queue *q) {
	unsigned long flags;
	u64 borrow;
	int timeout = 1;

	spin_lock_irqsave(&rl->spinlock, flags);
	borrow = max(iso_rl_singleq_burst(rl), (u64)q->first_pkt_size);

	if(rl->total_tokens >= borrow) {
		rl->total_tokens -= borrow;
		q->tokens += borrow;
		/* XXX: Because we borrow when we are running low of tokens,
		   we don't need to cap */
		/*
		  cap = max((rl->rate * ISO_MAX_BURST_TIME_US) >> 3, (u64)ISO_MIN_BURST_BYTES);
		  q->tokens = min(cap, q->tokens);
		*/
		timeout = 0;
	}

	if(hrtimer_active(q->cputimer))
		timeout = 0;

	spin_unlock_irqrestore(&rl->spinlock, flags);
	return timeout;
}

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
