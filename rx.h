
#include "tx.h"

int iso_rx_init(void);
void iso_rx_exit(void);

enum iso_verdict iso_rx(struct sk_buff *skb, const struct net_device *out);

inline iso_class_t iso_rx_classify(struct sk_buff *);

#if defined ISO_TX_CLASS_DEV
int iso_vq_dev_install(char *);
#elif defined ISO_TX_CLASS_ETHER_SRC
int iso_vq_ether_src_install(char *);
#elif defined ISO_TX_CLASS_MARK
int iso_vq_mark_install(char *);
#endif

inline int iso_generate_feedback(int bit, struct sk_buff *pkt);
inline int iso_is_generated_feedback(struct sk_buff *);
/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */

