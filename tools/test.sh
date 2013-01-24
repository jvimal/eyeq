make

rmmod perfiso

insmod ./perfiso.ko iso_param_dev=eth2
echo 11.0.1.1 > /sys/module/perfiso/parameters/create_txc
echo 11.0.1.2 > /sys/module/perfiso/parameters/create_txc

echo 11.0.1.1 > /sys/module/perfiso/parameters/create_vq
echo 11.0.1.2 > /sys/module/perfiso/parameters/create_vq

echo associate txc 11.0.1.1 vq 11.0.1.1 > /sys/module/perfiso/parameters/assoc_txc_vq
echo associate txc 11.0.1.2 vq 11.0.1.2 > /sys/module/perfiso/parameters/assoc_txc_vq

