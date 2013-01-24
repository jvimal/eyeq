#!/bin/bash

rmmod perfiso
make
insmod ./perfiso.ko iso_param_dev=eth2
bash tools/create-ip-tenant.sh 11.0.1.1
bash tools/create-ip-tenant.sh 11.0.2.1

