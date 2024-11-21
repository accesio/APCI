/*
This software is a driver for ACCES I/O Products, Inc. PCI and other plug-and-play cards.
Copyright (C) 2007-2024 ACCES I/O Products, Inc.

This program is free software; you can redistribute it and/or
modify it at will.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

In addition ACCES provides other licenses with its software at customer request.
For more information please contact the ACCES software department at
(800)-326-1649 or visit www.accesio.com
*/

#include <linux/types.h>

int apci_get_devices(int fd);

int apci_get_device_info(int fd, unsigned long device_index, unsigned int *dev_id, unsigned long base_addresses[6]);

int apci_write8(int fd, unsigned long device_index, int bar, int offset, __u8 data);
int apci_write16(int fd, unsigned long device_index, int bar, int offset, __u16 data);
int apci_write32(int fd, unsigned long device_index, int bar, int offset, __u32 data);

int apci_read8(int fd, unsigned long device_index, int bar, int offset, __u8 *data);
int apci_read16(int fd, unsigned long device_index, int bar, int offset, __u16 *data);
int apci_read32(int fd, unsigned long device_index, int bar, int offset, __u32 *data);

int apci_wait_for_irq(int fd, unsigned long device_index);
int apci_cancel_irq(int fd, unsigned long device_index);

int apci_dma_transfer_size(int fd, unsigned long device_index, __u8 num_slots, size_t slot_size);
int apci_dma_data_ready(int fd, unsigned long device_index, int *start_index, int *slots, int *data_discarded);
int apci_dma_data_done(int fd, unsigned long device_index, int num_slots);

int apci_writebuf8(int fd, unsigned long device_index, int bar, int bar_offset, unsigned int mmap_offset, int length);
int apci_writebuf16(int fd, unsigned long device_index, int bar, int bar_offset, unsigned int mmap_offset, int length);
int apci_writebuf32(int fd, unsigned long device_index, int bar, int bar_offset, unsigned int mmap_offset, int length);

int apci_dac_buffer_size (int fd, unsigned long size);
