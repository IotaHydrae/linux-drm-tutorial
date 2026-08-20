obj-m += drm-tutorial.o

drm-tutorial-objs := drm.o

KDIR := /lib/modules/$(shell uname -r)/build

PWD := $(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

test: all
	sudo rmmod drm-tutorial.ko || true
	sudo insmod drm-tutorial.ko || true
	modetest -e
