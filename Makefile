

obj-m += perfiso.o

perfiso-y := stats.o rl.o vq.o tx.o rx.o params.o bridge.o main.o

all:
	make -C /usr/src/linux-cfs-bw M=`pwd`
