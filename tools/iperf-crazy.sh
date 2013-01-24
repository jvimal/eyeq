#!/bin/bash

opts="-P 4"

iperf -B 11.0.1.1 -c 11.0.1.2 -t 1000 $opts &
sleep 1

iperf -B 11.0.1.1 -c 11.0.2.2 -t 1000 $opts &
sleep 1

iperf -B 11.0.2.1 -c 11.0.3.2 -t 1000 $opts &
sleep 1

iperf -B 11.0.3.1 -c 11.0.3.2 -t 1000 $opts &
sleep 1

wait

