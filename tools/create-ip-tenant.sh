#!/bin/bash

ip=$1
dir=/sys/module/perfiso/parameters

echo $ip > $dir/create_txc
echo $ip > $dir/create_vq
echo associate txc $ip vq $ip > $dir/assoc_txc_vq
echo $ip weight 1 > $dir/set_vq_weight

