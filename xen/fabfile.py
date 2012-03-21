from fabric.api import *

l1 = "l1"
l2 = "l2"
l3 = "l3"
root = [l1, l2, l3]

vm11 = "192.168.2.209"
vm12 = "192.168.2.210"

vm21 = "192.168.2.208"
vm22 = "192.168.2.206"

vm31 = "192.168.2.211"
vm32 = "192.168.2.212"
vm33 = "192.168.2.213"

guest = [vm11, vm12, vm21, vm22, vm31, vm32, vm33]

iface = {
  l1: "eth2",
  l2: "eth2",
  l3: "eth3",
  vm31 : "eth0", # l3
  vm32 : "eth0", # l3
  vm33 : "eth0", # l3

  vm21 : "eth0", # l2
  vm22 : "eth0", # l2

  vm11 : "eth0", # l1
  vm12 : 'eth0', # l1
}

env.roledefs = {
  'root': root,
  'guest': guest,
  'src': [vm31, vm32, vm21],
  'dst': [vm11, vm12],
}

env.always_use_pty = False

allhosts = root + guest

scripts = [
  # (path, perm)
  ('/usr/local/bin/iperf', "+x"),
  ('/usr/bin/killall', "+x"),
  ('/sbin/ethtool', "+x"),
]

# Tenants
tenants = {
  l1: [vm11, vm12],
  l2: [vm21, vm22],
  l3: [vm31, vm32, vm33]
}

tenant_weights = {
  l1: {vm11: 1, vm12: 4},
  l2: {vm21: 1, vm22: 4},
  l3: {vm31: 1, vm32: 1, vm33: 1}
}

# Traffic matrix
dst_ip = {
  vm31: vm11, # tenant A
  vm32: vm11, # tenant A
  vm33: vm11, # tenant A

  vm21: vm12  # tenant B
}

udp_opt = '-u -b 9G -l 60k'
tcp_opt = '-P 4 -l 32k'
iperf_opts = {
  vm31: udp_opt,
  vm32: udp_opt,
  vm33: udp_opt,
}

# Backgrounding
def _runbg(command, out_file="/dev/null", err_file=None, shell=True, pty=False):
    run('nohup %s >%s 2>%s </dev/null &' % (command, out_file, err_file or '&1'), shell, pty)


@task
@hosts(allhosts)
def ifconfig():
    run("ifconfig %s" % iface[env.host])

@task
@roles('guest')
@parallel
def txq():
    eth = iface[env.host]
    run("ifconfig %s txqueuelen 32" % eth)
    run("ifconfig %s | egrep -o 'txqueuelen:[0-9]+'" % eth)

@task
@roles('guest')
@parallel(pool_size=5)
def copy_scripts():
    for f,perm in scripts:
        put(f, f)
        run("chmod %s %s" % (perm, f))

@task
@roles('guest')
def test():
    run("iperf -h")

@task
@roles('dst')
@parallel
def start_servers():
    _runbg("iperf -s")

@task
@roles('src')
@parallel
def start_clients():
    dst = dst_ip[env.host]
    o = iperf_opts.get(env.host, '')
    run("iperf -c %s -t 20 -i 1 %s" % (dst, o))

@task
@roles('guest')
@parallel
def stop():
    env.warn_only = True
    run("kill -9 `pgrep iperf`")

@task
@roles('root')
@parallel
def setup():
    eth = iface[env.host]
    run("insmod ~/vimal/exports/perfiso.ko iso_param_dev=p%s" % eth)
    for ip in tenants[env.host]:
        wt = tenant_weights[env.host][ip]
        create_ip_tenant(ip, wt)

@task
@roles('root')
@parallel
def remove():
    run("rmmod perfiso")

def create_ip_tenant(ip, w=1):
    d = '/sys/module/perfiso/parameters'
    run("echo %s > %s/create_txc" % (ip, d))
    run("echo %s > %s/create_vq" % (ip, d))
    run("echo associate txc %s vq %s > %s/assoc_txc_vq" % (ip, ip, d))
    run("echo %s weight %s > %s/set_vq_weight" % (ip, w, d))

