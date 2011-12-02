

int iso_rx_init(void);
void iso_rx_exit(void);
unsigned int iso_rx_bridge(unsigned int hooknum,
						   struct sk_buff *skb,
						   const struct net_device *in,
						   const struct net_device *out,
						   int (*okfn)(struct sk_buff *));

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */

