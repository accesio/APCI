
#include <algorithm>   // std::max
#include <filesystem>  // std::filesystem::directory_iterator
#include <string>      // std::string
#include <stdint.h>    // uint8_t, uint32_t
#include <stdio.h>     // printf
#include <stdlib.h>    // exit
#include <fcntl.h>     // open, O_*
#include <unistd.h>    // read, close, usleep, sleep

#include "apcilib.h"

/* *********************************************************************************** */
/* 	                      WARNING     WARNING      WARNING     WARNING                 */
/* The following #define constants must be changed to match your specific hardware     */
#define BAR_REGISTER 1
#define ofsFPGARevision 0xA0
#define ofsFlashAddress 0x70
#define ofsFlashData 0x74
#define ofsFlashErase 0x78
/* *********************************************************************************** */

#define bmFlashWriteBit 0x80000000

int apci;
uint32_t EraseSentinel = 0x494f0000;	   // | DeviceID
uint32_t EraseSectorSentinel = 0x0ACCE500; // | iSector
int FileBytesRead = 0;
uint8_t FlashData[0x80000];

#define NUM_CHUNKS 256
static inline unsigned pct(unsigned long long cur, unsigned long long total)
{
	if (!total)
		return 0;
	if (cur > total)
		cur = total;
	return (unsigned)((100ULL * cur) / total);
}

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
	printf("Loaded %d bytes from file %s\n", FileBytesRead, FileName);
}

void EraseFlash()
{
	for (int iSector = 0; iSector < 8; iSector++)
	{
		apci_write32(apci, 1, BAR_REGISTER, ofsFlashErase, EraseSentinel);
		apci_write32(apci, 1, BAR_REGISTER, ofsFlashErase, EraseSectorSentinel | iSector);
		printf("Erasing Flash Sector %d/8 via %08X\r", iSector + 1, EraseSectorSentinel | iSector);
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
	printf("\nWriting %d bytes to Flash\n", FileBytesRead);
	const int chunk = std::max(1, FileBytesRead / NUM_CHUNKS);
	for (int iByte = 0; iByte < FileBytesRead; iByte++)
	{
		PrimitiveWriteFlashByte(iByte, FlashData[iByte]);

		int done = iByte + 1;
		if ((done % chunk) == 0 || done == FileBytesRead || done == 1)
		{
			printf("\rWrote     %9d / %-9d (%3u%%) to flash", done, FileBytesRead, pct(done, FileBytesRead));
			fflush(stdout);
		}
	}
	printf("\nFlash write complete\n");
}

int VerifyFlash()
{
	printf("\nVerifying %d bytes from Flash\n", FileBytesRead);
	int Result = 1;
	const int chunk = std::max(1, FileBytesRead / NUM_CHUNKS);

	for (int iByte = 0; iByte < FileBytesRead; iByte++)
	{
		uint8_t data = PrimitiveReadFlashByte(iByte);
		if (data != FlashData[iByte])
		{
			printf("\nVerify failed at byte %8d; Got %02X, expected %02X\n",
				   iByte, data, FlashData[iByte]);
			Result = 0;
			break;
		}

		int done = iByte + 1;
		if ((done % chunk) == 0 || done == FileBytesRead || done == 1)
		{
			printf("\rVerified  %9d / %-9d (%3u%%) of the flash", done, FileBytesRead, pct(done, FileBytesRead));
			fflush(stdout);
		}
	}
	printf("\n%s\n", Result ? "Verification of flash contents succeeded."
							: "Verification of flash contents failed.\nRETRYING");
	return Result;
}

int VerifyErase()
{
	printf("\nVerifying Erasure of Flash\n");
	int Result = 1;
	const int total = 0x80000;
	const int chunk = std::max(1, total / NUM_CHUNKS);

	for (int iByte = 0; iByte < total; iByte++)
	{
		uint8_t data = PrimitiveReadFlashByte(iByte);
		if (data != 0xFF)
		{
			printf("\nErase_Verify failed at byte %8d; Got %02X, expected FF\n",
				   iByte, data);
			Result = 0;
			break;
		}

		int done = iByte + 1;
		if ((done % chunk) == 0 || done == total || done == 1)
		{
			printf("\rErased    %9d / %-9d (%3u%%) of the flash", done, total, pct(done, total));
			fflush(stdout);
		}
	}
	printf("\n%s\n", Result ? "Verification of flash erasure succeeded."
							: "Verification of flash erasure failed.");
	return Result;
}

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		printf("\nUsage: %s <filename.rpd>\n", argv[0]);
		exit(1);
	}
	std::string devicefile = "";
	std::string devicepath = "/dev/apci";
	for (const auto &devfile : std::filesystem::directory_iterator(devicepath))
	{
		apci = open(devfile.path().c_str(), O_RDONLY);
		if (apci >= 0)
		{
			devicefile = devfile.path().c_str();
			break;
		}
	}
	if (apci < 0)
	{
		printf("Couldn't open device file on command line: do you need sudo? Check /dev/apci? [%s]\n", argv[1]);
		exit(2);
	}

	setvbuf(stdout, NULL, _IONBF, 0);
	printf("Using device @ %s", devicefile.c_str());

	uint32_t Version = 0;
	apci_read32(apci, 1, BAR_REGISTER, ofsFPGARevision, &Version);
	printf("\nACCES ISP-FPGA Engineering Utility for Linux [current FPGA Rev %08X]\n\n", Version);


	ReadFile(argv[1], FlashData);

	/* query DeviceID for use in EraseSentinel */
	unsigned int DeviceID = 0;
	unsigned long bar[6];
	apci_get_device_info(apci, 1, &DeviceID, bar);

	/* Verify the DeviceID is on approved list for this code */
	//	if (DeviceID != 0xC2EC)
	//	{
	//		printf("\nDeviceID %08X is not supported by this utility.\n", DeviceID);
	//		exit(1);
	//	}

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

	printf("Flash Update Successful.  Wrote %s to Device %04x.\n\n", argv[1], DeviceID);
	printf("----`sudo reboot` the eNET-AIO to load the new FPGA from Flash!!----\n");
}
