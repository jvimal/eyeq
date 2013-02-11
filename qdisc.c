#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include "rl.h"

#ifndef QDISC
#error "Compiling qdisc.c without -DQDISC"
#endif
#include "qdisc.h"
#include "tx.h"
#include "rx.h"
extern struct net_device *iso_netdev;

/*
 * net/sched/sch_mq.c		Classful multiqueue dummy scheduler
 *
 * Copyright (c) 2009 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
//#include <linux/export.h>

/*
 * Dummy per-dev queue qdisc
 */

static int eyeq_local_init(struct Qdisc *a, struct nlattr *opt)
{
	return 0;
}

static void eyeq_local_destroy(struct Qdisc *a)
{
}

static int iso_enqueue(struct sk_buff *skb, struct Qdisc *sch);
static struct sk_buff *iso_dequeue(struct Qdisc *sch) {
	qdisc_throttled(sch);
	return NULL;
}

static struct Qdisc_ops iso_local_ops __read_mostly = {
	.next = NULL,
	.cl_ops = NULL,
	.id = "eyeq",
	.priv_size = sizeof(int),
	.init = eyeq_local_init,
	.destroy = eyeq_local_destroy,
	.enqueue = iso_enqueue,
	.dequeue = iso_dequeue,
};

static void mq_destroy(struct Qdisc *sch)
{
	struct net_device *dev = qdisc_dev(sch);
	struct mq_sched *priv = qdisc_priv(sch);
	unsigned int ntx;

	if (!priv->qdiscs)
		return;
	for (ntx = 0; ntx < dev->num_tx_queues && priv->qdiscs[ntx]; ntx++)
		qdisc_destroy(priv->qdiscs[ntx]);
	kfree(priv->qdiscs);

	/* TODO: free the txc and rxc */
}

static int mq_init(struct Qdisc *sch, struct nlattr *opt)
{
	struct net_device *dev = qdisc_dev(sch);
	struct mq_sched *priv = qdisc_priv(sch);
	struct netdev_queue *dev_queue;
	struct Qdisc *qdisc;
	unsigned int ntx;

	if (sch->parent != TC_H_ROOT)
		return -EOPNOTSUPP;

	/* TODO: support even if not multiqueue.  I think we can just
	 * disable this check. */
	if (!netif_is_multiqueue(dev))
		return -EOPNOTSUPP;

	/* pre-allocate qdiscs, attachment can't fail */
	priv->qdiscs = kcalloc(dev->num_tx_queues, sizeof(priv->qdiscs[0]),
			       GFP_KERNEL);
	if (priv->qdiscs == NULL)
		return -ENOMEM;

	for (ntx = 0; ntx < dev->num_tx_queues; ntx++) {
		dev_queue = netdev_get_tx_queue(dev, ntx);
		qdisc = qdisc_create_dflt(dev_queue, &iso_local_ops,
					  TC_H_MAKE(TC_H_MAJ(sch->handle),
						    TC_H_MIN(ntx + 1)));
		if (qdisc == NULL)
			goto err;
		qdisc->flags |= TCQ_F_CAN_BYPASS;
		priv->qdiscs[ntx] = qdisc;
	}

	sch->flags |= TCQ_F_MQROOT;
	sch->flags |= TCQ_F_EYEQ;
	return 0;

err:
	mq_destroy(sch);
	return -ENOMEM;
}

static void mq_attach(struct Qdisc *sch)
{
	struct net_device *dev = qdisc_dev(sch);
	struct mq_sched *priv = qdisc_priv(sch);
	struct Qdisc *qdisc;
	unsigned int ntx;

	for (ntx = 0; ntx < dev->num_tx_queues; ntx++) {
		qdisc = priv->qdiscs[ntx];
		qdisc = dev_graft_qdisc(qdisc->dev_queue, qdisc);
		if (qdisc)
			qdisc_destroy(qdisc);
	}
	kfree(priv->qdiscs);
	priv->qdiscs = NULL;
}

