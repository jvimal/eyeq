#include <linux/netfilter_bridge.h>

/* This is what "br_dev_queue_push_xmit" would do */
inline void skb_xmit(struct sk_buff *skb) {
    skb_push(skb, ETH_HLEN);
    dev_queue_xmit(skb);
}
