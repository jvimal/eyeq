
#include "rc.h"

const char iso_rc_state_str[][32] = {
	[RC_FAST_RECOVERY] = "RC_FAST_RECOVERY",
	[RC_AI] = "RC_AI"
};

void iso_rc_init(struct iso_rc_state *rc) {
	int i;
	rc->rfair = ISO_RFAIR_INITIAL;
	rc->rfair_target = ISO_RFAIR_INITIAL;
	rc->alpha = 0;
	rc->count = 0;
	rc->state = RC_AI;

	rc->last_rfair_change_time = ktime_get();
	rc->last_rfair_decrease_time = ktime_get();
	rc->last_feedback_time = ktime_get();

	for_each_possible_cpu(i) {
		struct iso_rc_stats *stats = per_cpu_ptr(rc->stats, i);
		stats->num_marked = 0;
		stats->num_rx = 0;
	}

	spin_lock_init(&rc->spinlock);
}

/* Right now, we don't do anything on the TX side */
inline int iso_rc_tx(struct iso_rc_state *rc, struct sk_buff *skb) {
	return 0;
}

inline int iso_rc_rx(struct iso_rc_state *rc, struct sk_buff *skb) {
	int marked = skb_has_feedback(skb);
	ktime_t now = ktime_get();
	int changed = 0;
	u64 dt, target;
	int idle;
	struct iso_rc_stats *stats = per_cpu_ptr(rc->stats, smp_processor_id());

	stats->num_rx++;

	if(marked) {
		dt = ktime_us_delta(now, rc->last_rfair_decrease_time);
		stats->num_marked++;

		/* Reduce lock contention by being optimistic */
		if(dt > ISO_RFAIR_DECREASE_INTERVAL_US) {
			if(!spin_trylock(&rc->spinlock))
				goto end;

			/* Check again: it is required, but is it very likely? */
			dt = ktime_us_delta(now, rc->last_rfair_decrease_time);
			if(unlikely(dt < ISO_RFAIR_DECREASE_INTERVAL_US))
				goto done_decrease;

			idle = ktime_us_delta(now, rc->last_rfair_change_time) > ISO_IDLE_TIMEOUT_US;
			if(unlikely(idle && rc->rfair > ISO_IDLE_RATE)) {
				rc->rfair = rc->rfair_target = ISO_IDLE_RATE;
			}

			target = rc->rfair;
			rc->count = 0;
			/* Compute alpha */
			//iso_rc_do_alpha(rc);
			rc->alpha = marked;
			//iso_rc_do_md(rc);
			rc->rfair = (rc->rfair * (512 - marked)) / 512;
			rc->rfair = max((u64)ISO_MIN_RFAIR, rc->rfair);

			rc->last_rfair_decrease_time = now;
			rc->state = RC_FAST_RECOVERY;
			rc->rfair_target = target;

		done_decrease:
			spin_unlock(&rc->spinlock);
			goto changed;
		}

		goto end;
	} else {
		dt = ktime_us_delta(now, rc->last_rfair_change_time);
		if(dt > ISO_RFAIR_INCREASE_INTERVAL_US) {
			if(!spin_trylock(&rc->spinlock))
				goto end;

			dt = ktime_us_delta(now, rc->last_rfair_change_time);
			if(unlikely(dt < ISO_RFAIR_INCREASE_INTERVAL_US))
				goto done_increase;

			idle = dt > ISO_IDLE_TIMEOUT_US;
			if(unlikely(idle && rc->rfair > ISO_IDLE_RATE)) {
				rc->rfair = rc->rfair_target = ISO_IDLE_RATE;
			}

			// iso_rc_do_alpha(rc);
			if(rc->state == RC_FAST_RECOVERY && rc->count < 5) {
				rc->rfair = (rc->rfair + rc->rfair_target) / 2;
				rc->count++;
			} else {
				rc->state = RC_AI;
				rc->count = 0;
				iso_rc_do_ai(rc);
				rc->rfair_target = rc->rfair;
			}
		done_increase:
			spin_unlock(&rc->spinlock);
			goto changed;
		}

		goto end;
	}

 changed:
	rc->last_rfair_change_time = now;
	changed = 1;

 end:
	return changed;
}

inline void iso_rc_do_ai(struct iso_rc_state *rc) {
	rc->rfair = min((u64)ISO_MAX_TX_RATE, rc->rfair + ISO_RFAIR_INCREMENT);
}

inline void iso_rc_do_md(struct iso_rc_state *rc) {
	rc->rfair = rc->rfair * (2048 * ISO_FALPHA - rc->alpha) / (2048 * ISO_FALPHA);
	rc->rfair = max((u64)ISO_MIN_RFAIR, rc->rfair);
}

inline void iso_rc_do_alpha(struct iso_rc_state *rc) {
	struct iso_rc_stats *stats;
	u64 num_marked = 0, num_rx = 0;
	u64 frac = 0;
	int i;

	for_each_online_cpu(i) {
		stats = per_cpu_ptr(rc->stats, i);
		num_marked += stats->num_marked;
		num_rx += stats->num_rx;

		stats->num_marked = stats->num_rx = 0;
	}

	if(likely(num_rx))
		frac = 1024 * num_marked / num_rx;

#define MUL31(x) (((x) << 5) - (x))
#define DIV32(x) ((x) >> 5)
#define EWMA_G32(old, new)  DIV32((MUL31(old) + new))

	rc->alpha = EWMA_G32(rc->alpha, frac);
}

void iso_rc_show(struct iso_rc_state *rc, struct seq_file *s) {
	int i;
	struct iso_rc_stats *stats;

	seq_printf(s, "\trfair %llu (%llu)   alpha %llu   state %s   "
			   "last_change %llx   last_decrease %llx   last_feedback %llx\n",
			   rc->rfair, rc->rfair_target, rc->alpha, iso_rc_state_str[rc->state],
			   *(u64*)&rc->last_rfair_change_time, *(u64*)&rc->last_rfair_decrease_time,
			   *(u64*)&rc->last_feedback_time);

	seq_printf(s, "\t\tpercpu rx:");
	for_each_online_cpu(i) {
		stats = per_cpu_ptr(rc->stats, i);
		seq_printf(s, "  %9llx", stats->num_rx);
	}

	seq_printf(s, "\n\t\tpercpu fb:");
	for_each_online_cpu(i) {
		stats = per_cpu_ptr(rc->stats, i);
		seq_printf(s, "  %9llx", stats->num_marked);
	}

	seq_printf(s, "\n");
}

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
