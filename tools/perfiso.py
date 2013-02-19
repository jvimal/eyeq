
import glob
import os
import re
import sys
import ConfigParser
import subprocess
from subprocess import Popen
from collections import defaultdict, namedtuple
import logging

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
    try:
        subprocess.check_output(s, shell=True)
    except:
        die("Command '%s' failed." % s)
    return 0

def install(dev):
    global ISO_INSMOD
    ISO_INSMOD = os.path.exists(ISO_PROCFILE)
    if not ISO_INSMOD:
        ISO_INSMOD = True
        if not os.path.exists(ISO_PATH):
            die("Cannot find %s" % ISO_PATH)
        cmd("rmmod perfiso; insmod %s; " % ISO_PATH)
    if ISO_CREATED[dev]:
        return
    cmd("tc qdisc add dev %s root handle 1: htb" % dev)
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
        self.txcs_dev = defaultdict(list)
        self.weights = defaultdict(lambda: defaultdict(str))
        self.assoc = defaultdict(lambda: defaultdict(str))
        self.get()

    def create(self, dev, klass):
        if not ISO_CREATED[dev]:
            install(dev)
        if klass in self.txcs_dev[dev]:
            logging.error("txc %s already created." % klass)
            return -1
        c = "echo dev %s %s > %s/create_txc" % (dev, klass, ISO_SYSFS)
        return cmd(c)

    def delete(self, dev, klass):
        if not ISO_CREATED[dev]:
            return -1
        if klass not in self.txcs_dev[dev]:
            logging.error("txc %s not found." % klass)
            return -1
        c = "echo dev %s txc %s > %s/delete_txc" % (dev, klass, ISO_SYSFS)
        return cmd(c)

    def set_weight(self, dev, klass, w):
        c = "echo dev %s %s weight %s > %s/set_txc_weight" % (dev, klass, w, ISO_SYSFS)
        return cmd(c)

    def associate(self, dev, klass, vq):
        c = "echo dev %s associate txc %s vq %s > %s/assoc_txc_vq" % (dev, klass, vq, ISO_SYSFS)
        return cmd(c)

    def get(self):
        if not os.path.exists(ISO_PROCFILE):
            return
        lines = open(ISO_PROCFILE, 'r').readlines()
        ret = []
        dev = ""
        for line in lines:
            if line.startswith("tx->dev"):
                dev = line.strip().split(' ')[1]
                dev = dev.replace(',','')
                ISO_CREATED[dev] = True
            if line.startswith("txc class"):
                lst = re_spaces.split(line)
                tx_class = lst[2]
                vq_class = lst[7]
                weight = lst[4]
                ret.append((dev, tx_class, vq_class, weight))
                self.txcs_dev[dev].append(tx_class)
                self.weights[dev][tx_class] = weight
                self.assoc[dev][tx_class] = vq_class
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
            logging.info("Created TXC %s associated VQ %s on %s" % (txc, vqc, dev))

    def list(self):
        INDENT = '\t'
        print "Listing TXCs"
        for dev, lst in self.txcs_dev.iteritems():
            print "dev:", dev
            for txc in lst:
                print INDENT, txc, "weight", self.weights[dev][txc], "assoc vq", self.assoc[dev][txc]
        return

