
#include "vq.h"

/*
s64 vq_total_tokens;
ktime_t vq_last_update_time;
ktime_t vq_last_check_time;
spinlock_t vq_spinlock;
struct list_head vq_list;
struct hlist_head vq_bucket[ISO_MAX_VQ_BUCKETS];
atomic_t vq_active_rate;
*/

void iso_vqs_init(struct iso_rx_context *ctx) {
	int i;
	INIT_LIST_HEAD(&ctx->vq_list);
	ctx->vq_total_tokens = 0;
	ctx->vq_last_update_time = ktime_get();
	ctx->vq_last_check_time = ktime_get();

	spin_lock_init(&ctx->vq_spinlock);
	atomic_set(&ctx->vq_active_rate, 0);

	for(i = 0; i < ISO_MAX_VQ_BUCKETS; i++) {
		INIT_HLIST_HEAD(&ctx->vq_bucket[i]);
	}
}

void iso_vqs_exit(struct iso_rx_context *ctx) {
	struct iso_vq *vq, *vq_next;
	for_each_vq(vq, ctx) {
		iso_vq_free(vq);
	}
}

struct iso_vq *iso_vq_alloc(iso_class_t klass, struct iso_rx_context *rxctx) {
	struct iso_vq *vq = kmalloc(sizeof(struct iso_vq), GFP_KERNEL);
	u32 hash;
	struct hlist_head *head;

	if(vq) {
		vq->rxctx = rxctx;
		iso_vq_init(vq);
		rcu_read_lock();
		vq->klass = klass;
		hash = iso_class_hash(klass);
		head = &rxctx->vq_bucket[hash & (ISO_MAX_VQ_BUCKETS - 1)];

		list_add_tail_rcu(&vq->list, &rxctx->vq_list);
		hlist_add_head_rcu(&vq->hash_node, head);
		iso_vq_calculate_rates(rxctx);
		rcu_read_unlock();
	}
	return vq;
}

void iso_vq_calculate_rates(struct iso_rx_context *rxctx) {
	u32 total_weight = 0;
	struct iso_vq *vq, *vq_next;

	for_each_vq(vq, rxctx) {
		total_weight += vq->weight;
	}

	if(total_weight > 0) {
		for_each_vq(vq, rxctx) {
			vq->rate = ISO_VQ_DRAIN_RATE_MBPS * vq->weight / total_weight;
		}
	}
}

int iso_vq_init(struct iso_vq *vq) {
	int i;
	vq->enabled = 1;
	vq->active = 0;
	vq->is_static = 0;
	vq->rate = ISO_MIN_RFAIR;
	vq->total_bytes_queued = 0;
	vq->backlog = 0;
	vq->feedback_rate = ISO_MIN_RFAIR;
	vq->last_rx_bytes = 0;
	vq->rx_rate = 0;
	vq->weight = 1;
	vq->alpha = 0;
	vq->last_update_time = vq->last_borrow_time = ktime_get();

	vq->percpu_stats = alloc_percpu(struct iso_vq_stats);
	if(vq->percpu_stats == NULL)
		return -ENOMEM;

	for_each_possible_cpu(i) {
		struct iso_vq_stats *stats = per_cpu_ptr(vq->percpu_stats, i);
		stats->bytes_queued = 0;
		stats->network_marked = 0;
		stats->rx_bytes = 0;
		stats->rx_packets = 0;
		stats->rx_since_last_feedback = 0;
		stats->rx_marked_since_last_feedback = 0;
	}

	spin_lock_init(&vq->spinlock);
	vq->tokens = 0;

	INIT_LIST_HEAD(&vq->list);
	INIT_HLIST_NODE(&vq->hash_node);

	atomic_set(&vq->refcnt, 0);
	return 0;
}

void iso_vq_free(struct iso_vq *vq) {
	if(atomic_read(&vq->refcnt) > 0)
		return;

	synchronize_rcu();
	list_del(&vq->list);
	free_percpu(vq->percpu_stats);
	kfree(vq);
}

void iso_vq_check_idle(struct iso_rx_context *rxctx) {
	struct iso_vq *vq, *vq_next;
	ktime_t now = ktime_get();

	for_each_vq(vq, rxctx) {
		if(!spin_trylock(&vq->spinlock))
			continue;

		if(vq->active && ktime_us_delta(now, vq->last_update_time) > 10000) {
			vq->active = 0;
			atomic_sub(vq->rate, &rxctx->vq_active_rate);
		}

		spin_unlock(&vq->spinlock);
	}

	rxctx->vq_last_check_time = now;
}

