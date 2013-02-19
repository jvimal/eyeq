#!/usr/bin/python

import argparse
import perfiso
import subprocess

parser = argparse.ArgumentParser(description="Perfiso Userspace Control")
parser.add_argument("--get",
                    dest="get",
                    action="store_true",
                    help="Get all parameters",
                    default=None)

parser.add_argument("--set",
                    dest="set",
                    action="store",
                    help="Set parameter number/name",
                    default=None)

parser.add_argument("--value", "-v",
                    dest="value",
                    action="store",
                    help="Value to set the parameter",
                    default=None)

parser.add_argument("--save", "-s",
                    dest="save",
                    action="store",
                    help="Save current configuration (txc, vqs, parameters)",
                    default=None)

parser.add_argument("--load",
                    dest="load",
                    action="store",
                    help="Load configuration from file.  Module will be reset.",
                    default=None)

parser.add_argument("--module", "-m",
                    dest="module",
                    action="store",
                    help="Path to perfiso module.",
                    default="./perfiso.ko iso_param_dev=eth2")

parser.add_argument('--get-rps',
                    dest="get_rps",
                    help="Get the RPS configuration for the device.",
                    default=None)

parser.add_argument('--dev',
                    dest="dev",
                    help="Device to load/save RPS configuration.",
                    default=None)

parser.add_argument('--set-rps',
                    dest="set_rps",
                    help="Sets the RPS configuration for the device.",
                    default=None)

parser.add_argument("--create-tenant", '-c',
                    help="Create tenant.  Pass tenant class id (IP address/skb->mark).",
                    default=None)

parser.add_argument("--delete-tenant", '-d',
                    help="Delete tenant.  Pass tenant class id (IP address/skb->mark).",
                    default=None)

parser.add_argument("--list", '-l',
                    help="List all tenants.",
                    action="store_true")

parser.add_argument("--stats",
                    help="List stats for tenants, rate limiters and VQs.",
                    action="store_true")

parser.add_argument('--weight', '-w',
                    help="Relative weight of tenant.",
                    default=1)

args = parser.parse_args()

def is_netdev(dev):
    out = subprocess.check_output("ip link show", shell=True)
    for line in out.split('\n'):
        if len(line) == 0:
            continue
        first = line[0]
        if not first.isdigit():
            continue
        num, d, _ = line.split(':', 2)
        if dev == d.strip():
            return True
    return False

if args.get:
    perfiso.get(args)
elif args.set:
    perfiso.set(args)
elif args.save:
    perfiso.save(args)
elif args.load:
    perfiso.clear()
    perfiso.load_module(args)
    perfiso.load_config(args)
elif args.get_rps:
    perfiso.get_rps(args)
elif args.set_rps:
    perfiso.set_rps(args)
elif args.create_tenant:
    tid = args.create_tenant
    dev = args.dev
    if not is_netdev(dev):
        perfiso.die("Device %s not found" % dev)
    perfiso.txc.create(dev, tid)
    perfiso.vqs.create(dev, tid)
    perfiso.txc.associate(dev, tid, tid)
    perfiso.txc.set_weight(dev, tid, args.weight)
    perfiso.vqs.set_weight(dev, tid, args.weight)
elif args.delete_tenant:
    tid = args.delete_tenant
    dev = args.dev
    if not is_netdev(dev):
        perfiso.die("Device %s not found" % dev)
    perfiso.txc.delete(dev, tid)
    perfiso.vqs.delete(dev, tid)
elif args.list:
    perfiso.txc.list()
    perfiso.vqs.list()
elif args.stats:
    stats = perfiso.stats(args.dev)
    INDENT = '\t'
    for dev in stats:
        print dev
        for txc in dev.txcs:
            print INDENT, txc
            for rl in txc.rls:
                print INDENT*2, rl
        for vq in dev.vqs:
            print INDENT, vq
else:
    parser.print_help()
