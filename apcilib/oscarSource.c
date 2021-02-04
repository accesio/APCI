#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pci/pci.h>
#include <sys/procmgr.h>
#include <sys/neutrino.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "sg_target.h"
#include "sg_common.h"


//Parameters
const pci_vid_t my_vid = 0x494F;
const pci_did_t my_did = 0xC0EC;
const uint32_t my_BAR = 2;

//Start
int main(void) {
	pci_bdf_t my_device;
	pci_devhdl_t my_handle;
	pci_err_t error;

	puts("Oscar testa!!!"); /* prints Hello World!!! */

	//Abilities
	if(sg_setAbilities())
		return EXIT_FAILURE;

	printf("Let's look for the PCI device...\n");
	my_device = pci_device_find(0, my_vid, my_did, PCI_CCODE_ANY);
	printf("\tFound: 0x%08X (Bus %d, Slot %d, Function %d)\n",(uint32_t)my_device, PCI_BUS(my_device), PCI_DEV(my_device), PCI_FUNC(my_device));
	if(my_device == PCI_BDF_NONE)
		return EXIT_FAILURE;


	printf("Try to attach the device...\n");
	my_handle = pci_device_attach(my_device, pci_attachFlags_e_SHARED | pci_attachFlags_e_OWNER, &error);
	printf("\tHandle: 0x%p\n",my_handle);
	if(my_handle == NULL)
		return EXIT_FAILURE;
	if(error != PCI_ERR_OK)
	{
		printf("\tError: %d\n",error);
		return EXIT_FAILURE;
	}


	printf("Read out BAR information:\n");
	pci_ba_t ba[7]; // the maximum number of entries that can be returned
	int_t nba = NELEMENTS(ba);
	error = pci_device_read_ba(my_handle, &nba, ba, pci_reqType_e_UNSPECIFIED);
	if(error != PCI_ERR_OK)
	{
		printf("\tError: %d -> %s\n",error, strerror((int)error));
		return EXIT_FAILURE;
	}
	for (int i=0; i<nba; i++)
	{
		printf("\tBAR %i:\n",ba[i].bar_num);
		printf("\t\taddr 0x%p\n", ba[i].addr);
		printf("\t\tsize %i\n", ba[i].size);
		printf("\t\ttype %i\n", ba[i].type);
	}


	printf("Try to map the memory...\n");
	uint32_t* my_regs = mmap_device_memory(NULL, ba[my_BAR].size,PROT_READ|PROT_WRITE|PROT_NOCACHE, 0, ba[my_BAR].addr);
	printf("\tObtained address: 0x%p\n",my_regs);
	if (my_regs == MAP_FAILED)
	{
		printf("\tError: %d\n", errno);
		return EXIT_FAILURE;
	}

	printf("Reset and Power register: %u (0x%08X)\n", my_regs[0], my_regs[0]);
	printf("ADC clock register: %u (0x%08X)\n", my_regs[3], my_regs[3]);
	printf("FPGA revision register: %u (0x%08X)\n", my_regs[26], my_regs[26]);

	printf("Thanks for watching!\n");
	return EXIT_SUCCESS;
}
