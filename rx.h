
#include "tx.h"

int iso_rx_init(void);
void iso_rx_exit(void);

enum iso_verdict iso_rx(struct sk_buff *skb, const struct net_device *out);

inline iso_class_t iso_rx_classify(struct sk_buff *);

int iso_vq_install(char *);

inline int iso_generate_feedback(int bit, struct sk_buff *pkt);
inline int iso_is_generated_feedback(struct sk_buff *);
/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */

