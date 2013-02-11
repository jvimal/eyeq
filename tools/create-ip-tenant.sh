#!/bin/bash

ip=$1
dir=/sys/module/perfiso/parameters
dev=eth2

echo dev $dev $ip > $dir/create_txc
echo dev $dev $ip > $dir/create_vq
echo dev $dev associate txc $ip vq $ip > $dir/assoc_txc_vq
echo dev $dev $ip weight 1 > $dir/set_vq_weight

