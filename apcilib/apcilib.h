#pragma once

/*
 * Copyright (C) 2007-2024 ACCES I/O Products, Inc.
 * SPDX-FileCopyrightText: 2026 ACCES I/O Products, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * SPDX-License-Identifier: LicenseRef-WITH-ACCES
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <linux/types.h>
#include <stddef.h>        // for size_t


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

int apci_readbuf32(int fd, unsigned long device_index, int bar, int bar_offset, unsigned int mmap_offset, int length);

int apci_dac_buffer_size (int fd, unsigned long size);

#ifdef __cplusplus
}
#endif
