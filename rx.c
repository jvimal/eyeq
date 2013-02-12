
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
	printk(KERN_INFO "perfiso: Init RX path for %s\n", context->netdev->name);
	iso_vqs_init(context);
	list_add_tail(&context->list, &rxctx_list);
	return iso_rx_hook_init(context);
}

void iso_rx_exit(struct iso_rx_context *context) {
	printk(KERN_INFO "perfiso: Exit RX path for %s\n", context->netdev->name);
	list_del_init(&context->list);
	iso_vqs_exit(context);
	iso_rx_hook_exit(context);
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

