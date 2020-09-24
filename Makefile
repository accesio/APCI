obj-m += apci.o
CC		:= "gcc -O3"
KVERSION        := $(shell uname -r)
KDIR		:= /lib/modules/$(KVERSION)/build

apci-objs :=          \
        apci_fops.o   \
	apci_dev.o

all:
	$(MAKE) CC=$(CC) -C $(KDIR) M=$(PWD) modules 

clean: 
	$(MAKE) CC=$(CC) -C $(KDIR) M=$(PWD) clean

install:
	$(MAKE) CC=$(CC) -C $(KDIR) M=$$PWD modules_install
	depmod
