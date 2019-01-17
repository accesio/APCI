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




//for the magic number I have decided to use 0xE0. There really is no
//reason for this choice other than the fact that it is not listed as taken in
//ioctl-number.txt and it is pretty obvious that no  one is updating the list anyway.
//to make it easier to change in the future I will define it

#define ACCES_MAGIC_NUM 0xE0

//uncomment for kernel debug messages
#define A_PCI_DEBUG


//the vendor ID for all the PCI cards this driver will support
#define A_VENDOR_ID 0x494F

//the device IDs for all the PCI cards this driver will support

#define PCI_DIO_24H		0x0C50
#define PCI_DIO_24D		0x0C51
#define PCI_DIO_24H_C	0x0E51
#define PCI_DIO_24D_C	0x0E52
#define PCI_DIO_24S		0x0E50
#define PCI_DIO_48		0x0C60
#define PCI_DIO_48S		0x0E60
#define P104_DIO_48S		0x0E61
#define PCI_DIO_48H		0x0C60
#define PCI_DIO_48HS		0x0E60
#define PCI_DIO_72		0x0C68
#define P104_DIO_96		0x0C69
#define PCI_DIO_96		0x0C70
#define PCI_DIO_96CT		0x2C50
#define PCI_DIO_96C3		0x2C58
#define PCI_DIO_120		0x0C78
#define PCI_AI12_16		0xACA8
#define PCI_AI12_16A		0xACA9
#define PCI_AIO12_16		0xECA8
#define PCI_A12_16A		0xECAA
#define LPCI_A16_16A		0xECE8
#define PCI_DA12_16		0x6CB0
#define PCI_DA12_8		0x6CA8
#define PCI_DA12_6		0x6CA0
#define PCI_DA12_4		0x6C98
#define PCI_DA12_2		0x6C90
#define PCI_DA12_16V		0x6CB1
#define PCI_DA12_8V		0x6CA9
#define LPCI_IIRO_8		0x0F01
#define PCI_IIRO_8		0x0F00
#define PCI_IIRO_16		0x0F08
#define PCI_IDI_48		0x0920
#define PCI_IDO_48		0x0520
#define PCI_IDIO_16		0x0DC8
#define PCI_WDG_2S		0x1250 //TODO: Find out if the Watch Dog Cards should really be here
#define PCI_WDG_CSM		0x22C0
#define PCI_WDG_IMPAC	0x12D0

//structs used by ioctls
typedef struct
{
	unsigned long device_index;
	int dev_id;
	unsigned long base_addresses[6]; //most likely only base_addresses[2] will be useful
												//consult your product manual for more information
}info_struct;

enum SIZE {BYTE = 0, WORD, DWORD};

typedef struct
{
	unsigned long device_index;
	int bar;
	unsigned int offset;
	enum SIZE size;
	__u32 data;
}iopack;
	

	

#define apci_get_devices_ioctl _IO(ACCES_MAGIC_NUM, 1)
#define apci_get_device_info_ioctl _IOR(ACCES_MAGIC_NUM, 2, info_struct *)
#define apci_write_ioctl _IOW(ACCES_MAGIC_NUM, 3, iopack *)
#define apci_read_ioctl _IOR(ACCES_MAGIC_NUM, 4, iopack *)
#define apci_wait_for_irq_ioctl _IOR(ACCES_MAGIC_NUM, 5, unsigned long)
#define apci_cancel_wait_ioctl _IOW(ACCES_MAGIC_NUM, 6, unsigned long)



















