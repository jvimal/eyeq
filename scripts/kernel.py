from fabric.api import *

def l(i):
    return "l%d" % i

exclude = [l(9), l(11), l(12), l(18)]

# Machines with old and new grub
old = [l(i) for i in xrange(1, 11)]
new = [l(i) for i in xrange(11, 21)]
machines = old + new

for m in exclude:
    if m in old:
        old.remove(m)
    if m in new:
        new.remove(m)
    machines.remove(m)

print machines

# Faster...
env.parallel = True

# Kernel names with old and new grub slightly differ, keep note!
kernel_new = 'Debian GNU/Linux, with Linux 3.0.0with-cfs-setns-dctcp'
kernel_old = 'Debian GNU/Linux, kernel 3.0.0with-cfs-setns-dctcp'

@task
@hosts(machines)
def grub_version():
    run("grub-install -v")

@task
@hosts(machines)
def kernel():
    run("uname -r")

@task
@hosts(machines)
def reboot():
    run("reboot")

@task
def grub_set_default():
    execute(grub_set_default_new)
    execute(grub_set_default_old)

@task
@hosts(new)
def grub_set_default_new():
    run("sed 's/^GRUB_DEFAULT=.*/GRUB_DEFAULT=saved/' -i /etc/default/grub")
    run("grub-mkconfig -o /boot/grub/grub.cfg")
    run("grub-set-default '%s'" % kernel_new)
    run("grub-editenv list")

@task
@hosts(old)
def grub_set_default_old():
    #run("update-grub")
    num = run("grep '^title' /boot/grub/menu.lst | nl -v 0 | egrep 'title[[:blank:]]+%s$' | awk '{ print $1 }'" % kernel_old)
    run("sed 's/^default .*$/default %s/' -i /boot/grub/menu.lst" % num)

@task
@hosts(old)
def grub_backup():
    run("cp /boot/grub/menu.lst /boot/grub/menu.lst-`date +%h-%d-%H-%M`")

@task
@hosts(new)
def grub_list():
    run("grep GRUB_DEFAULT /etc/default/grub")
    run("grep '%s' /boot/grub/grub.cfg" % kernel)
