
#include <linux/sysctl.h>
#include "params.h"

// params
int ISO_FALPHA = 2;
/* All rates are in Mbps */
int ISO_MAX_TX_RATE = 10000;
// The VQ's net drain rate in Mbps is 90% of 10G ~ 9000 Mbps
int ISO_VQ_DRAIN_RATE_MBPS = 9000;
int ISO_MAX_BURST_TIME_US = 1000;
int ISO_MIN_BURST_BYTES = 1;
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

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */

