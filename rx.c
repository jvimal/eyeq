
#include <linux/netfilter_bridge.h>
#include "tx.h"
#include "rx.h"
#include "vq.h"

int iso_rx_hook_init(struct iso_rx_context *);
void iso_rx_hook_exit(struct iso_rx_context *);

struct list_head rxctx_list;

#ifdef QDISC
struct iso_rx_context *iso_rxctx_dev(const struct net_device *dev) {
	struct Qdisc *qdisc = dev->qdisc;
	struct mq_sched *mq = qdisc_priv(qdisc);
	return mq->rxc;
}
#else
struct iso_rx_context global_rxcontext;
struct iso_rx_context *iso_rxctx_dev(const struct net_device *dev) {
	return &global_rxcontext;
}
#endif

int iso_rx_init(struct iso_rx_context *context) {
	int i;

	printk(KERN_INFO "perfiso: Init RX path for %s\n", context->netdev->name);
	context->stats = alloc_percpu(struct iso_rx_stats);
	if (context->stats == NULL)
		return -1;
	context->last_stats_update_time = ktime_get();
	context->last_rcp_time = ktime_get();

	context->rcp_rate = ISO_VQ_DRAIN_RATE_MBPS;
	context->rx_rate = 0;

	for_each_possible_cpu(i) {
		struct iso_rx_stats *st = per_cpu_ptr(context->stats, i);
		memset(st, 0, sizeof(struct iso_rx_stats));
	}

	memset(&context->global_stats, 0, sizeof(struct iso_rx_stats));
	memset(&context->global_stats_last, 0, sizeof(struct iso_rx_stats));

	iso_vqs_init(context);
	list_add_tail(&context->list, &rxctx_list);
	return iso_rx_hook_init(context);
}

void iso_rx_exit(struct iso_rx_context *context) {
	printk(KERN_INFO "perfiso: Exit RX path for %s\n", context->netdev->name);
	list_del_init(&context->list);
	iso_vqs_exit(context);
	iso_rx_hook_exit(context);
	free_percpu(context->stats);
}

void iso_rx_stats_update(struct iso_rx_context *rxctx, struct sk_buff *skb)
{
	int cpu = smp_processor_id();
	struct iso_rx_stats *rxstats = per_cpu_ptr(rxctx->stats, cpu);
	u64 dt;
	ktime_t now;
	int i;
	u64 rx_bytes;

	rxstats->rx_bytes += skb_size(skb);
	rxstats->rx_packets += 1;

	now = ktime_get();
	/* There are too many test-and-test-and-set things going on.
	 * Abstract it out as light lock? */
	dt = ktime_us_delta(now, rxctx->last_stats_update_time);
	if (dt < ISO_VQ_UPDATE_INTERVAL_US)
		return;

	if (!spin_trylock(&rxctx->vq_spinlock))
		return;

	dt = ktime_us_delta(now, rxctx->last_stats_update_time);
	if (dt < ISO_VQ_UPDATE_INTERVAL_US)
		goto unlock;

	rxctx->last_stats_update_time = now;
	rxctx->global_stats_last = rxctx->global_stats;
	rxctx->global_stats.rx_bytes = 0;
	rxctx->global_stats.rx_packets = 0;

	/* Quickly sum everything up */
	for_each_online_cpu(i) {
		struct iso_rx_stats *st = per_cpu_ptr(rxctx->stats, i);
		rxctx->global_stats.rx_bytes += st->rx_bytes;
		rxctx->global_stats.rx_packets = st->rx_packets;
	}

	/* bits per us = mbps */
	rx_bytes = (rxctx->global_stats.rx_bytes - rxctx->global_stats_last.rx_bytes);
	rxctx->rx_rate = (rx_bytes << 3) / dt;

unlock:
	spin_unlock(&rxctx->vq_spinlock);
}

void iso_rx_rcp_update(struct iso_rx_context *rxctx)
{
	/* Based on rxctx->rx_rate, determine one advertised rate
	 * rxctx->rcp_rate. */
	ktime_t now = ktime_get();
	u64 dt, rate;
	u32 cap, cap2;

	dt = ktime_us_delta(now, rxctx->last_rcp_time);
	if (dt < ISO_VQ_HRCP_US)
		return;

	if (!spin_trylock(&rxctx->vq_spinlock))
		return;

	dt = ktime_us_delta(now, rxctx->last_rcp_time);
	if (dt < ISO_VQ_HRCP_US)
		goto unlock;

	cap = ISO_VQ_DRAIN_RATE_MBPS;
	cap2 = ISO_VQ_DRAIN_RATE_MBPS << 1;
	rate = (u64)rxctx->rcp_rate * (cap2 + cap - rxctx->rx_rate) / cap2;
	rate = max_t(u64, ISO_MIN_RFAIR, rate);
	rate = min_t(u64, ISO_VQ_DRAIN_RATE_MBPS, rate);
	rxctx->rcp_rate = rate;
	rxctx->last_rcp_time = now;

unlock:
	spin_unlock(&rxctx->vq_spinlock);
}

