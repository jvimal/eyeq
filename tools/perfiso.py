
import glob
import os
import re
import sys
import subprocess
from subprocess import Popen
from collections import defaultdict, namedtuple
import logging
import json

re_digits = re.compile(r'\d+')
re_spaces = re.compile(r'\s+')

ISO_SYSCTL_DIR = '/proc/sys/perfiso/'
ISO_PROCFILE   = '/proc/perfiso_stats'
ISO_SYSFS      = '/sys/module/perfiso/parameters'
ISO_CREATED    = defaultdict(bool)
ISO_INSMOD     = False
ISO_PATH       = '/root/iso/perfiso.ko'

logging.basicConfig(level=logging.DEBUG)

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

    def load(self, cfg):
        for name,value in cfg.iteritems():
            self.set(name, value)

class TxClass:
    def __init__(self):
        self.txcs = []
        self.txcs_dev = defaultdict(list)
        self.get()

    def create(self, dev, klass):
        if not ISO_CREATED[dev]:
            install(dev)
        if klass in self.txcs_dev[dev]:
            logging.error("txc %s already created." % klass)
            return -1
        c = "echo dev %s %s > %s/create_txc" % (dev, klass, ISO_SYSFS)
        logging.info("Creating txc %s on dev %s" % (klass, dev))
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
        logging.info("Setting weight of vq %s on dev %s to %s" % (klass, dev, w))
        return cmd(c)

    def associate(self, dev, klass, vq):
        c = "echo dev %s associate txc %s vq %s > %s/assoc_txc_vq" % (dev, klass, vq, ISO_SYSFS)
        return cmd(c)

    def get(self):
        if not os.path.exists(ISO_PROCFILE):
            return
        st = stats()
        for dev in st:
            for txc in dev.txcs:
                self.txcs_dev[dev.dev].append(txc.klass)
        return

    def load(self, config):
        for dev in config.get('config', {}).keys():
            install(dev)
            for val in config['config'][dev]['txcs']:
                klass = val['klass']
                weight = val['weight']
                vq = val['assoc']
                self.create(dev, klass)
                self.set_weight(dev, klass, weight)
                self.associate(dev, klass, vq)
                logging.info("Created TXC %s weight %s on dev %s assoc %s" % (klass, weight, dev, vq))

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
        logging.info("Creating vq %s on dev %s" % (klass, dev))
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
        st = stats()
        for dev in st:
            for vq in dev.vqs:
                self.vqs_dev[dev.dev].append(vq.klass)
        return

    def load(self, config):
        for dev in config.get('config', {}).keys():
            install(dev)
            for val in config['config'][dev]['vqs']:
                klass = val['klass']
                weight = val['weight']
                self.create(dev, klass)
                self.set_weight(dev, klass, weight)
                logging.info("Created VQ %s weight %s on dev %s" % (klass, weight, dev))

def get(args):
    print params.__str__()

def recompute_dev(dev):
    c = "echo dev %s > %s/recompute_dev" % (dev, ISO_SYSFS)
    return cmd(c)

def set(args):
    try:
        if args.value is None and ',' in args.set:
            args.set, args.value = args.set.split(',')
        params.set(args.set, args.value)
        # Recompute the tx and rx rates
        for dev in stats():
            recompute_dev(dev.dev)
    except Exception, e:
        die("error setting %s to %s.  (are you root?)" % (args.set, args.value))

def save(args):
    where = open(args.save, 'w')
    config = dict(config=dict(), params=params.params)
    data = stats(None)
    for dev in data:
        curr = dict()
        config['config'][dev.dev] = curr
        curr['txcs'] = []
        curr['vqs'] = []
        for txc in dev.txcs:
            curr['txcs'].append(txc.get())
        for vq in dev.vqs:
            curr['vqs'].append(vq.get())
    json.dump(config, where, indent=4)
    where.close()

