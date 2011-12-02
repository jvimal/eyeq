

int iso_tx_init(void);
void iso_tx_exit(void);
unsigned int iso_tx_bridge(unsigned int hooknum,
						   struct sk_buff *skb,
						   const struct net_device *in,
						   const struct net_device *out,
						   int (*okfn)(struct sk_buff *));

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */

