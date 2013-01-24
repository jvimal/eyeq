#!/bin/bash

pat="${1:- }"

if [ -f /boot/grub/menu.lst ]; then
	grep '^title' /boot/grub/menu.lst |  nl -v 0 | grep "$pat"
fi

if [ -f /boot/grub/grub.cfg ]; then
	grep '^menuentry' /boot/grub/grub.cfg | nl -v 0 | grep "$pat"
fi

