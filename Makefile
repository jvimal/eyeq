

obj-m += perfiso.o

perfiso-y := stats.o rc.o rl.o vq.o tx.o rx.o params.o bridge.o main.o
EXTRA_CFLAGS += -DISO_TX_CLASS_MARK -O2

all:
	make -C /usr/src/linux-cfs-bw M=`pwd`
