
#include "vq.h"

s64 vq_total_tokens;
ktime_t vq_last_update_time;
spinlock_t vq_spinlock;
struct list_head vq_list;
static struct hlist_head vq_bucket[ISO_MAX_VQ_BUCKETS];

void iso_vqs_init() {
	int i;
	INIT_LIST_HEAD(&vq_list);
	vq_total_tokens = 0;
	vq_last_update_time = ktime_get();
	spin_lock_init(&vq_spinlock);

	for(i = 0; i < ISO_MAX_VQ_BUCKETS; i++) {
		INIT_HLIST_HEAD(&vq_bucket[i]);
	}
}

void iso_vqs_exit() {
	struct iso_vq *vq;
	rcu_read_lock();
	for_each_vq(vq) {
		iso_vq_free(vq);
	}
	rcu_read_unlock();
}

struct iso_vq *iso_vq_alloc(iso_class_t klass) {
	struct iso_vq *vq = kmalloc(sizeof(struct iso_vq), GFP_KERNEL);
	u32 hash;
	struct hlist_head *head;

	if(vq) {
		iso_vq_init(vq);
		rcu_read_lock();
		vq->klass = klass;
		hash = iso_class_hash(klass);
		head = &vq_bucket[hash & (ISO_MAX_VQ_BUCKETS - 1)];

		list_add_tail_rcu(&vq->list, &vq_list);
		hlist_add_head_rcu(&vq->hash_node, head);
		rcu_read_unlock();
	}
	return vq;
}

int iso_vq_init(struct iso_vq *vq) {
	int i;
	vq->enabled = 1;
	vq->active = 0;
	vq->is_static = 0;
	vq->rate = 1;
	vq->total_bytes_queued = 0;
	vq->backlog = 0;
	vq->weight = 1;
	vq->last_update_time = ktime_get();

	vq->percpu_stats = alloc_percpu(struct iso_vq_stats);
	if(vq->percpu_stats == NULL)
		return -ENOMEM;

	for_each_possible_cpu(i) {
		struct iso_vq_stats *stats = per_cpu_ptr(vq->percpu_stats, i);
		stats->bytes_queued = 0;
		stats->network_marked = 0;
		stats->rx_bytes = 0;
	}

	spin_lock_init(&vq->spinlock);
	INIT_LIST_HEAD(&vq->list);
	INIT_HLIST_NODE(&vq->hash_node);

	atomic_set(&vq->refcnt, 0);
	return 0;
}

void iso_vq_free(struct iso_vq *vq) {
	if(atomic_read(&vq->refcnt) > 0)
		return;
	rcu_read_lock();
	list_del_rcu(&vq->list);
	rcu_read_unlock();
	free_percpu(vq->percpu_stats);
	kfree(vq);
}

void iso_vq_enqueue(struct iso_vq *vq, struct sk_buff *pkt) {
	ktime_t now = ktime_get();
	u64 dt = ktime_us_delta(now, vq_last_update_time);
	unsigned long flags;
	int cpu = smp_processor_id();
	struct iso_vq_stats *stats = per_cpu_ptr(vq->percpu_stats, cpu);
	u32 len = skb_size(pkt);

	if(unlikely(dt > ISO_VQ_UPDATE_INTERVAL_US)) {
		if(spin_trylock_irqsave(&vq_spinlock, flags)) {
			iso_vq_tick(dt);
			vq_last_update_time = now;
			spin_unlock_irqrestore(&vq_spinlock, flags);
		}
	}

	stats->bytes_queued += len;
	stats->rx_bytes += len;
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
		if(iso_vq_active(vq) && active_weight > 0) {
			vq->rate = ISO_VQ_DRAIN_RATE_MBPS * vq->weight / active_weight;
		}
	}
}

/* called with vq's global lock */
void iso_vq_drain(struct iso_vq *vq, u64 dt) {
	u64 max_drain;
	u64 can_drain;
	int i;

	max_drain = (vq->rate * dt) >> 3;

	/* assimilate and reset per-cpu counters */
	for_each_online_cpu(i) {
		struct iso_vq_stats *stats = per_cpu_ptr(vq->percpu_stats, i);
		vq->backlog += stats->bytes_queued;
		stats->bytes_queued = 0;
	}

	vq->backlog = min(vq->backlog, (u64)ISO_VQ_MAX_BYTES);
	can_drain = min(vq->backlog, max_drain);
	vq->backlog -= can_drain;

	vq_total_tokens -= can_drain;
	vq_total_tokens = max(vq_total_tokens, 0LL);
}

inline int iso_vq_over_limits(struct iso_vq *vq) {
	return vq->backlog > ISO_VQ_MARK_THRESH_BYTES;
}

/* Called with rcu lock */
inline struct iso_vq *iso_vq_find(iso_class_t klass) {
	u32 hash = iso_class_hash(klass);
	struct hlist_head *head = &vq_bucket[hash & (ISO_MAX_VQ_BUCKETS - 1)];
	struct hlist_node *node;
	struct iso_vq *vq;

	hlist_for_each_entry_rcu(vq, node, head, hash_node) {
		if(iso_class_cmp(vq->klass, klass) == 0)
			return vq;
	}

	return NULL;
}

void iso_vq_show(struct iso_vq *vq, struct seq_file *s) {
	char buff[128];
	int first = 1, i;
	struct iso_vq_stats *stats;

	iso_class_show(vq->klass, buff);
	seq_printf(s, "vq class %s   flags %d%d%d   rate %llu   backlog %llu   weight %llu"
			   "   refcnt %d\n",
			   buff, vq->enabled, vq->active, vq->is_static,
			   vq->rate, vq->backlog, vq->weight,
			   atomic_read(&vq->refcnt));

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
