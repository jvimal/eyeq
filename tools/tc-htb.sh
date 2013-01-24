
DEV=eth2

tc qdisc del dev $DEV root

tc qdisc add dev $DEV root handle 1: htb default 1000
tc class add dev $DEV parent 1: classid 1:1 htb rate 10Gbit burst 30k mtu 64000
tc class add dev $DEV parent 1:1 classid 1:11 htb rate 3Gbit ceil 10Gbit burst 30k mtu 64000
tc class add dev $DEV parent 1:1 classid 1:12 htb rate 6Gbit ceil 10Gbit burst 30k mtu 64000

tc filter add dev $DEV protocol ip parent 1: prio 1 \
  u32 match ip src 11.0.0.0/8 flowid 1:1

tc filter add dev $DEV protocol ip parent 1:1 prio 1 \
  u32 match ip src 11.0.1.1 flowid 1:11

tc filter add dev $DEV protocol ip parent 1:1 prio 1 \
  u32 match ip src 11.0.1.2 flowid 1:12

