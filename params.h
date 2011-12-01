#include <linux/types.h>

extern u64 ISO_FALPHA;

/* All rates are in Mbps */
extern u64 ISO_MAX_TX_RATE;

// The VQ's net drain rate in Mbps is 90% of 10G ~ 9000 Mbps
extern u64 ISO_VQ_DRAIN_RATE_MBPS;
extern u64 ISO_MAX_BURST_TIME_US;
extern u64 ISO_MIN_BURST_BYTES;
extern u64 ISO_RATEMEASURE_INTERVAL_US;
extern u64 ISO_TOKENBUCKET_TIMEOUT_NS;
extern u64 ISO_TOKENBUCKET_MARK_THRESH_BYTES;
extern u64 ISO_TOKENBUCKET_DROP_THRESH_BYTES;
extern u64 ISO_VQ_MARK_THRESH_BYTES;
extern u64 ISO_VQ_MAX_BYTES;
extern u64 ISO_RFAIR_INITIAL;
extern u64 ISO_MIN_RFAIR;
extern u64 ISO_RFAIR_INCREMENT;
extern u64 ISO_RFAIR_DECREASE_INTERVAL_US;
extern u64 ISO_RFAIR_INCREASE_INTERVAL_US;
extern u64 ISO_RFAIR_FEEDBACK_TIMEOUT_US;
extern u64 ISO_RFAIR_FEEDBACK_TIMEOUT_DEFAULT_RATE;
extern u64 IsoGlobalEnabled;
extern u64 IsoEnablePortClassMap;

// DEBUG: setting it to 666 means we will ALWAYS generate feedback for
// EVERY packet!
// USE IT ONLY FOR DEBUGGING.  You've been warned.
extern u64 IsoAlwaysFeedback;

// This param is a fail-safe.  If anything goes wrong and we reboot,
// we recover to a fail-safe state.
extern u64 IsoAutoGenerateFeedback;
extern u64 ISO_FEEDBACK_INTERVAL_US;

// TODO: We are assuming that we don't need to do any VLAN tag
// ourselves
extern const u64 ISO_FEEDBACK_PACKET_SIZE;
extern const u16 ISO_FEEDBACK_HEADER_SIZE;
extern const u8 ISO_FEEDBACK_PACKET_TTL;
extern u64 ISO_FEEDBACK_PACKET_IPPROTO; // should be some unused protocol

// New parameters
extern u64 ISO_RL_UPDATE_INTERVAL_US;
extern int ISO_BURST_FACTOR;
extern u64 ISO_VQ_UPDATE_INTERVAL_US;

// MUST be 1 less than a power of 2
#define ISO_MAX_QUEUE_LEN_PKT (127)

struct iso_param {
	char name[64];
	u64 *ptr;
};

extern struct iso_param iso_params[32];
extern int iso_num_params;

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