def load_config(args):
    global config
    config = json.load(open(args.load))
    params.load(config.get('params'))
    vqs.load(config)
    txc.load(config)

def clear():
    global params, txc, vqs, ISO_CREATED, ISO_INSMOD
    # See if we're already loaded
    st = stats()
    for dev in st:
        c = "tc qdisc del dev %s root" % dev.dev
        logging.debug("Removing on %s" % dev.dev)
        cmd(c)
    Popen("rmmod perfiso", shell=True).wait()
    ISO_CREATED = defaultdict(bool)
    ISO_INSMOD = False

    params = Parameters()
    txc = TxClass()
    vqs = VQ()

def load_module(args):
    Popen("insmod %s" % args.module, shell=True).wait()

def stats(filterdev=None):
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
        def getdict(self, keys):
            ret = {}
            for k in keys:
                ret[k] = self.__dict__[k]
            return ret
    class Dev(Data):
        def __str__(self):
            keys = ['dev']
            return Data.str(self, 'Dev', keys)
    class Txc(Data):
        def __str__(self):
            keys = ['klass', 'min_rate', 'rate', 'tx_rate', 'assoc']
            return Data.str(self, 'TXC', keys)
        def get(self):
            keys = ['klass', 'weight', 'assoc']
            return self.getdict(keys)
    class Vq(Data):
        def __str__(self):
            keys = ['klass', 'rate', 'rx_rate', 'fb_rate', 'alpha']
            return Data.str(self, 'VQ', keys)
        def get(self):
            keys = ['klass', 'weight']
            return self.getdict(keys)
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
    vq_dev = None
    dev_by_name = dict()
    devs = []
    global ISO_INSMOD, ISO_CREATED
    if not os.path.exists(ISO_PROCFILE):
        die("EyeQ module not loaded")

    ISO_INSMOD = True
    for line in open(ISO_PROCFILE).readlines():
        if line.startswith('tx->dev'):
            if dev is not None and (filterdev is None or filterdev == dev.dev):
                devs.append(dev)
                dev_by_name[dev.dev] = dev
            devname = line.split(',')[0].split(' ')[1]
            ISO_CREATED[devname] = True
            dev = Dev(dev=devname, txcs=[], vqs=[])
            rl_parse = False
        if line.startswith('txc class'):
            data = re_spaces.split(line)
            klass = data[2]
            weight = data[4]
            assoc = data[7]
            txc = Txc(klass=klass, rls=[], rate='', tx_rate='', min_rate='', weight=weight, assoc=assoc)
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
        if line.startswith('vqs'):
            vq_dev = line.split(' ')[1]
        if line.startswith('vq class'):
            data = re_spaces.split(line)
            klass = data[2]
            rate = data[6]
            rx_rate = data[8]
            fb_rate = data[10]
            alpha = data[12]
            weight = data[16]
            whichdev = dev_by_name.get(vq_dev, None)
            vq = Vq(klass=klass, rate=rate, rx_rate=rx_rate,
                    fb_rate=fb_rate, alpha=alpha, weight=weight)
            if whichdev is None:
                dev.vqs.append(vq)
            else:
                whichdev.vqs.append(vq)
    if dev is not None and (filterdev is None or filterdev == dev.dev):
        devs.append(dev)
    return devs

params = Parameters()
txc = TxClass()
vqs = VQ()

def set_onegbe_params():
    vals = {
        # 100us rate limiting granularity
        "ISO_TOKENBUCKET_TIMEOUT_NS": 100 * 1000,
        # The bandwidth headroom can be smaller at 1GbE
        "ISO_VQ_DRAIN_RATE_MBPS": 920,
        # 980Mb/s is the maximum wire rate due to inter-packet gap and
        # framing overheads.
        "ISO_MAX_TX_RATE": 980,
        "ISO_RFAIR_INITIAL": 500,
        "ISO_RL_UPDATE_INTERVAL_US": 50,
        }
    for parm, val in vals.iteritems():
        params.set(parm, val)
    return
