ifconfig eth2 0
brctl addbr br0
brctl addif br0 eth2
ifconfig br0 192.168.2.1 up

#ifconfig eth2 192.168.2.1 up

# Outgoing
iptables -F
iptables -A OUTPUT ! --dst 192.168.2.1 -p tcp --dport 5001 -j MARK --set-mark 1
iptables -A OUTPUT --src 192.168.2.1 -p tcp --sport 5001 -j MARK --set-mark 1

# Incoming
ebtables -t broute -F 
ebtables -t broute -A BROUTING -p ip --ip-proto tcp --ip-dport 5001 --in-if eth2 -j mark --set-mark 1
ebtables -t broute -A BROUTING -p ip --ip-proto tcp --ip-sport 5001 --in-if eth2 -j mark --set-mark 1

rmmod perfiso
insmod ./perfiso.ko iso_param_dev=eth2

function preset_fine_rl {
  pushd /proc/sys/perfiso
  
  echo 32 > ISO_FALPHA
  echo 1 > IsoAutoGenerateFeedback
  echo 200 > ISO_FEEDBACK_INTERVAL_US
  echo 1 > ISO_RFAIR_INCREMENT
  echo 500 > ISO_RFAIR_INCREASE_INTERVAL_US
  echo 500 > ISO_RFAIR_DECREASE_INTERVAL_US

  popd
}

echo 1 > /proc/sys/perfiso/IsoAutoGenerateFeedback
#preset_fine_rl

for class in 1 2; do
  echo -n $class > /sys/module/perfiso/parameters/create_txc
  echo -n $class > /sys/module/perfiso/parameters/create_vq
  echo associate txc $class vq $class \
        > /sys/module/perfiso/parameters/assoc_txc_vq
done