enum iso_verdict iso_rx(struct sk_buff *skb, const struct net_device *in, struct iso_rx_context *rxctx)
{
	struct iso_tx_class *txc;
	iso_class_t klass;
	struct iso_per_dest_state *state;
	struct iso_vq *vq;
	struct iso_vq_stats *stats;
	enum iso_verdict verdict = ISO_VERDICT_SUCCESS;
	struct iso_tx_context *txctx;

	rcu_read_lock();
	iso_rx_stats_update(rxctx, skb);
	iso_rx_rcp_update(rxctx);

	txctx = iso_txctx_dev(in);
	/* Pick VQ */
	klass = iso_rx_classify(skb);
	vq = iso_vq_find(klass, rxctx);
	if(vq == NULL)
		goto accept;

	iso_vq_enqueue(vq, skb);

	txc = iso_txc_find(klass, txctx);
	if(txc == NULL)
		goto accept;

	/*
	 * Warning: Legacy code.  We can do an optimisation here.  We
	 * seem to be doing a state lookup for every single packet.
	 * Instead, we can do state lookup only if there is feedback.
	 * Not very important for now, but useful to fix this in the
	 * future.
	 */
	state = iso_state_get(txc, skb, 1, ISO_CREATE_RL && iso_is_feedback_marked(skb));

	if(likely(state != NULL)) {
		int rate = skb_has_feedback(skb);
		struct iso_rc_state *rc = &state->tx_rc;
		ktime_t now = ktime_get();
		/* XXX: for now */
		if((ISO_VQ_DRAIN_RATE_MBPS <= ISO_MAX_TX_RATE) && (rate != 0)) {
			u64 dt = ktime_us_delta(now, rc->last_rfair_change_time);
			if(dt >= ISO_RFAIR_DECREASE_INTERVAL_US) {
				if(spin_trylock(&rc->spinlock)) {
					dt = ktime_us_delta(now, rc->last_rfair_change_time);
					if(dt >= ISO_RFAIR_DECREASE_INTERVAL_US) {
						state->rl->rate = rate;
						state->rl->last_rate_update_time = now;
						rc->last_rfair_change_time = now;
					}
					spin_unlock(&rc->spinlock);
				}
			}
		}

		if(unlikely(iso_is_generated_feedback(skb)))
			verdict = ISO_VERDICT_DROP;

		/* Clear the ECN mark before sending to stack */
		iso_clear_ecn(skb);
	}

	stats = per_cpu_ptr(vq->percpu_stats, smp_processor_id());
	if(IsoAutoGenerateFeedback && verdict != ISO_VERDICT_DROP) {
		ktime_t now = ktime_get();
		u64 dt = ktime_us_delta(ktime_get(), stats->last_feedback_gen_time);
		stats->rx_since_last_feedback += skb_size(skb);

		if((dt > ISO_FEEDBACK_INTERVAL_US) ||							\
		   (stats->rx_since_last_feedback >= ISO_FEEDBACK_INTERVAL_BYTES)) {
			iso_generate_feedback(iso_vq_over_limits(vq), skb);
			stats->last_feedback_gen_time = now;
			stats->rx_since_last_feedback = 0;
			stats->network_marked = 0;
		}
	}

 accept:
	rcu_read_unlock();
	return verdict;
}

int iso_vq_install(char *_klass, struct iso_rx_context *rxctx) {
	iso_class_t klass;
	struct iso_vq *vq;
	int ret = 0;

	rcu_read_lock();
	klass = iso_class_parse(_klass);
	vq = iso_vq_find(klass, rxctx);
	if(vq != NULL) {
		ret = -1;
		printk(KERN_INFO "perfiso: class %s exists\n", _klass);
		goto err;
	}

	vq = iso_vq_alloc(klass, rxctx);
	if(vq == NULL) {
		printk(KERN_INFO "perfiso: could not allocate vq\n");
		ret = -1;
		goto err;
	}

 err:
	rcu_read_unlock();
	return ret;
}

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */

