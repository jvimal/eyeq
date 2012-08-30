
#include <linux/netfilter_bridge.h>
#include "tx.h"
#include "rx.h"
#include "vq.h"

int iso_rx_hook_init(void);

int iso_rx_init() {
	printk(KERN_INFO "perfiso: Init RX path\n");
	iso_vqs_init();
	return iso_rx_hook_init();
}

void iso_rx_exit() {
	iso_vqs_exit();
}

enum iso_verdict iso_rx(struct sk_buff *skb, const struct net_device *in)
{
	struct iso_tx_class *txc;
	iso_class_t klass;
	struct iso_per_dest_state *state;
	struct iso_vq *vq;
	struct iso_vq_stats *stats;
	struct iso_rc_state *rc;
	int changed;
	enum iso_verdict verdict = ISO_VERDICT_SUCCESS;

	rcu_read_lock();

	/* Pick VQ */
	klass = iso_rx_classify(skb);
	vq = iso_vq_find(klass);
	if(vq == NULL)
		goto accept;

	iso_vq_enqueue(vq, skb);

	txc = iso_txc_find(klass);
	if(txc == NULL)
		goto accept;

	state = iso_state_get(txc, skb, 1, ISO_CREATE_RL && iso_is_feedback_marked(skb));

	if(likely(state != NULL)) {
		rc = &state->tx_rc;
		changed = iso_rc_rx(rc, skb);

		/* XXX: for now */
		if(changed && ISO_VQ_DRAIN_RATE_MBPS <= ISO_MAX_TX_RATE)
			state->rl->rate = rc->rfair;

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

int iso_vq_install(char *_klass) {
	iso_class_t klass;
	struct iso_vq *vq;
	int ret = 0;

	rcu_read_lock();
	klass = iso_class_parse(_klass);
	vq = iso_vq_find(klass);
	if(vq != NULL) {
		ret = -1;
		printk(KERN_INFO "perfiso: class %s exists\n", _klass);
		goto err;
	}

	vq = iso_vq_alloc(klass);
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

