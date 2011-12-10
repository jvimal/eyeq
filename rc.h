#ifndef __RC_H__
#define __RC_H__
#include <linux/ktime.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include "params.h"
#include "rl.h"

struct iso_rc_stats {
	u64 num_marked;
	u64 num_rx;
};

/* Rate controller specific state */
struct iso_rc_state {
	u64 rfair;
	u64 alpha;

	struct iso_rc_stats __percpu *stats;

	ktime_t last_rfair_change_time;
	ktime_t last_rfair_decrease_time;
	ktime_t last_feedback_time;
	spinlock_t spinlock;
};


void iso_rc_init(struct iso_rc_state *);
inline int iso_rc_tx(struct iso_rc_state *, struct sk_buff *);
inline int iso_rc_rx(struct iso_rc_state *, struct sk_buff *);

/* We might have to be more "generic" as ai/md are specific */
inline void iso_rc_do_ai(struct iso_rc_state *);
inline void iso_rc_do_md(struct iso_rc_state *);
inline void iso_rc_do_alpha(struct iso_rc_state *);

void iso_rc_show(struct iso_rc_state *, struct seq_file *);
#endif /* __RC_H__ */

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
