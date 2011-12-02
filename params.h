#ifndef __PARAMS_H__
#define __PARAMS_H__


#include <linux/types.h>

extern int ISO_FALPHA;

/* All rates are in Mbps */
extern int ISO_MAX_TX_RATE;

// The VQ's net drain rate in Mbps is 90% of 10G ~ 9000 Mbps
extern int ISO_VQ_DRAIN_RATE_MBPS;
extern int ISO_MAX_BURST_TIME_US;
extern int ISO_MIN_BURST_BYTES;
extern int ISO_RATEMEASURE_INTERVAL_US;
extern int ISO_TOKENBUCKET_TIMEOUT_NS;
extern int ISO_TOKENBUCKET_MARK_THRESH_BYTES;
extern int ISO_TOKENBUCKET_DROP_THRESH_BYTES;
extern int ISO_VQ_MARK_THRESH_BYTES;
extern int ISO_VQ_MAX_BYTES;
extern int ISO_RFAIR_INITIAL;
extern int ISO_MIN_RFAIR;
extern int ISO_RFAIR_INCREMENT;
extern int ISO_RFAIR_DECREASE_INTERVAL_US;
extern int ISO_RFAIR_INCREASE_INTERVAL_US;
extern int ISO_RFAIR_FEEDBACK_TIMEOUT_US;
extern int ISO_RFAIR_FEEDBACK_TIMEOUT_DEFAULT_RATE;
extern int IsoGlobalEnabled;
extern int IsoEnablePortClassMap;

// DEBUG: setting it to 666 means we will ALWAYS generate feedback for
// EVERY packet!
// USE IT ONLY FOR DEBUGGING.  You've been warned.
extern int IsoAlwaysFeedback;

// This param is a fail-safe.  If anything goes wrong and we reboot,
// we recover to a fail-safe state.
extern int IsoAutoGenerateFeedback;
extern int ISO_FEEDBACK_INTERVAL_US;

// TODO: We are assuming that we don't need to do any VLAN tag
// ourselves
extern const int ISO_FEEDBACK_PACKET_SIZE;
extern const u16 ISO_FEEDBACK_HEADER_SIZE;
extern const u8 ISO_FEEDBACK_PACKET_TTL;
extern int ISO_FEEDBACK_PACKET_IPPROTO; // should be some unused protocol

// New parameters
extern int ISO_RL_UPDATE_INTERVAL_US;
extern int ISO_BURST_FACTOR;
extern int ISO_VQ_UPDATE_INTERVAL_US;

// MUST be 1 less than a power of 2
#define ISO_MAX_QUEUE_LEN_PKT (127)

/* These MUST be a power of 2 as well */
#define ISO_MAX_TX_BUCKETS (32)
#define ISO_MAX_RL_BUCKETS (128)
#define ISO_MAX_STATE_BUCKETS (256)

struct iso_param {
	char name[64];
	int *ptr;
};

extern struct iso_param iso_params[32];
extern int iso_num_params;

int iso_params_init(void);
void iso_params_exit(void);


#endif /* __PARAMS_H__ */

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
