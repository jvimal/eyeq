
#include <linux/sysctl.h>
#include <linux/moduleparam.h>
#include <linux/stat.h>

#include "params.h"
#include "tx.h"
#include "rx.h"
#include "vq.h"

// params
int ISO_FALPHA = 2;
/* All rates are in Mbps */
int ISO_MAX_TX_RATE = 10000;
// The VQ's net drain rate in Mbps is 90% of 10G ~ 9000 Mbps
int ISO_VQ_DRAIN_RATE_MBPS = 9000;
int ISO_MAX_BURST_TIME_US = 5000;
int ISO_MIN_BURST_BYTES = 65536;
int ISO_RATEMEASURE_INTERVAL_US = 1000 * 100;
int ISO_TOKENBUCKET_TIMEOUT_NS = 1000 * 1000;
int ISO_TOKENBUCKET_MARK_THRESH_BYTES = 512 * 1024;
int ISO_TOKENBUCKET_DROP_THRESH_BYTES = 512 * 1024;
int ISO_VQ_MARK_THRESH_BYTES = 1024 * 1000;
int ISO_VQ_MAX_BYTES = 2048 * 1024;
int ISO_RFAIR_INITIAL = 100;
int ISO_MIN_RFAIR = 1;
int ISO_RFAIR_INCREMENT = 10;
int ISO_RFAIR_DECREASE_INTERVAL_US = 5000;
int ISO_RFAIR_INCREASE_INTERVAL_US = 5000;
int ISO_RFAIR_FEEDBACK_TIMEOUT_US = 1000 * 1000;
int ISO_RFAIR_FEEDBACK_TIMEOUT_DEFAULT_RATE = 10;
int IsoGlobalEnabled = 0;
int IsoEnablePortClassMap = 0;

// DEBUG: setting it to 666 means we will ALWAYS generate feedback for
// EVERY packet!
// USE IT ONLY FOR DEBUGGING.  You've been warned.
int IsoAlwaysFeedback = 0;

// This param is a fail-safe.  If anything goes wrong and we reboot,
// we recover to a fail-safe state.
int IsoAutoGenerateFeedback = 0;
int ISO_FEEDBACK_INTERVAL_US = 500;

// TODO: We are assuming that we don't need to do any VLAN tag
// ourselves
const int ISO_FEEDBACK_PACKET_SIZE = 64;
const u16 ISO_FEEDBACK_HEADER_SIZE = 14 + 20;
const u8 ISO_FEEDBACK_PACKET_TTL = 64;
int ISO_FEEDBACK_PACKET_IPPROTO = 143; // should be some unused protocol

// New parameters
int ISO_RL_UPDATE_INTERVAL_US = 200;
int ISO_BURST_FACTOR = 8;
int ISO_VQ_UPDATE_INTERVAL_US = 100;

struct iso_param iso_params[32] = {
  {"ISO_FALPHA", &ISO_FALPHA },
  {"ISO_MAX_TX_RATE", &ISO_MAX_TX_RATE },
  {"ISO_VQ_DRAIN_RATE_MBPS", &ISO_VQ_DRAIN_RATE_MBPS },
  {"ISO_MAX_BURST_TIME_US", &ISO_MAX_BURST_TIME_US },
  {"ISO_MIN_BURST_BYTES", &ISO_MIN_BURST_BYTES },
  {"ISO_RATEMEASURE_INTERVAL_US", &ISO_RATEMEASURE_INTERVAL_US },
  {"ISO_TOKENBUCKET_TIMEOUT_NS", &ISO_TOKENBUCKET_TIMEOUT_NS },
  {"ISO_TOKENBUCKET_MARK_THRESH_BYTES", &ISO_TOKENBUCKET_MARK_THRESH_BYTES },
  {"ISO_TOKENBUCKET_DROP_THRESH_BYTES", &ISO_TOKENBUCKET_DROP_THRESH_BYTES },
  {"ISO_VQ_MARK_THRESH_BYTES", &ISO_VQ_MARK_THRESH_BYTES },
  {"ISO_VQ_MAX_BYTES", &ISO_VQ_MAX_BYTES },
  {"ISO_RFAIR_INITIAL", &ISO_RFAIR_INITIAL },
  {"ISO_MIN_RFAIR", &ISO_MIN_RFAIR },
  {"ISO_RFAIR_INCREMENT", &ISO_RFAIR_INCREMENT },
  {"ISO_RFAIR_DECREASE_INTERVAL_US", &ISO_RFAIR_DECREASE_INTERVAL_US },
  {"ISO_RFAIR_INCREASE_INTERVAL_US", &ISO_RFAIR_INCREASE_INTERVAL_US },
  {"ISO_RFAIR_FEEDBACK_TIMEOUT", &ISO_RFAIR_FEEDBACK_TIMEOUT_US },
  {"ISO_RFAIR_FEEDBACK_TIMEOUT_DEFAULT_RATE", &ISO_RFAIR_FEEDBACK_TIMEOUT_DEFAULT_RATE },
  {"IsoGlobalEnabled", &IsoGlobalEnabled },
  {"IsoEnablePortClassMap", &IsoEnablePortClassMap },
  {"IsoAlwaysFeedback", &IsoAlwaysFeedback },
  {"IsoAutoGenerateFeedback", &IsoAutoGenerateFeedback },
  {"ISO_FEEDBACK_PACKET_IPPROTO", &ISO_FEEDBACK_PACKET_IPPROTO },
  {"ISO_FEEDBACK_INTERVAL_US", &ISO_FEEDBACK_INTERVAL_US },
  {"ISO_RL_UPDATE_INTERVAL_US", &ISO_RL_UPDATE_INTERVAL_US },
  {"ISO_BURST_FACTOR", &ISO_BURST_FACTOR },
  {"ISO_VQ_UPDATE_INTERVAL_US", &ISO_VQ_UPDATE_INTERVAL_US },
  {"", NULL},
};

int iso_num_params = 27;
struct ctl_table iso_params_table[32];
struct ctl_path iso_params_path[] = {
	{ .procname = "perfiso" },
	{ },
};
struct ctl_table_header *iso_sysctl;

int iso_params_init() {
	int i;

	memset(iso_params_table, 0, sizeof(iso_params_table));

	for(i = 0; i < iso_num_params; i++) {
		struct ctl_table *entry = &iso_params_table[i];
		entry->procname = iso_params[i].name;
		entry->data = iso_params[i].ptr;
		entry->maxlen = sizeof(int);
		entry->mode = 0644;
		entry->proc_handler = proc_dointvec;
	}

	iso_sysctl = register_sysctl_paths(iso_params_path, iso_params_table);
	if(iso_sysctl == NULL)
		goto err;

	return 0;

 err:
	return -1;
}

void iso_params_exit() {
	unregister_sysctl_table(iso_sysctl);
}

/*
 * Create a new TX context with a specific filter
 * If compiled with CLASS_DEV
 * echo -n eth0 > /sys/module/perfiso/parameters/create_txc
 *
 * If compiled with CLASS_ETHER_SRC
 * echo -n 00:00:00:00:01:01 > /sys/module/perfiso/parameters/create_txc
 */
static int iso_sys_create_txc(const char *val, struct kernel_param *kp) {
	char buff[128];
	int len, ret;

	len = min(127, (int)strlen(val));
	strncpy(buff, val, len);
	buff[len] = '\0';

#if defined ISO_TX_CLASS_DEV
	ret = iso_txc_dev_install(buff);
#elif defined ISO_TX_CLASS_ETHER_SRC
	ret = iso_txc_ether_src_install(buff);
#endif

	if(ret)
		return -EINVAL;

	printk(KERN_INFO "perfiso: created tx context for class %s\n", buff);
	return 0;
}

static int iso_sys_noget(const char *val, struct kernel_param *kp) {
  return 0;
}

module_param_call(create_txc, iso_sys_create_txc, iso_sys_noget, NULL, S_IWUSR);

/*
 * Create a new RX context (vq) with a specific filter
 * If compiled with CLASS_DEV
 * echo -n eth0 > /sys/module/perfiso/parameters/create_vq
 *
 * If compiled with CLASS_ETHER_SRC
 * echo -n 00:00:00:00:01:01 > /sys/module/perfiso/parameters/create_vq
 */
static int iso_sys_create_vq(const char *val, struct kernel_param *kp) {
	char buff[128];
	int len, ret;

	len = min(127, (int)strlen(val));
	strncpy(buff, val, len);
	buff[len] = '\0';

#if defined ISO_TX_CLASS_DEV
	ret = iso_vq_dev_install(buff);
#elif defined ISO_TX_CLASS_ETHER_SRC
	ret = iso_vq_ether_src_install(buff);
#endif

	if(ret)
		return -EINVAL;

	printk(KERN_INFO "perfiso: created vq for class %s\n", buff);
	return 0;
}

module_param_call(create_vq, iso_sys_create_vq, iso_sys_noget, NULL, S_IWUSR);

/*
 * Associate the TX path with a VQ.
 * echo -n associate txc 00:00:00:00:01:01 vq 00:00:00:00:01:01
 * > /sys/module/perfiso/parameters/assoc_txc_vq
 */
static int iso_sys_assoc_txc_vq(const char *val, struct kernel_param *kp) {
	char _txc[128], _vqc[128];
	iso_class_t txclass, vqclass;
	struct iso_tx_class *txc;
	struct iso_vq *vq;

	int n, ret = 0;

	n = sscanf(val, "associate txc %s vq %s", _txc, _vqc);
	if(n != 2) {
		ret = -EINVAL;
		goto out;
	}

	txclass = iso_class_parse(_txc);
	vqclass = iso_class_parse(_vqc);

	txc = iso_txc_find(txclass);
	if(txc == NULL) {
		ret = -EINVAL;
		goto out;
	}

	vq = iso_vq_find(vqclass);
	if(vq == NULL) {
		printk(KERN_INFO "perfiso: Could not find vq %s\n", _vqc);
		ret = -EINVAL;
		goto out;
	}

	/* XXX: locks?  synchronisation? */
	if(txc->vq) {
		atomic_dec(&txc->vq->refcnt);
	}

	txc->vq = vq;
	atomic_inc(&vq->refcnt);

	printk(KERN_INFO "perfiso: Associated txc %s with vq %s\n",
		   _txc, _vqc);
 out:
	return ret;
}

module_param_call(assoc_txc_vq, iso_sys_assoc_txc_vq, iso_sys_noget, NULL, S_IWUSR);

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */

