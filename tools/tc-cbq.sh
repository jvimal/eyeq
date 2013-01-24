
DEV=eth2

tc qdisc del dev $DEV root

tc qdisc add dev $DEV root handle 1: cbq bandwidth 10Gbit avpkt 1500

tc class add dev $DEV parent 1: classid 1:1 cbq weight 1 bandwidth 10Gbit rate 1Gbit maxburst 20 avpkt 1500
tc class add dev $DEV parent 1: classid 1:2 cbq weight 1 bandwidth 10Gbit rate 9Gbit maxburst 20 avpkt 1500

tc filter add dev $DEV protocol ip parent 1: prio 1 \
  u32 match ip src 11.0.1.1 flowid 1:1

tc filter add dev $DEV protocol ip parent 1: prio 1 \
  u32 match ip src 11.0.1.2 flowid 1:2

