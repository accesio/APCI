
#include <stdint.h>
#include <math.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "apcilib.h"


#define BAR_REGISTER 1
#define ofsFPGARevision 0xA0
#define ofsFlashAddress 0x70
#define ofsFlashData 0x74
#define ofsFlashErase 0x78
#define bmFlashWriteBit 0x80000000

int apci;
uint32_t EraseSentinel = 0x494f0000;	   // | DeviceID
uint32_t EraseSectorSentinel = 0x0ACCE500; // | iSector
uint32_t FileBytesRead = 0;
uint8_t FlashData[0x80000];

void ReadFile(char *FileName, uint8_t FlashData[])
{
	int fd = open(FileName, O_RDONLY);
	if (fd < 0)
	{
		printf("Could not open file %s\n", FileName);
		exit(1);
	}

	FileBytesRead = read(fd, FlashData, 0x80000);
	if (FileBytesRead < 0 || FileBytesRead > 0x80000)
	{
		printf("Could not read file %s\n", FileName);
		exit(1);
	}
	printf("Read %d bytes from file %s\n", FileBytesRead, FileName);
}

void EraseFlash()
{
	for (int iSector = 0; iSector < 8; iSector++)
	{
		apci_write32(apci, 1, BAR_REGISTER, ofsFlashErase, EraseSentinel);
		apci_write32(apci, 1, BAR_REGISTER, ofsFlashErase, EraseSectorSentinel | iSector);
		printf("Erasing Flash Sector %d/8 via %08X\n", iSector + 1, EraseSectorSentinel | iSector);
		sleep(2);
	}
}

void PrimitiveWriteFlashByte(uint32_t offset, uint8_t data)
{
	apci_write32(apci, 1, BAR_REGISTER, ofsFlashData, data);
	apci_write32(apci, 1, BAR_REGISTER, ofsFlashAddress, offset | bmFlashWriteBit);
	usleep(25);
}

uint8_t PrimitiveReadFlashByte(uint32_t offset)
{
	uint32_t data;
	apci_write32(apci, 1, BAR_REGISTER, ofsFlashAddress, offset & ~bmFlashWriteBit);
	usleep(25);
	apci_read32(apci, 1, BAR_REGISTER, ofsFlashData, &data);
	return data & 0x000000FF;
}

void WriteFlash()
{
	printf("Writing %d bytes to Flash\n", FileBytesRead);
	for (int iByte = 0; iByte < FileBytesRead; iByte++)
	{
		PrimitiveWriteFlashByte(iByte, FlashData[iByte]);
		if (iByte % (FileBytesRead / 8) == 0)
		{
			printf("wrote %d of %d to flash\n", iByte, FileBytesRead);
		}
	}
	printf("Flash write complete\n");
}

int VerifyFlash()
{
	printf("Verifying %d bytes from Flash\n", FileBytesRead);
	int Result = 1;
	for (int iByte = 0; iByte < FileBytesRead; iByte++)
	{
		uint8_t data = PrimitiveReadFlashByte(iByte);
		if (data != FlashData[iByte])
		{
			printf("Verify failed at byte %8d; Got %02X, expected %02X\n", iByte, data, FlashData[iByte]);
			Result = 0;
			break;
		}

		if (iByte % (FileBytesRead / 8) == 0)
		{
			printf("Verified %d of %d to flash\n", iByte, FileBytesRead);
		}
	}
	if (Result)
		printf("\nVerification of flash contents succeeded.\n");
	else
		printf("\nVerification of flash contents failed.\nRETRYING\n");
	return Result;
}

int VerifyErase()
{
	int Result = 1;
	for (int iByte = 0; iByte < 0x80000; iByte++)
	{
		uint8_t data = PrimitiveReadFlashByte(iByte);
		if (data != 0xFF)
		{
			printf("Erase_Verify failed at byte %8d; Got %02X, expected %02X\n", iByte, data, FlashData[iByte]);
			Result = 0;
			break;
		}

		if (iByte % (FileBytesRead / 8) == 0)
		{
			printf("Verified erasure of %d/%d of the flash\n", iByte, FileBytesRead);
		}
	}
	if (Result)
		printf("\nVerification of flash erasure succeeded.\n");
	else
		printf("\nVerification of flash erasure failed.\n");
	return Result;
}

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		printf("\nUsage: %s <devfile> <filename.rpd>\n", argv[0]);
		exit(1);
	}

	// this code section is weak.  TODO: improve plug-and-play and device file detection / selection; enumerate /dev/apci/ and if >1 device have menu?
	apci = -1;
	apci = open(argv[1], O_RDONLY); // open device file from command line
	if (apci < 0)
	{
		printf("Couldn't open device file on command line: do you need sudo? Check /dev/apci? [%s]\n", argv[1]);
		exit(2);
	}

	uint32_t Version = 0;
	apci_read32(apci, 1, BAR_REGISTER, ofsFPGARevision, &Version);

	printf("\nACCES ISP-FPGA Engineering Utility for Linux [FPGA Rev %08X]\n", Version);

	ReadFile(argv[2], FlashData);

	/* query DeviceID for use in EraseSentinel */
	unsigned int DeviceID = 0;
	unsigned long bar[6];
	apci_get_device_info(apci, 1, &DeviceID, bar);

	/* Verify the DeviceID is on approved list for this code */
	if (DeviceID != 0xC2EC)	
	{
		printf("\nDeviceID %08X is not supported by this utility.\n", DeviceID);
		exit(1);
	}

	EraseSentinel |= DeviceID;
	printf("EraseSentinel is %08X\n", EraseSentinel);
	do
	{
		EraseFlash();
		if (!VerifyErase())
			continue;
		
		/* write data to flash */
		WriteFlash();

		/* verify data read from flash */
	} while (!VerifyFlash());

	printf("Flash Update Successful.  Wrote %s to Device %04x.\n\n", argv[2], DeviceID); // never prints due to CTRL-C abort
}