
DEV=eth2

tc qdisc del dev $DEV root

tc qdisc add dev $DEV root handle 1: drr
tc class add dev $DEV parent 1: classid 1:1 drr quantum 1500
tc class add dev $DEV parent 1: classid 1:2 drr quantum 1500
tc class add dev $DEV parent 1: classid 1:3 drr quantum 1500

#tc filter add dev $DEV protocol ip parent 1: prio 1 \
#  flow hash keys src divisor 1024 perturb 10

tc filter add dev $DEV protocol ip parent 1: prio 1 \
  u32 match ip src 11.0.1.1 flowid 1:1

tc filter add dev $DEV protocol ip parent 1: prio 1 \
  u32 match ip src 11.0.1.2 flowid 1:2

tc filter add dev $DEV protocol arp parent 1: prio 2  \
  u32 match u8 0 0 flowid 1:3