class VQ:
    def __init__(self):
        self.vqs_dev = defaultdict(list)
        self.weights = defaultdict(lambda: defaultdict(str))
        self.get()

    def create(self, dev, klass):
        if not ISO_CREATED[dev]:
            install(dev)
        if klass in self.vqs_dev[dev]:
            logging.error("vq %s already created." % klass)
            return -1
        c = "echo dev %s %s > %s/create_vq" % (dev, klass, ISO_SYSFS)
        return cmd(c)

    def delete(self, dev, klass):
        if not ISO_CREATED[dev]:
            return -1
        if klass not in self.vqs_dev[dev]:
            logging.error("vq %s not found." % klass)
            return -1
        c = "echo dev %s vq %s > %s/delete_vq" % (dev, klass, ISO_SYSFS)
        return cmd(c)

    def set_weight(self, dev, klass, w):
        c = "echo dev %s %s weight %s > %s/set_vq_weight" % (dev, klass, w, ISO_SYSFS)
        return cmd(c)

    def get(self):
        if not os.path.exists(ISO_PROCFILE):
            return
        lines = open(ISO_PROCFILE, 'r').readlines()
        ret = []
        dev = ""
        for line in lines:
            if line.startswith("tx->dev"):
                dev = line.strip().split(' ')[1]
                dev = dev.replace(',','')
                ISO_CREATED[dev] = True
            if line.startswith("vq class"):
                lst = re_spaces.split(line)
                vq_class = lst[2]
                weight = lst[10]
                ret.append((dev, vq_class, weight))
                self.vqs_dev[dev].append(vq_class)
                self.weights[dev][vq_class] = weight
        self.vqs = ret
        return ret

    def save(self, config):
        self.get()
        if not config.has_section("vqs"):
            config.add_section("vqs")
        for dev,vqc,wt in self.vqs:
            value = "weight %s, dev %s" % (wt, dev)
            # todo: we assume vqc is unique across devs.
            config.set("vqs", vqc, value)

    def load(self, config):
        if not config.has_section("vqs"):
            return
        for dev,vq,w in config.items("vqs"):
            self.create(dev, vq)
            w = w.split(" ")[1]
            self.set_weight(dev, vq, w)
            logging.info("Created VQ %s weight %s on dev %s" % (vq, w, dev))

    def list(self):
        INDENT = '\t'
        print "Listing VQs"
        for dev, lst in self.vqs_dev.iteritems():
            print "dev:", dev
            for vq in lst:
                print INDENT, vq, "weight", self.weights[dev][vq]
        return

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
        logging.error("set_rps expects 3 arguments: --set-rps dev:qnum:cpumask")
        return
    if 'rx' not in q:
        q = "rx-%s" % q
    file = "/sys/class/net/%s/queues/%s/rps_cpus" % (dev, q)
    try:
        open(file, 'w').write(value)
    except:
        logging.error("Cannot find device %s/queue %s (path %s)" % (dev, q, file))
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

def stats(dev):
    """Parse the txc, rl and vq stats.  For each txc, we return tx
    rate and the rate assigned to it.  For each rl, we return the dest
    IP and current rate."""
    # This is probably easier if the kernel module returns it in
    # structured format.  But let's live with this for now.
    import struct
    import socket
    def IP(hex):
        return socket.inet_ntoa(struct.pack('!L', int(hex, 16)))

    class Data:
        def __init__(self, **kwargs):
            self.__dict__.update(kwargs)
        def str(self, head, keys):
            s = []
            for k in keys:
                v = self.__dict__[k]
                s.append("%s: %s" % (k,v))
            return head + ": " + ", ".join(s)
    class Dev(Data):
        def __str__(self):
            keys = ['dev']
            return Data.str(self, 'Dev', keys)
    class Txc(Data):
        def __str__(self):
            keys = ['klass', 'min_rate', 'rate', 'tx_rate']
            return Data.str(self, 'TXC', keys)
    class Vq(Data):
        def __str__(self):
            keys = ['klass', 'rate', 'rx_rate', 'fb_rate']
            return Data.str(self, 'VQ', keys)
    class RL(Data):
        def __str__(self):
            keys = ['dst', 'rate']
            return Data.str(self, 'RL', keys)
    """
    Dev = namedtuple('Dev', ['name', 'txcs', 'vqs'])
    Txc = namedtuple('Txc', ['klass', 'min_rate', 'rate', 'tx_rate', 'rls'])
    Vq = namedtuple('Vq', ['klass', 'min_rate', 'rate', 'rx_rate', 'fb_rate'])
    RL = namedtuple('RL', ['dst', 'rate'])
    """

    dev = None
    txc = None
    vq = None
    rl = None
    rl_parse = False
    devs = []
    for line in open(ISO_PROCFILE).readlines():
        if line.startswith('tx->dev'):
            if dev is not None:
                devs.append(dev)
            devname = line.split(',')[0].split(' ')[1]
            dev = Dev(dev=devname, txcs=[], vqs=[])
            rl_parse = False
        if line.startswith('txc class'):
            klass = re_spaces.split(line)[2]
            klass = klass
            txc = Txc(klass=klass, rls=[], rate='', tx_rate='', min_rate='')
            rl_parse = False
        if line.startswith('txc rl'):
            data = re_spaces.split(line)
            txc.min_rate = data[7]
            txc.rate = data[5]
            txc.tx_rate = data[3].split(',')[1]
            dev.txcs.append(txc)
        if line.startswith('rate limiters:'):
            rl_parse = True
        if rl_parse and line.startswith('hash'):
            data = re_spaces.split(line)
            dst = IP(data[3])
            rate = data[5]
            rl = RL(dst=dst, rate=rate)
            txc.rls.append(rl)
        if line.startswith('vq class'):
            data = re_spaces.split(line)
            klass = data[2]
            rate = data[6]
            rx_rate = data[8]
            fb_rate = data[10]
            dev.vqs.append(Vq(klass=klass, rate=rate, rx_rate=rx_rate, fb_rate=fb_rate))
    if dev is not None:
        devs.append(dev)
    return devs
