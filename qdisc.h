#ifndef __QDISC_H__
#define __QDISC_H__

#define TCQ_F_EYEQ (1 << 4)

struct Qdisc;

/* Wrapper around per-dev qdisc */
struct mq_sched {
	struct Qdisc		**qdiscs;

	/* This is where the tx and rx control blocks will be
	 * populated */
	void *txc;
	void *rxc;
};

#endif /* __QDISC_H__ */
