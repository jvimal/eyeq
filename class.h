#ifndef __CLASS_H__
#define __CLASS_H__


#include <linux/skbuff.h>

/*
 * Classification can be based on skb->dev, src hwaddr, ip, tcp, etc.
 * It is your job to ensure that exactly ONE of the #defines are
 * defined, preferably in your Makefile.
 */
// #define ISO_TX_CLASS_ETHER_SRC
// #define ISO_TX_CLASS_DEV
// #define ISO_TX_CLASS_MARK

#if defined ISO_TX_CLASS_DEV
typedef struct net_device *iso_class_t;
#elif defined ISO_TX_CLASS_ETHER_SRC
typedef struct {
	u8 addr[ETH_ALEN];
}iso_class_t;
#elif defined ISO_TX_CLASS_MARK
typedef u32 iso_class_t;
#elif defined ISO_TX_CLASS_IPADDR
typedef u32 iso_class_t;
#endif

/* Maybe we need an "ops" structure */
static inline iso_class_t iso_txc_classify(struct sk_buff *);
static inline void iso_class_free(iso_class_t);
static inline int iso_class_cmp(iso_class_t a, iso_class_t b);
static inline u32 iso_class_hash(iso_class_t);
static inline void iso_class_show(iso_class_t, char *);
static inline iso_class_t iso_class_parse(char*);
static inline struct iso_tx_class *iso_txc_find(iso_class_t);


/* First attempt: out device classification.  Its address is usually
   aligned, so shift out the zeroes */

#if defined ISO_TX_CLASS_DEV
static inline iso_class_t iso_txc_classify(struct sk_buff *pkt) {
	return pkt->dev;
}

static inline void iso_class_free(iso_class_t klass) {
	dev_put((struct net_device *)klass);
}

static inline int iso_class_cmp(iso_class_t a, iso_class_t b) {
	return (u64)a - (u64)b;
}

static inline u32 iso_class_hash(iso_class_t klass) {
	return (u32) ((u64)klass >> 12);
}

static inline void iso_class_show(iso_class_t klass, char *buff) {
	sprintf(buff, "%p", klass);
}

/* XXX: should this routine do "dev_put" as well?  Maybe it should, as
 *  we will not be dereferencing iso_class_t anyway?
 */

static inline iso_class_t iso_class_parse(char *devname) {
	struct net_device *dev;

	rcu_read_lock();
	dev = dev_get_by_name_rcu(&init_net, devname);
	rcu_read_unlock();

	dev_put(dev);
	return dev;
}

static inline iso_class_t iso_rx_classify(struct sk_buff *skb) {
	return skb->dev;
}
#elif defined ISO_TX_CLASS_ETHER_SRC
static inline iso_class_t iso_txc_classify(struct sk_buff *skb) {
	iso_class_t ret;
	memcpy((unsigned char *)&ret, eth_hdr(skb)->h_source, ETH_ALEN);
	return ret;
}

static inline void iso_class_free(iso_class_t klass) {}

static inline int iso_class_cmp(iso_class_t a, iso_class_t b) {
	return memcmp(&a, &b, sizeof(a));
}

static inline u32 iso_class_hash(iso_class_t klass) {
	return jhash((void *)&klass, sizeof(iso_class_t), 0);
}

/* Just lazy, looks weird */
static inline void iso_class_show(iso_class_t klass, char *buff) {
#define O "%02x:"
#define OO "%02x"
	u8 *o = &klass.addr[0];
	sprintf(buff, O O O O O OO,
			o[0], o[1], o[2], o[3], o[4], o[5]);
#undef O
#undef OO
}

static inline iso_class_t iso_class_parse(char *hwaddr) {
	iso_class_t ret = {.addr = {0}};
	mac_pton(hwaddr, (u8*)&ret);
	return ret;
}

static inline iso_class_t iso_rx_classify(struct sk_buff *skb) {
	iso_class_t klass;
	memcpy((void *)&klass, eth_hdr(skb)->h_dest, ETH_ALEN);
	return klass;
}

#elif defined ISO_TX_CLASS_MARK
static inline iso_class_t iso_txc_classify(struct sk_buff *skb) {
	return skb->mark;
}

static inline void iso_class_free(iso_class_t klass) {}

static inline int iso_class_cmp(iso_class_t a, iso_class_t b) {
	return a - b;
}

/* We don't do any bit mixing here; it's for ease of use */
static inline u32 iso_class_hash(iso_class_t klass) {
	return klass;
}

/* Just lazy, looks weird */
static inline void iso_class_show(iso_class_t klass, char *buff) {
	sprintf(buff, "%d", klass);
}

static inline iso_class_t iso_class_parse(char *hwaddr) {
	int ret = 0;
	sscanf(hwaddr, "%d", &ret);
	return ret;
}

static inline iso_class_t iso_rx_classify(struct sk_buff *skb) {
	return skb->mark;
}

#elif defined ISO_TX_CLASS_IPADDR
static inline iso_class_t iso_txc_classify(struct sk_buff *skb) {
	struct ethhdr *eth;
	u32 addr = 0;

	eth = eth_hdr(skb);
	if(likely(eth->h_proto == htons(ETH_P_IP))) {
		addr = ip_hdr(skb)->saddr;
	}

	return addr;
}

static inline void iso_class_free(iso_class_t klass) {}

static inline int iso_class_cmp(iso_class_t a, iso_class_t b) {
	return a - b;
}

/* We don't do any bit mixing here; it's for ease of use */
static inline u32 iso_class_hash(iso_class_t klass) {
	return klass;
}

static inline void iso_class_show(iso_class_t klass, char *buff) {
	u32 addr = htonl(klass);
	sprintf(buff, "%u.%u.%u.%u",
			(addr & 0xFF000000) >> 24,
			(addr & 0x00FF0000) >> 16,
			(addr & 0x0000FF00) >> 8,
			(addr & 0x000000FF));
}

static inline iso_class_t iso_class_parse(char *ipaddr) {
	u32 addr, oct[4];
	int n;

	addr = 0;
	n = sscanf(ipaddr, "%u.%u.%u.%u", oct, oct+1, oct+2, oct+3);
	if(n == 4) {
		addr = (oct[0] << 24) | (oct[1] << 16) | (oct[2] << 8) | oct[3];
	}
	return htonl(addr);
}

static inline iso_class_t iso_rx_classify(struct sk_buff *skb) {
	iso_class_t klass = 0;
	struct ethhdr *eth = NULL;
	klass = 0;
	eth = eth_hdr(skb);
	if(likely(eth->h_proto == htons(ETH_P_IP))) {
		klass = ip_hdr(skb)->daddr;
	}
	return klass;
}

#endif



#endif /* __CLASS_H__ */

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */
