
#include "vq.h"

static s64 vq_total_tokens;
static ktime_t vq_last_update_time;
static spinlock_t vq_spinlock;
static struct list_head vq_list;

void iso_vqs_init() {
	INIT_LIST_HEAD(&vq_list);
	vq_total_tokens = 0;
	vq_last_update_time = ktime_get();
	spin_lock_init(&vq_spinlock);
}

void iso_vqs_exit() {
	struct iso_vq *vq;
	for_each_vq(vq) {
		iso_vq_free(vq);
	}
}

int iso_vq_init(struct iso_vq *vq) {
	vq->enabled = 1;
	vq->active = 0;
	vq->is_static = 0;
	vq->total_bytes_queued = 0;
	vq->backlog = 0;
	vq->weight = 1;
	vq->last_update_time = ktime_get();

	vq->percpu_stats = alloc_percpu(struct iso_vq_stats);
	if(vq->percpu_stats == NULL)
		return -ENOMEM;

	spin_lock_init(&vq->spinlock);
	INIT_LIST_HEAD(&vq->list);
	return 0;
}

void iso_vq_free(struct iso_vq *vq) {
	list_del_rcu(&vq->list);
	free_percpu(vq->percpu_stats);
	kfree(vq);
}

void iso_vq_enqueue(struct iso_vq *vq, struct sk_buff *pkt, u32 len) {
	ktime_t now = ktime_get();
	u64 dt = ktime_us_delta(now, vq_last_update_time);
	unsigned long flags;
	int cpu = smp_processor_id();
	struct iso_vq_stats *stats = per_cpu_ptr(vq->percpu_stats, cpu);

	if(unlikely(dt > ISO_VQ_UPDATE_INTERVAL_US)) {
		if(spin_trylock_irqsave(&vq_spinlock, flags)) {
			iso_vq_tick(dt);
			spin_unlock_irqrestore(&vq_spinlock, flags);
		}
	}

	stats->bytes_queued += len;
}

inline int iso_vq_active(struct iso_vq *vq) {
	return vq->backlog > 0;
}

/* Should be called once in a while */
void iso_vq_tick(u64 dt) {
	u64 diff_tokens = (ISO_VQ_DRAIN_RATE_MBPS * dt) >> 3;
	u64 active_weight = 0;
	struct iso_vq *vq;

	vq_total_tokens += diff_tokens;
	vq_total_tokens = min((u64)(ISO_VQ_DRAIN_RATE_MBPS * ISO_MAX_BURST_TIME_US) >> 3,
						  diff_tokens);

	for_each_vq(vq) {
		iso_vq_drain(vq, dt);
		if(iso_vq_active(vq))
			active_weight += vq->weight;
	}

	/* Reassign capacities */
	for_each_vq(vq) {
		if(iso_vq_active(vq)) {
			vq->rate = ISO_VQ_DRAIN_RATE_MBPS * vq->weight / active_weight;
		} else {
			vq->rate = ISO_VQ_DEFAULT_RATE_MBPS;
		}
	}
}

/* called with vq's global lock */
void iso_vq_drain(struct iso_vq *vq, u64 dt) {
	u64 max_drain;
	u64 can_drain;
	int i;

	max_drain = vq->rate * dt;

	/* assimilate and reset per-cpu counters */
	for_each_possible_cpu(i) {
		struct iso_vq_stats *stats = per_cpu_ptr(vq->percpu_stats, i);
		vq->backlog += stats->bytes_queued;
		stats->bytes_queued = 0;
	}

	can_drain = min(vq->backlog, max_drain);
	vq->backlog -= can_drain;

	vq_total_tokens -= can_drain;
	vq_total_tokens = max(vq_total_tokens, 0LL);
}

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
