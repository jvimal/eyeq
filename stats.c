#include <linux/version.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include "stats.h"
#include "tx.h"
#include "vq.h"

extern char *iso_param_dev;

static void *iso_stats_proc_seq_start(struct seq_file *s, loff_t *pos)
{
	static unsigned long counter = 0;
	if (*pos == 0) {
		return &counter;
	}
	else {
		*pos = 0;
		return NULL;
	}
}

static void *iso_stats_proc_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	unsigned long *index = (unsigned long *)v;
	(*index)++;

	return NULL;
}

static void iso_stats_proc_seq_stop(struct seq_file *s, void *v)
{
	/* nothing to do, we use a static value in start() */
}

static int iso_stats_proc_seq_show(struct seq_file *s, void *v)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct iso_tx_class *txc;
	struct iso_vq *vq, *vq_next;
	struct iso_tx_context *txctx, *txctx_next;
	struct iso_rx_context *rxctx, *rxctx_next;
	int i;

	for_each_tx_context(txctx) {
		seq_printf(s, "tx->dev %s, tx_rate %u, rate %u\n",
			   txctx->netdev->name, txctx->tx_rate, txctx->rate);

		for(i = 0; i < ISO_MAX_TX_BUCKETS; i++) {
			head = &txctx->iso_tx_bucket[i];
			hlist_for_each_entry_rcu(txc, node, head, hash_node) {
				iso_txc_show(txc, s);
			}
		}
	}

	for_each_rx_context(rxctx) {
		seq_printf(s, "\nvqs->dev %s   total_tokens %lld   last_update %llx   active_rate %d\n",
			   rxctx->netdev->name,
			   rxctx->vq_total_tokens, rxctx->vq_last_update_time.tv64, atomic_read(&rxctx->vq_active_rate));

		for_each_vq(vq, rxctx) {
			iso_vq_show(vq, s);
		}
	}

	return 0;
}

static int iso_csvstats_proc_seq_show(struct seq_file *s, void *v) {
	struct iso_tx_context *txctx, *txctx_next;
	struct iso_rx_context *rxctx, *rxctx_next;
	struct iso_tx_class *txc, *txc_next;
	struct iso_rl *rl;
	struct iso_vq *vq, *vq_next;
	struct iso_vq_stats *stats;

	int i;
	u64 rx_bytes;
	char buff[128];

	for_each_tx_context(txctx) {
		for_each_txc(txc, txctx) {
			rl = &txc->rl;
			iso_class_show(txc->klass, buff);
			seq_printf(s, "tx,%s,%llu\n", buff, txc->rl.accum_xmit);
		}
	}

	for_each_rx_context(rxctx) {
	for_each_vq(vq, rxctx) {
		rx_bytes = 0;
		iso_class_show(vq->klass, buff);
		for_each_online_cpu(i) {
			stats = per_cpu_ptr(vq->percpu_stats, i);
			rx_bytes += stats->rx_bytes;
		}
		seq_printf(s, "rx,%s,%llu\n", buff, rx_bytes);
	}
	}

	return 0;
}

static struct proc_dir_entry *iso_stats_proc;

static struct seq_operations iso_stats_proc_seq_ops = {
	.start = iso_stats_proc_seq_start,
	.next = iso_stats_proc_seq_next,
	.stop = iso_stats_proc_seq_stop,
	.show = iso_stats_proc_seq_show
};

static int iso_stats_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &iso_stats_proc_seq_ops);
}

static struct file_operations iso_stats_proc_file_ops = {
	.owner = THIS_MODULE,
	.open = iso_stats_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};


/* For program friendly CSV stats */
static struct proc_dir_entry *iso_csvstats_proc;

static struct seq_operations iso_csvstats_proc_seq_ops = {
	.start = iso_stats_proc_seq_start,
	.next = iso_stats_proc_seq_next,
	.stop = iso_stats_proc_seq_stop,
	.show = iso_csvstats_proc_seq_show
};

static int iso_csvstats_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &iso_csvstats_proc_seq_ops);
}

static struct file_operations iso_csvstats_proc_file_ops = {
	.owner = THIS_MODULE,
	.open = iso_csvstats_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};


int iso_stats_init() {
	int ret = 0;

	iso_stats_proc = create_proc_entry(ISO_STATS_PROC_NAME, 0, NULL);
	if(iso_stats_proc) {
		iso_stats_proc->proc_fops = &iso_stats_proc_file_ops;
	} else {
		ret = 1;
		goto out;
	}

	iso_csvstats_proc = create_proc_entry(ISO_CSVSTATS_PROC_NAME, 0, NULL);
	if(iso_csvstats_proc) {
		iso_csvstats_proc->proc_fops = &iso_csvstats_proc_file_ops;
	} else {
		ret = 1;
		remove_proc_entry(ISO_STATS_PROC_NAME, NULL);
		goto out;
	}

 out:
	return ret;
}

void iso_stats_exit() {
	remove_proc_entry(ISO_STATS_PROC_NAME, NULL);
	remove_proc_entry(ISO_CSVSTATS_PROC_NAME, NULL);
}

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
