/*
This software is a driver for ACCES I/O Products, Inc. PCI cards.
Copyright (C) 2007  ACCES I/O Products, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation. This software is released under
version 2 of the GPL.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

In addition ACCES provides other licenses with its software at customer request.
For more information please contact the ACCES software department at 
(800)-326-1649 or visit www.accesio.com
*/

#include <sys/ioctl.h>
#include <linux/errno.h>

#include "apcilib.h"
#include "apci.h"

int apci_get_devices(int fd)
{
	return ioctl(fd, apci_get_devices_ioctl);
}

int apci_get_device_info(int fd, unsigned long device_index, unsigned int *dev_id, unsigned long base_addresses[6])
{
	info_struct dev_info;
	int count;
	int status;

	dev_info.device_index = device_index;

	status = ioctl(fd, apci_get_device_info_ioctl, &dev_info);
	
	if (dev_id != NULL) *dev_id = dev_info.dev_id;
	
	if (base_addresses != NULL)
	for (count = 0; count < 6; count ++) base_addresses[count] = dev_info.base_addresses[count];
	
	return status;

}

int apci_write8(int fd, unsigned long device_index, int bar, int offset, __u8 data)
{
	iopack io_pack;
	
	io_pack.device_index = device_index;
	io_pack.bar = bar;
	io_pack.offset = offset;
	io_pack.data = data;
	io_pack.size = BYTE;
	
	return ioctl(fd, apci_write_ioctl, &io_pack);

}

int apci_write16(int fd, unsigned long device_index, int bar, int offset, __u16 data)
{
	iopack io_pack;
	
	io_pack.device_index = device_index;
	io_pack.bar = bar;
	io_pack.offset = offset;
	io_pack.data = data;
	io_pack.size = WORD;
	
	return ioctl(fd, apci_write_ioctl, &io_pack);
}

int apci_write32(int fd, unsigned long device_index, int bar, int offset, __u32 data)
{
	iopack io_pack;
	
	io_pack.device_index = device_index;
	io_pack.bar = bar;
	io_pack.offset = offset;
	io_pack.data = data;
	io_pack.size = DWORD;
	
	return ioctl(fd, apci_write_ioctl, &io_pack);
}

int apci_read8(int fd, unsigned long device_index, int bar, int offset, __u8 *data)
{
	iopack io_pack;
	int status;

	io_pack.device_index = device_index;
	io_pack.bar = bar;
	io_pack.offset = offset;
	io_pack.size = BYTE;
	
	status = ioctl(fd, apci_read_ioctl, &io_pack);
	
	if (data != NULL) *data = io_pack.data;
	
	return status;
	
	
}

int apci_read16(int fd, unsigned long device_index, int bar, int offset, __u16 *data)
{
	iopack io_pack;
	int status;

	io_pack.device_index = device_index;
	io_pack.bar = bar;
	io_pack.offset = offset;
	io_pack.size = WORD;
	
	status = ioctl(fd, apci_read_ioctl, &io_pack);
	
	if (data != NULL) *data = io_pack.data;
	
	return status;
}

int apci_read32(int fd, unsigned long device_index, int bar, int offset, __u32 *data)
{
	iopack io_pack;
	int status;

	io_pack.device_index = device_index;
	io_pack.bar = bar;
	io_pack.offset = offset;
	io_pack.size = DWORD;
	
	status = ioctl(fd, apci_read_ioctl, &io_pack);
	
	if (data != NULL) *data = io_pack.data;
	
	return status;

}

int apci_wait_for_irq(int fd, unsigned long device_index)
{
	return ioctl(fd, apci_wait_for_irq_ioctl, device_index);
}
int apci_cancel_irq(int fd, unsigned long device_index)
{
	return ioctl(fd, apci_cancel_wait_ioctl, device_index);
}
