#include <linux/version.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include "stats.h"
#include "tx.h"
#include "vq.h"

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
	struct iso_vq *vq;

	int i;
	rcu_read_lock();
	for(i = 0; i < ISO_MAX_TX_BUCKETS; i++) {
		head = &iso_tx_bucket[i];
		hlist_for_each_entry_rcu(txc, node, head, hash_node) {
			iso_txc_show(txc, s);
		}
	}

	seq_printf(s, "\nvqs   total_tokens %lld   last_update %llx\n",
			   vq_total_tokens, *(u64 *)&vq_last_update_time);

	for_each_vq(vq) {
		iso_vq_show(vq, s);
	}
	rcu_read_unlock();
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

int iso_stats_init() {
	iso_stats_proc = create_proc_entry(ISO_STATS_PROC_NAME, 0, NULL);
	if(iso_stats_proc) {
		iso_stats_proc->proc_fops = &iso_stats_proc_file_ops;
	}

	return iso_stats_proc == NULL;
}

void iso_stats_exit() {
	remove_proc_entry(ISO_STATS_PROC_NAME, NULL);
}

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
