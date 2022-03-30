obj-m += apci.o
CC		:= aarch64-none-linux-gnu-gcc
KVERSION        := $(shell uname -r)
KDIR		:= /usr/src/linux

apci-objs :=      \
    apci_fops.o   \
	apci_dev.o

all:
	$(MAKE) CC=$(CC) -C $(KDIR) M=$(CURDIR) ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- modules

clean:
	$(MAKE) CC=$(CC) -C $(KDIR) M=$(CURDIR) ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- clean

install:
	$(MAKE) CC=$(CC) -C $(KDIR) M=$(CURDIR) ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- modules_install
	depmod -A
	modprobe -r apci
	modprobe apci
