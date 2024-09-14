obj-m += apci.o
CC		?= "gcc"
KVERSION        ?= $(shell uname -r)
KDIR		?= /lib/modules/$(KVERSION)/build

apci-objs :=      \
    apci_fops.o   \
	apci_dev.o

all:
	$(MAKE) CC=$(CC) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) CC=$(CC) -C $(KDIR) M=$(CURDIR) clean

install:
	$(MAKE) CC=$(CC) -C $(KDIR) M=$(CURDIR) modules_install
	depmod -A
	modprobe -r apci
	modprobe apci
