

obj-m += perfiso.o

perfiso-y := rl.o vq.o tx.o rx.o params.o

all:
	make -C /usr/src/linux-cfs-bw M=`pwd`
