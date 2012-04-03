rmmod perfiso
dmesg -c
insmod ./perfiso.ko iso_param_dev=peth2
#echo -n peth2 > /sys/module/perfiso/parameters/txc

for class in 00:00:00:00:01:01 00:00:00:00:01:02; do
  echo -n $class > /sys/module/perfiso/parameters/create_txc
  echo -n $class > /sys/module/perfiso/parameters/create_vq
  echo associate txc $class vq $class \
    > /sys/module/perfiso/parameters/assoc_txc_vq
done

