Build instructions

Here are the easiest ways to build this module


Debian / Ubuntu systems
=======================


1. Install your default headers 

sudo apt-get install linux-headers-$(uname -r)

2. Verify that they were installed. They should exist under
/usr/src/linux-headers-$(uname -r )

3. make KDIR=/usr/src/linux-headers-$(uname -r)


Fedora 
=============

Coming shortly




In cases where you already have the Linux kernel downloaded, you
should look at the following instructions.



Debian system (EASY)


1. Assumes that you have a kernel that is gunzipped / untarred  into /usr/src

2. Assumes that uname -r matches the kernel you are currently using and have installed

3. If these are met, then you should just be able to run "make" in the
   APCI directory




Your Kernel source exists elsewhere (LITTLE TRICKY)


1. Specify the path to your kernel code, as follows


make KDIR=/usr/src/linux-3.4.37 


CAVEAT

This won't apply unless you have a Module.symvers inside of that
directory. When that files doesn't exist, you will have to first
run "make modules" to generate the required Module.symvers. 



You downloaded a fresh Kernel ( HARDER )

You have just untarred a kernel from www.kernel.org and the directory
exists in /usr/src ( or elsewhere , but assume /usr/src for
explanation )

Assuming in the following case that 

KERNEL_NUMBER=3.2.44



1. cd /usr/src/linux-$KERNEL_NUMBER

2. make menuconfig #  ( hit return and exit immediately to generate
basic ) 

3. make modules # ( this takes a while , but is required )

4. cd APCI directory 

5. make KDIR=/usr/src/linux-$KERNEL_NUMBER