static int mq_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct net_device *dev = qdisc_dev(sch);
	struct Qdisc *qdisc;
	unsigned int ntx;

	sch->q.qlen = 0;
	memset(&sch->bstats, 0, sizeof(sch->bstats));
	memset(&sch->qstats, 0, sizeof(sch->qstats));

	for (ntx = 0; ntx < dev->num_tx_queues; ntx++) {
		qdisc = netdev_get_tx_queue(dev, ntx)->qdisc_sleeping;
		spin_lock_bh(qdisc_lock(qdisc));
		sch->q.qlen		+= qdisc->q.qlen;
		sch->bstats.bytes	+= qdisc->bstats.bytes;
		sch->bstats.packets	+= qdisc->bstats.packets;
		sch->qstats.qlen	+= qdisc->qstats.qlen;
		sch->qstats.backlog	+= qdisc->qstats.backlog;
		sch->qstats.drops	+= qdisc->qstats.drops;
		sch->qstats.requeues	+= qdisc->qstats.requeues;
		sch->qstats.overlimits	+= qdisc->qstats.overlimits;
		spin_unlock_bh(qdisc_lock(qdisc));
	}
	return 0;
}

static struct netdev_queue *mq_queue_get(struct Qdisc *sch, unsigned long cl)
{
	struct net_device *dev = qdisc_dev(sch);
	unsigned long ntx = cl - 1;

	if (ntx >= dev->num_tx_queues)
		return NULL;
	return netdev_get_tx_queue(dev, ntx);
}

static struct netdev_queue *mq_select_queue(struct Qdisc *sch,
					    struct tcmsg *tcm)
{
	unsigned int ntx = TC_H_MIN(tcm->tcm_parent);
	struct netdev_queue *dev_queue = mq_queue_get(sch, ntx);

	if (!dev_queue) {
		struct net_device *dev = qdisc_dev(sch);

		return netdev_get_tx_queue(dev, 0);
	}
	return dev_queue;
}

static int mq_graft(struct Qdisc *sch, unsigned long cl, struct Qdisc *new,
		    struct Qdisc **old)
{
	struct netdev_queue *dev_queue = mq_queue_get(sch, cl);
	struct net_device *dev = qdisc_dev(sch);

	if (dev->flags & IFF_UP)
		dev_deactivate(dev);

	*old = dev_graft_qdisc(dev_queue, new);

	if (dev->flags & IFF_UP)
		dev_activate(dev);
	return 0;
}

static struct Qdisc *mq_leaf(struct Qdisc *sch, unsigned long cl)
{
	struct netdev_queue *dev_queue = mq_queue_get(sch, cl);

	return dev_queue->qdisc_sleeping;
}

static unsigned long mq_get(struct Qdisc *sch, u32 classid)
{
	unsigned int ntx = TC_H_MIN(classid);

	if (!mq_queue_get(sch, ntx))
		return 0;
	return ntx;
}

static void mq_put(struct Qdisc *sch, unsigned long cl)
{
}

static int mq_dump_class(struct Qdisc *sch, unsigned long cl,
			 struct sk_buff *skb, struct tcmsg *tcm)
{
	struct netdev_queue *dev_queue = mq_queue_get(sch, cl);

	tcm->tcm_parent = TC_H_ROOT;
	tcm->tcm_handle |= TC_H_MIN(cl);
	tcm->tcm_info = dev_queue->qdisc_sleeping->handle;
	return 0;
}

static int mq_dump_class_stats(struct Qdisc *sch, unsigned long cl,
			       struct gnet_dump *d)
{
	struct netdev_queue *dev_queue = mq_queue_get(sch, cl);

	sch = dev_queue->qdisc_sleeping;
	sch->qstats.qlen = sch->q.qlen;
	if (gnet_stats_copy_basic(d, &sch->bstats) < 0 ||
	    gnet_stats_copy_queue(d, &sch->qstats) < 0)
		return -1;
	return 0;
}

static void mq_walk(struct Qdisc *sch, struct qdisc_walker *arg)
{
	struct net_device *dev = qdisc_dev(sch);
	unsigned int ntx;

	if (arg->stop)
		return;

	arg->count = arg->skip;
	for (ntx = arg->skip; ntx < dev->num_tx_queues; ntx++) {
		if (arg->fn(sch, ntx + 1, arg) < 0) {
			arg->stop = 1;
			break;
		}
		arg->count++;
	}
}

static const struct Qdisc_class_ops mq_class_ops = {
	.select_queue	= mq_select_queue,
	.graft		= mq_graft,
	.leaf		= mq_leaf,
	.get		= mq_get,
	.put		= mq_put,
	.walk		= mq_walk,
	.dump		= mq_dump_class,
	.dump_stats	= mq_dump_class_stats,
};

struct Qdisc_ops mq_qdisc_ops __read_mostly = {
	.cl_ops		= &mq_class_ops,
	.id		= "htb",
	.priv_size	= sizeof(struct mq_sched),
	.init		= mq_init,
	.destroy	= mq_destroy,
	.attach		= mq_attach,
	.dump		= mq_dump,
	.owner		= THIS_MODULE,
};

