/*
This software is a driver for ACCES I/O Products, Inc. PCI and other plug-and-play cards.
Copyright (C) 2007-2024 ACCES I/O Products, Inc.

This program is free software; you can redistribute it and/or
modify it at will.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE

In addition ACCES provides other licenses with its software at customer request.
For more information please contact the ACCES software department at
(800)-326-1649 or visit www.accesio.com
*/

#include <sys/ioctl.h>
#include <stddef.h>

#include "apcilib.h"
#include "apci_ioctl.h"

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


int apci_writebuf32(int fd, unsigned long device_index, int bar, int bar_offset, unsigned int mmap_offset, int length)
{
	buff_iopack buff_pack;
	buff_pack.device_index = device_index;
	buff_pack.bar = bar;
	buff_pack.bar_offset = bar_offset;
	buff_pack.mmap_offset = mmap_offset;
	buff_pack.length = length;
	buff_pack.size = DWORD;

	return ioctl(fd, apci_write_buff_ioctl, &buff_pack);
}

int apci_writebuf16(int fd, unsigned long device_index, int bar, int bar_offset, unsigned int mmap_offset, int length)
{
	buff_iopack buff_pack;
	buff_pack.device_index = device_index;
	buff_pack.bar = bar;
	buff_pack.bar_offset = bar_offset;
	buff_pack.mmap_offset = mmap_offset;
	buff_pack.length = length;
	buff_pack.size = WORD;

	return ioctl(fd, apci_write_buff_ioctl, &buff_pack);
}

int apci_writebuf8(int fd, unsigned long device_index, int bar, int bar_offset, unsigned int mmap_offset, int length)
{
	buff_iopack buff_pack;
	buff_pack.device_index = device_index;
	buff_pack.bar = bar;
	buff_pack.bar_offset = bar_offset;
	buff_pack.mmap_offset = mmap_offset;
	buff_pack.length = length;
	buff_pack.size = BYTE;

	return ioctl(fd, apci_write_buff_ioctl, &buff_pack);
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

int apci_dma_transfer_size(int fd, unsigned long device_index, __u8 num_slots, size_t slot_size)
{
	dma_buffer_settings_t settings;
	settings.num_slots = num_slots;
	settings.slot_size = slot_size;
	return ioctl(fd, apci_set_dma_transfer_size, &settings);
	(void)device_index;
}

int apci_dma_data_ready(int fd, unsigned long device_index, int *start_index, int *slots, int *data_discarded)
{
	int status;
	data_ready_t ready = {0};
	status = ioctl(fd, apci_data_ready, &ready);

	*start_index = ready.start_index;
	*slots = ready.slots;
	*data_discarded = ready.data_discarded;

	return status;
	(void)device_index;
}

int apci_dma_data_done(int fd, unsigned long device_index, int num_slots)
{
	return ioctl(fd, apci_data_done, num_slots);
	(void)device_index;
}

int apci_dac_buffer_size (int fd, unsigned long size)
{
	return ioctl(fd, apci_set_dac_buff_size, size);
}
