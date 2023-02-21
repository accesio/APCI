#ifndef APCI_FOPS_H
#define APCI_FOPS_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <asm/io.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include "apci_common.h"
#include "apci_ioctl.h"

extern struct apci_my_info head;

ssize_t read_child_apci(struct file *f, char __user *buf, size_t len,
			loff_t *off);
int open_child_apci(struct inode *inode, struct file *filp);

ssize_t read_apci(struct file *f, char __user *buf, size_t len, loff_t *off);
int open_apci(struct inode *inode, struct file *filp);
int release_apci(struct inode *inode, struct file *filp);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39)
int ioctl_apci(struct inode *inode, struct file *filp, unsigned int cmd,
	       unsigned long arg);
#else
long ioctl_apci(struct file *filp, unsigned int cmd, unsigned long arg);
#endif
#endif

int mmap_apci(struct file *filp, struct vm_area_struct *);
