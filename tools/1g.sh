#!/bin/bash

rmmod perfiso
ifdown eth1
ifdown br0
brctl delif br0 eth1
brctl delbr br0
brctl addbr br0
brctl addif br0 eth1
ifconfig eth1 0.0.0.0 up
ifconfig br0 192.168.1.1 up

insmod ./perfiso.ko iso_param_dev=eth1
tools/create-ip-tenant.sh 11.0.1.1
tools/create-ip-tenant.sh 11.0.1.2

tools/pi.py --set 4,1000
tools/pi.py --set 17,100

ifconfig br0:1 11.0.1.1
ifconfig br0:2 11.0.1.2

exit 0

function disable {
	m wredconfig_cell maxdroprate=0xe enable=0
	m wredparam_cell dropstartpoint=240 dropendpoint=240
	s ecn_config 0
}

function enable {
	m wredconfig_cell maxdroprate=0xe enable=1
	m wredparam_cell dropstartpoint=240 dropendpoint=240
	s ecn_config 0xffffff
}