void iso_vq_enqueue(struct iso_vq *vq, struct sk_buff *pkt) {
	ktime_t now;
	u64 dt;
	unsigned long flags;
	int cpu = smp_processor_id();
	struct iso_vq_stats *stats = per_cpu_ptr(vq->percpu_stats, cpu);
	u32 len = skb_size(pkt);
	struct ethhdr *eth;
	struct iphdr *iph;
	struct iso_rx_context *rxctx = vq->rxctx;

	eth = eth_hdr(pkt);

	stats->rx_since_last_feedback++;
	stats->rx_packets++;

	if(likely(eth->h_proto == __constant_htons(ETH_P_IP))) {
		iph = ip_hdr(pkt);
		if((iph->tos & 0x3) == 0x3) {
			stats->network_marked++;
			stats->rx_marked_since_last_feedback++;
		}
	}

	now = ktime_get();
	if(ktime_us_delta(now, rxctx->vq_last_check_time) > 10000) {
		iso_vq_check_idle(rxctx);
	}

	now = ktime_get();
	dt = ktime_us_delta(now, vq->last_update_time);
	if(unlikely(dt > ISO_VQ_UPDATE_INTERVAL_US)) {
		if(spin_trylock_irqsave(&vq->spinlock, flags)) {
			iso_vq_drain(vq, dt);
			spin_unlock_irqrestore(&vq->spinlock, flags);
		}
	}

	stats->bytes_queued += len;
	stats->rx_bytes += len;
}

/* Should be called once in a while */
void iso_vq_tick(u64 dt, struct iso_rx_context *rxctx) {
	u64 diff_tokens = (ISO_VQ_DRAIN_RATE_MBPS * dt) >> 3;
	u64 active_weight = 0, total_weight = 0;
	struct iso_vq *vq, *vq_next;

	rxctx->vq_total_tokens += diff_tokens;
	rxctx->vq_total_tokens = min((u64)(ISO_VQ_DRAIN_RATE_MBPS * ISO_MAX_BURST_TIME_US) >> 3,
				     diff_tokens);

	for_each_vq(vq, rxctx) {
		iso_vq_drain(vq, dt);
		total_weight += vq->weight;
		if(iso_vq_active(vq))
			active_weight += vq->weight;
	}

	/* Reassign capacities */
	for_each_vq(vq, rxctx) {
		if(iso_vq_active(vq) && active_weight > 0) {
			vq->rate = ISO_VQ_DRAIN_RATE_MBPS * vq->weight / active_weight;
		} else {
			vq->rate = 0;
		}
	}
}

/* Called with the global lock */
inline void iso_vq_global_tick(struct iso_rx_context *rxctx) {
	u64 dtokens, dt, maxtokens;
	ktime_t now = ktime_get();

	dt = ktime_us_delta(now, rxctx->vq_last_update_time);
	dt = min_t(u64, dt, ISO_VQ_REFRESH_INTERVAL_US);

	dtokens = (ISO_VQ_DRAIN_RATE_MBPS * dt) >> 3;
	maxtokens = (ISO_VQ_DRAIN_RATE_MBPS * ISO_VQ_REFRESH_INTERVAL_US) >> 3;

	rxctx->vq_total_tokens = min(maxtokens, rxctx->vq_total_tokens + dtokens);
	rxctx->vq_last_update_time = now;
}



