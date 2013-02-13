
import glob
import os
import re
import sys
import ConfigParser
from subprocess import Popen
from collections import defaultdict

re_digits = re.compile(r'\d+')
re_spaces = re.compile(r'\s+')

ISO_SYSCTL_DIR = '/proc/sys/perfiso/'
ISO_PROCFILE   = '/proc/perfiso_stats'
ISO_SYSFS      = '/sys/module/perfiso/parameters'
ISO_CREATED    = defaultdict(bool)
ISO_INSMOD     = False
ISO_PATH       = '/root/iso/perfiso.ko'

def die(s):
    print s
    sys.exit(-1)

def read_file(f):
    return open(f).read().strip()

def cmd(s):
    return Popen(s, shell=True).wait()

def install(dev):
    if not ISO_INSMOD:
        ISO_INSMOD = True
        if not os.path.exists(ISO_PATH):
            die("Cannot find %s" % ISO_PATH)
        cmd("rmmod sch_htb; insmod %s; " % ISO_PATH)
    if not ISO_CREATED[dev]:
        return
    cmd("tc qdisc add dev %s htb default 1" % dev)
    # todo check ret value
    ISO_CREATED[dev] = True

class Parameters:
    def __init__(self):
        self.params = {}
        self.read()

    def read(self):
        for p in glob.glob(ISO_SYSCTL_DIR + "*"):
            name = os.path.basename(p)
            value = read_file(p)
            self.params[name] = value
        self.params_list = list(self.params.iteritems())

    def set(self, name='', value=''):
        if re_digits.match(name):
            name = self.params_list[int(name)-1][0]
        path = os.path.join(ISO_SYSCTL_DIR, name)
        open(path, 'w').write(str(value))
        print "Setting %s = %s" % (name, value)
        self.read()

    def __str__(self, pad=True):
        ret = []
        i = 0
        for k,v in self.params.iteritems():
            i += 1
            if pad:
                ret.append("%2s %40s %10s" % (i, k, v))
            else:
                ret.append("%s %s %s" % (i, k, v))
        ret = "\n".join(ret)
        return ret

    def __repr__(self):
        return self.__str__(pad=False)

    def save(self, config):
        if not config.has_section("params"):
            config.add_section("params")
        for name,value in self.params.iteritems():
            config.set("params", name, value)

    def load(self, config):
        if not config.has_section("params"):
            return
        for name,value in config.items("params"):
            self.set(name, value)

class TxClass:
    def __init__(self):
        self.txcs = []

    def create(self, dev, klass):
        if not ISO_CREATED[dev]:
            install(dev)
        return Popen("echo dev %s %s > %s/create_txc" % (dev, klass, ISO_SYSFS), shell=True).wait()

    def associate(self, dev, klass, vq):
        return Popen("echo dev %s associate txc %s vq %s > %s/assoc_txc_vq" % (dev, klass, vq, ISO_SYSFS),
                     shell=True).wait()

    def get(self):
        lines = open(ISO_PROCFILE, 'r').readlines()
        ret = []
        dev = ""
        for line in lines:
            if line.startswith("tx->dev"):
                dev = line.strip().split(' ')[1]
            if line.startswith("txc class"):
                lst = re_spaces.split(line)
                tx_class = lst[2]
                vq_class = lst[5]
                ret.append((dev, tx_class, vq_class))
        self.txcs = ret
        return ret

    def save(self, config):
        self.get()
        if not config.has_section("txc"):
            config.add_section("txc")
        for dev,txc,vqc in self.txcs:
            value = "vq %s, dev %s" % (vqc, dev)
            config.set("txc", txc, value)

    def load(self, config):
        if not config.has_section("txc"):
            return
        for dev, txc,vqc in config.items("txc"):
            self.create(dev, txc)
            vqc = vqc.split(" ")[1]
            self.associate(dev, txc, vqc)
            print "Created TXC %s associated VQ %s on %s" % (txc, vqc, dev)

class VQ:
    def create(self, dev, klass):
        if not ISO_CREATED[dev]:
            install(dev)
        return Popen("echo dev %s %s > %s/create_vq" % (dev, klass, ISO_SYSFS), shell=True).wait()

    def set_weight(self, dev, klass, w):
        return Popen("echo dev %s %s weight %s > %s/set_vq_weight" % (dev, klass, w, ISO_SYSFS),
                     shell=True).wait()

    def get(self):
        lines = open(ISO_PROCFILE, 'r').readlines()
        ret = []
        dev = ""
        for line in lines:
            if line.startswith("tx->dev"):
                dev = line.strip().split(' ')[1]
            if line.startswith("vq class"):
                lst = re_spaces.split(line)
                vq_class = lst[2]
                weight = lst[10]
                ret.append((dev, vq_class, weight))
        self.vqs = ret
        return ret

    def save(self, config):
        self.get()
        if not config.has_section("vqs"):
            config.add_section("vqs")
        for dev,vqc,wt in self.vqs:
            value = "weight %s, dev %s" % (wt, dev)
            config.set("vqs", vqc, value)

    def load(self, config):
        if not config.has_section("vqs"):
            return
        for dev,vq,w in config.items("vqs"):
            self.create(dev, vq)
            w = w.split(" ")[1]
            self.set_weight(dev, vq, w)
            print "Created VQ %s weight %s on dev %s" % (vq, w, dev)

params = Parameters()
txc = TxClass()
vqs = VQ()

config = ConfigParser.ConfigParser()
config.optionxform = str

def get(args):
    print params.__str__()

def set(args):
    try:
        if args.value is None and ',' in args.set:
            args.set, args.value = args.set.split(',')
        params.set(args.set, args.value)
    except Exception, e:
        die("error setting %s to %s.  (are you root?)" % (args.set, args.value))

def get_rps(args):
    dev = args.get_rps
    if dev is None:
        dev = args.dev
    ret = []
    for queue in glob.glob("/sys/class/net/%s/queues/rx*" % dev):
        config = open(os.path.join(queue, "rps_cpus"), "r").read().strip()
        ret.append((os.path.basename(queue), config))
    print '\n'.join(map(lambda e: "%s: %s" % e, ret))
    return ret

def set_rps(args):
    try:
        dev, q, value = args.set_rps.split(':')
    except:
        print "set_rps expects 3 arguments: --set-rps dev:qnum:cpumask"
        return
    if 'rx' not in q:
        q = "rx-%s" % q
    file = "/sys/class/net/%s/queues/%s/rps_cpus" % (dev, q)
    try:
        open(file, 'w').write(value)
    except:
        print "Cannot find device %s/queue %s (path %s)" % (dev, q, file)
    print file, open(file, 'r').read().strip()

def save_rps(args, config):
    if not config.has_section("rps"):
        config.add_section("rps")
    for rxq, value in get_rps(args):
        config.set("rps", rxq, value)

def load_rps(args, config):
    if not config.has_section("rps"):
        return
    dev = args.dev
    for rxq, value in config.items("rps"):
        args.set_rps = ':'.join([dev, rxq, value])
        set_rps(args)

def save(args):
    where = open(args.save, 'w')
    params.save(config)
    txc.save(config)
    vqs.save(config)
    save_rps(args, config)
    config.write(where)

def load_config(args):
    config = ConfigParser.ConfigParser()
    config.optionxform = str
    config.read(args.load)
    params.load(config)
    load_rps(args, config)
    vqs.load(config)
    txc.load(config)

def clear():
    Popen("rmmod perfiso", shell=True).wait()

def load_module(args):
    Popen("insmod %s" % args.module, shell=True).wait()

