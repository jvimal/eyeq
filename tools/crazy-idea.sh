#!/bin/bash

create=tools/create-ip-tenant.sh
rmmod perfiso
insmod perfiso.ko iso_param_dev=eth2

for tid in 1 2 3; do
    echo $tid 11.0.$tid.1
    $create 11.0.$tid.1
    ifconfig eth2:$tid 11.0.$tid.1
done

