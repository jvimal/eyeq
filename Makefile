

obj-m += perfiso.o

perfiso-y := stats.o rc.o rl.o vq.o tx.o rx.o params.o direct.o main.o
EXTRA_CFLAGS += -DISO_TX_CLASS_IPADDR -DDIRECT -O2

all:
	make -C /usr/src/linux-cfs-bw M=`pwd`
	cp perfiso.ko ~/vimal/10g/modules
	cp perfiso.ko ~/vimal/exports
