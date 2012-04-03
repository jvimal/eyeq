#!/bin/bash

tc qdisc del dev eth2 root

tc qdisc add dev eth2 root handle 1: htb default 1
tc class add dev eth2 parent 1: classid 1:1 htb rate 9000Mbit

