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



#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <linux/types.h>

#include "apcilib.h"

int fd;


int main (void)
{
	__u8 relays = 1;
	int count, inner;
	int status;
	int devices;
	unsigned int dev_id;
	unsigned long addresses[6];

	fd = open_dev_file();
	
	devices = apci_get_devices(fd);
	
	printf("Devices Found:%d\n", devices);
	

	for (count = 0; count < devices; count++)
	{
		status = apci_get_device_info(fd, count, &dev_id, addresses);
		
		printf("Device %d\n", count);
		printf("status:%d\n", status);
		printf("Device ID: %04X\n", dev_id);
		
		for (inner = 0; inner < 6; inner++)
		{
			printf("BAR[%d]:%X\n", inner, addresses[inner]);
		}
		
		fflush(stdout);
		
	}
	
	close(fd);
}

int open_dev_file()
{
	int fd;

	fd = open("/dev/apci", O_RDONLY);

	if (fd < 0)
	{
		printf("Device file could not be opened. Please ensure the iogen driver module is loaded.\n");
		exit(0);
	}
	
	return fd;

}
