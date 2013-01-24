#!/bin/bash

num=$1
if [ -z "$num" ]; then
  echo "usage: $0 entry-number"
  exit 0
fi

if [ -f /boot/grub/menu.lst ]; then
	echo Current default entry: `grep '^default' /boot/grub/menu.lst`
	sed "s/^default.*$/default $num/" -i /boot/grub/menu.lst
	echo Changed default entry: `grep '^default' /boot/grub/menu.lst`
fi

if [ -f /boot/grub/grub.cfg ]; then
	grub-set-default $num
fi

