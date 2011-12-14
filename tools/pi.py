#!/usr/bin/python

import argparse
import perfiso

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

parser.add_argument("--load", "-l",
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

args = parser.parse_args()

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
else:
    parser.print_help()
