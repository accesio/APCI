#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdint.h>

#include <sys/mman.h>

#include <unistd.h>

#include "apcilib.h"

int main (int argc, char **argv)
{
	int fd;
	int i;
	volatile void *mmap_addr;
	int status = 0;


	fd = open("/dev/apci/mPCIe_AIO16_16F_proto_0", O_RDONLY);

	if (fd < 0)
	{
		printf("Device file could not be opened. Please ensure the iogen driver module is loaded.\n");
		return -1;
	}
	mmap_addr = mmap(NULL, 4096 * 2 *2, PROT_READ, MAP_SHARED, fd, 0);

	if (mmap_addr == NULL)
	{
		printf("mmap_addr is NULL\n");
		return -1;
	}

	for (i = 0 ; i < 4096 ; i++)
	{
		printf("0x%x ", ((uint16_t *)mmap_addr)[i]);
		if (!(i% 16)) printf("\n");
	}

	status = apci_dma(fd, 0);

	if (status)
	{
		printf("Status is %d\n", status);
	}
	else
	{
		printf("\n\n\n\n");
		printf("Status is %d\n", status);
		sleep(1);
		for (i = 0 ; i < 4096 ; i++)
		{
			printf("0x%x ", ((uint16_t *)mmap_addr)[i]);
			if (!(i% 16)) printf("\n");
		}
	}

	status = apci_dma(fd, 0);

	if (status)
	{
		printf("Status is %d\n", status);
	}
	else
	{
		printf("\n\n\n\n");
		printf("Status is %d\n", status);
		sleep(1);
		for (i = 0 ; i < 4096 ; i++)
		{
			printf("0x%x ", ((uint16_t *)mmap_addr)[i]);
			if (!(i% 16)) printf("\n");
		}
	}




}