static netdev_tx_t (*old_ndo_start_xmit)(struct sk_buff *, struct net_device *);
netdev_tx_t iso_ndo_start_xmit(struct sk_buff *, struct net_device *);
rx_handler_result_t iso_rx_handler(struct sk_buff **);

int iso_tx_hook_init(struct iso_tx_context *);
void iso_tx_hook_exit(struct iso_tx_context *);

int iso_rx_hook_init(struct iso_rx_context *);
void iso_rx_hook_exit(struct iso_rx_context *);

enum iso_verdict iso_tx(struct sk_buff *skb, const struct net_device *out, struct iso_tx_context *);
enum iso_verdict iso_rx(struct sk_buff *skb, const struct net_device *in, struct iso_rx_context *);

/* Called with bh disabled */
inline void skb_xmit(struct sk_buff *skb) {
	struct netdev_queue *txq;
	int cpu;
	int locked = 0;

	if(likely(old_ndo_start_xmit != NULL)) {
		cpu = smp_processor_id();
		txq = netdev_get_tx_queue(iso_netdev, skb_get_queue_mapping(skb));

		if(txq->xmit_lock_owner != cpu) {
			HARD_TX_LOCK(iso_netdev, txq, cpu);
			locked = 1;
		}
		/* XXX: will the else condition happen? */

		if(!netif_tx_queue_stopped(txq)) {
			old_ndo_start_xmit(skb, iso_netdev);
		} else {
			kfree_skb(skb);
		}

		if(locked) {
			HARD_TX_UNLOCK(iso_netdev, txq);
		}
	}
}

int iso_tx_hook_init(struct iso_tx_context *context) {
	struct net_device_ops *ops;
	struct net_device *netdev = context->netdev;

	if(netdev == NULL || netdev->netdev_ops == NULL)
		return 1;

	ops = (struct net_device_ops *)netdev->netdev_ops;
	context->xmit = ops->ndo_start_xmit;
	synchronize_net();
	return 0;
}

void iso_tx_hook_exit(struct iso_tx_context *txctx) {
	kfree(txctx);
	synchronize_net();
}

int iso_rx_hook_init(struct iso_rx_context *rxctx) {
	int ret = 0;

	rtnl_lock();
	ret = netdev_rx_handler_register(rxctx->netdev, iso_rx_handler, NULL);
	rtnl_unlock();

	/* Wait till stack sees our new handler */
	synchronize_net();
	return ret;
}

void iso_rx_hook_exit(struct iso_rx_context *rxctx) {
	rtnl_lock();
	netdev_rx_handler_unregister(rxctx->netdev);
	rtnl_unlock();
	synchronize_net();
}

static int iso_enqueue(struct sk_buff *skb, struct Qdisc *sch) {
	enum iso_verdict verdict;
	struct net_device *out = qdisc_dev(sch);
	int ret = NET_XMIT_SUCCESS;

	skb_reset_mac_header(skb);
	verdict = iso_tx(skb, out, iso_txctx_dev(out));

	switch(verdict) {
	case ISO_VERDICT_DROP:
		ret = NET_XMIT_DROP;
		kfree_skb(skb);
		break;

	case ISO_VERDICT_PASS:
		skb_xmit(skb);
		break;

	case ISO_VERDICT_SUCCESS:
	case ISO_VERDICT_ERROR:
	default:
		break;
	}

	return ret;
}

rx_handler_result_t iso_rx_handler(struct sk_buff **pskb) {
	struct sk_buff *skb = *pskb;
	enum iso_verdict verdict;
	struct net_device *in = skb->dev;
	struct iso_rx_context *rxctx;

	if(unlikely(skb->pkt_type == PACKET_LOOPBACK))
		return RX_HANDLER_PASS;

	if (unlikely(!iso_enabled(in)))
		return RX_HANDLER_PASS;

	rxctx = iso_rxctx_dev(in);
	verdict = iso_rx(skb, iso_netdev, rxctx);

	switch(verdict) {
	case ISO_VERDICT_DROP:
		kfree_skb(skb);
		return RX_HANDLER_CONSUMED;

	case ISO_VERDICT_SUCCESS:
	case ISO_VERDICT_PASS:
	default:
		return RX_HANDLER_PASS;
	}

	/* Unreachable */
	return RX_HANDLER_PASS;
}

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
