# How to Compile and Install the ACCES PCI Linux Driver


Steps to install the driver

	1. Download the current Linux kernel headers.
	2. Download, compile, and load the ACCES PCI Linux driver.
	3. Inform the Linux kernel of our new driver so it can load it at boot time.


1.	The first thing to do is to download and install the current Linux kernel headers.
~~~
	sudo apt-get install linux-headers-$(uname -r)
~~~

The kernel headers should now be installed at /usr/src/linux-headers-$(uname -r)

Note that "uname -r" produces the current Linux kernel version name (i.e. 5.0.0-23-generic).
Also notice that in Ubuntu Linux there is no administrator account and all activities requiring
root user privileges must done using "sudo".


2.	Next, the driver sources need to be downloaded from the github repository.
~~~
	cd ~
	git clone https://github.com/accesio/APCI.git
~~~
Then we need to compile them.
~~~
	cd APCI/
	make
~~~
The APCI directory should now include the compiled driver module "apci.ko" as shown below.
~~~
jimmy@jimmy-desktop:~/Programming/APCI$ ls
apci_common.h  apci_fops.c   apci.ko     apci.o         Module.symvers
apci_dev.c     apci_fops.h   apcilib     build.sh       README.md
apci_dev.h     apci_fops.o   apci.mod.c  Makefile       README.txt
apci_dev.o     apci_ioctl.h  apci.mod.o  modules.order
~~~


Now we can load the driver module and use it.
~~~
sudo insmod apci.ko
~~~
There should now be a device node in directory /dev/apci/ for each ACCES PCI device in the system. For example /dev/apci/mpcie-dio-24s_0 can be used to access the M.2-DIO-24S functions and registers programmatically as shown in the sample source files in the apcilib/ subdirectory.

Use the commands below to compile the provided samples from the APCI/apcilib/ directory.
~~~
cd apcilib/
make
~~~
Below is a snipet of the output from the compiled mpcie-dio-24s-irq.c sample code.

	jimmy@jimmy-desktop:~/Programming/APCI/apcilib$ sudo ./mpcie-dio-24s-irq
	Initial read: status = 0, inputs A = 0xff
	Initial read: status = 0, inputs B = 0xff
	Initial read: status = 0, inputs C = 0xff
	 Waiting for irq @ Tue Dec 17 08:38:17 2019
	IRQ occurred
	final read: status = 0, inputs A = 0xfe
	final read: status = 0, inputs B = 0xff
	final read: status = 0, inputs C = 0xff
	 Waiting for irq @ Tue Dec 17 08:38:22 2019
	IRQ occurred
	final read: status = 0, inputs A = 0xfe
	final read: status = 0, inputs B = 0xfe
	final read: status = 0, inputs C = 0xff
	 Waiting for irq @ Tue Dec 17 08:38:22 2019
	IRQ occurred
	final read: status = 0, inputs A = 0xfe
	final read: status = 0, inputs B = 0xfe
	final read: status = 0, inputs C = 0xff
	 Waiting for irq @ Tue Dec 17 08:38:22 2019
	IRQ occurred
	final read: status = 0, inputs A = 0xff
	final read: status = 0, inputs B = 0xff
	final read: status = 0, inputs C = 0xff
	 Waiting for irq @ Tue Dec 17 08:38:22 2019
	IRQ occurred

3.	The install the driver module is to run the install target of the makefile as root. Note that only the install should be done as root. The build should be done as a regular user.
~~~
	sudo make install
~~~

The kernel should now load the apci.ko driver module at boot time if there is a supported ACCES PCI device present in the system.

