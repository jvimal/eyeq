#!/bin/bash

mod=$1
modpath=$mod.ko

if [ "$mod" == "" ]; then
    mod=openvswitch_mod
fi

cd /sys/module/$mod/sections
echo add-symbol-file \'$modpath\' `cat .text` \\

for sec in .[a-z]*; do
    if [ $sec != ".text" ]; then
        echo "-s $sec `cat $sec` \\"
    fi
done