/* called with vq's lock */
void iso_vq_drain(struct iso_vq *vq, u64 dt) {
	u64 can_drain, max_drain, min_borrow;
	u64 rx_bytes, dt2;
	u32 rx_pkts, rx_marked;
	int i, factor;
	ktime_t now = ktime_get();
	struct iso_rx_context *rxctx = vq->rxctx;

	dt2 = ktime_us_delta(now, vq->last_update_time);
	if(dt2 < ISO_VQ_UPDATE_INTERVAL_US)
		return;

	vq->last_update_time = now;
	min_borrow = 0;
	rx_bytes = 0;
	factor = 0;

	rx_pkts = 0;
	rx_marked = 0;

	/* assimilate and reset per-cpu counters */
	for_each_online_cpu(i) {
		struct iso_vq_stats *stats = per_cpu_ptr(vq->percpu_stats, i);
		vq->backlog += stats->bytes_queued;
		rx_bytes += stats->rx_bytes;
		stats->bytes_queued = 0;

		rx_pkts += stats->rx_since_last_feedback;
		rx_marked += stats->rx_marked_since_last_feedback;

		stats->rx_since_last_feedback = 0;
		stats->rx_marked_since_last_feedback = 0;
	}

	vq->backlog = min(vq->backlog, (u64)ISO_VQ_MAX_BYTES);

	if (unlikely(rx_pkts == 0)) {
		if (net_ratelimit())
			printk(KERN_INFO "EyeQ: BUG: rx_pkts is 0, but it shouldn't be.\n");
		rx_pkts = 1;
	}

	if(vq->backlog > 0) {
		if(!vq->active) {
			vq->active = 1;
			atomic_add(vq->rate, &rxctx->vq_active_rate);
		}

		factor = atomic_read(&rxctx->vq_active_rate);
		if(factor == 0)
			factor = vq->rate;

		min_borrow = (ISO_VQ_DRAIN_RATE_MBPS * vq->rate * dt / factor) >> 3;
	} else {
		if(vq->active) {
			vq->active = 0;
			atomic_sub(vq->rate, &rxctx->vq_active_rate);
		}
		vq->feedback_rate = ISO_VQ_DRAIN_RATE_MBPS;
		factor = vq->rate;
	}

	/* The control algorithms */
	{
		/* RCP calculation */
		{
			int rate = vq->rate * ISO_VQ_DRAIN_RATE_MBPS / factor;
			u64 diff = rx_bytes - vq->last_rx_bytes;
			int rx_rate = (diff << 3) / dt;
			if(ISO_VQ_DRAIN_RATE_MBPS <= ISO_MAX_TX_RATE) {
				vq->feedback_rate = vq->feedback_rate * (3 * rate - rx_rate) / (rate << 1);
				vq->feedback_rate = min_t(u64, rate, vq->feedback_rate);
				vq->feedback_rate = max_t(u64, ISO_MIN_RFAIR, vq->feedback_rate);
			} else {
				vq->feedback_rate = ISO_MAX_TX_RATE;
			}
			vq->rx_rate = rx_rate;
			vq->last_rx_bytes = rx_bytes;
		}

		/* ECN calculation */
		{
			u32 frac = (rx_marked << ECN_ALPHA_FRAC_SHIFT) / rx_pkts;
			u32 mult = 1 << (ECN_ALPHA_FRAC_SHIFT + 1);
			/* Safeguard against races. */
			frac = min_t(u32, (1 << ECN_ALPHA_FRAC_SHIFT), frac);
			vq->alpha = EWMA_G16(vq->alpha, frac);
			vq->feedback_rate = (vq->feedback_rate * (mult - frac)) >> (ECN_ALPHA_FRAC_SHIFT + 1);
			vq->feedback_rate = min_t(u64, ISO_VQ_DRAIN_RATE_MBPS, vq->feedback_rate);
			vq->feedback_rate = max_t(u64, ISO_MIN_RFAIR, vq->feedback_rate);
		}
	}

	// TODO: get rid of all this
	if(vq->tokens == 0) {
		/* We've run out of tokens.  Borrow in proportion to our
		   rate */
		u64 borrow = min_borrow;

		if(spin_trylock_irq(&rxctx->vq_spinlock)) {
			int rate;
			iso_vq_global_tick(rxctx);
			rxctx->vq_total_tokens -= borrow;
			vq->tokens += borrow;
			/* Don't accumulate infinitely many tokens */
			if(factor == 0)
				factor = vq->rate;
			rate = vq->rate * ISO_VQ_DRAIN_RATE_MBPS / factor;
			vq->tokens = min_t(u64, vq->tokens, (rate * ISO_MAX_BURST_TIME_US) >> 3);
			spin_unlock_irq(&rxctx->vq_spinlock);
		}
	}

	max_drain = vq->tokens;
	can_drain = min(vq->backlog, max_drain);
	vq->backlog -= can_drain;
	vq->tokens -= can_drain;
}

void iso_vq_show(struct iso_vq *vq, struct seq_file *s) {
	char buff[128];
	int first = 1, i;
	struct iso_vq_stats *stats;

	iso_class_show(vq->klass, buff);
	seq_printf(s, "vq class %s   flags %d,%d,%d   rate %llu  rx_rate %llu  fb_rate %llu  alpha %u/%u  "
		   " backlog %llu   weight %llu   refcnt %d   tokens %llu\n",
		   buff, vq->enabled, vq->active, vq->is_static,
		   vq->rate, vq->rx_rate, vq->feedback_rate, vq->alpha, (1 << 10),
		   vq->backlog, vq->weight, atomic_read(&vq->refcnt), vq->tokens);

	for_each_online_cpu(i) {
		if(first) {
			first = 0;
			seq_printf(s, "\t cpu   enqueued   network-mark   rx\n");
		}

		stats = per_cpu_ptr(vq->percpu_stats, i);

		if(stats->bytes_queued > 0 || stats->network_marked > 0) {
			seq_printf(s, "\t %3d   %8llu  %12llu   %llu\n",
					   i, stats->bytes_queued, stats->network_marked, stats->rx_bytes);
		}
	}
}

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
