
#include <stdint.h>
#include <math.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "apcilib.h"
#define DEVICEPATH "/dev/apci/pcie_da16_6_0"


#define BAR_REGISTER 2

int apci;



//------------------------------------------------------------------------------------
int main (int argc, char **argv)
{

	uint32_t Version = 0;
	apci_read32(apci, 1, BAR_REGISTER, 0x68, &Version);
	printf("\nPCIe-DA16-6 Family ADC Sample 0\n");
	apci = -1;
	if (argc > 1)
	{
		apci = open(argv[1], O_RDONLY);	// open device file from command line
		if (apci < 0)
			printf("Couldn't open device file on command line [%s]: do you need sudo? /dev/apci? \n", argv[1]);
	}

	if (apci < 0) // if the command line didn't work, or if they didn't pass a parameter
	{
		apci = open(DEVICEPATH, O_RDONLY);
		if (apci < 0)
		{
			printf("Device file %s could not be opened. Please ensure the APCI driver module is loaded or try sudo?.\n", DEVICEPATH);
		}
	}

	int chan;
	__u8 data_ignore;
	apci_write16(apci, 0, BAR_REGISTER, 0x00, 0x8000); // set DAC0 to midscale
	apci_write16(apci, 0, BAR_REGISTER, 0x02, 0x8000);
	apci_write16(apci, 0, BAR_REGISTER, 0x04, 0x8000);
	apci_write16(apci, 0, BAR_REGISTER, 0x06, 0x8000);
	apci_write16(apci, 0, BAR_REGISTER, 0x08, 0x8000);
	apci_write16(apci, 0, BAR_REGISTER, 0x0A, 0x8000); // set DAC5 to midscale
	apci_read8(apci, 0, BAR_REGISTER, 0x0F, &data_ignore);

	printf("Demonstrating Async (non-simultaneous) DAC updates");
	apci_read8(apci, 0, BAR_REGISTER, 0x0A, &data_ignore); // take DACs out of simultaneous mode
	for (int counts = 0; counts < 0xFFFF; counts += 0x1000)
	{
		for (chan = 0; chan < 6; chan++)
		{
			apci_write16(apci, 0, BAR_REGISTER, chan * 2, counts);
		}
		printf("wrote [%04x] to DACs\n", counts);
		sleep(2);
	}


	printf("Demonstrating Simultaneous DAC updates");

	apci_read8(apci, 0, BAR_REGISTER, 0x00, &data_ignore); // put DACs into simultaneous mode without updating outputs
	for (int counts = 0; counts < 0xFFFF; counts += 0x1000)
	{
		for (chan = 0; chan < 6; chan++)
		{
			apci_write16(apci, 0, BAR_REGISTER, chan * 2, counts);
		}
		printf("wrote [%04x] to DACs without updating outputs; now updating all at once\n", counts);
		apci_read8(apci, 0, BAR_REGISTER, 0x08, &data_ignore); // update all outputs at once and leave DACs in simultaneous mode
		sleep(2);
	}

	printf("\nSample Done.\n"); // never prints due to CTRL-C abort
}




