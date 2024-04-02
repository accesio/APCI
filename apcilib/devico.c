#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdbool.h>
#include "apcilib.h"
uint8_t *baseAddr = NULL;
//----------------------------------------------------------------------
// Static Variable Definitions
//----------------------------------------------------------------------
static bool gExit = false;
//----------------------------------------------------------------------
// Function Prototypes
//----------------------------------------------------------------------
//======================================================================
//   FUNCTION DEFINITIONS
//======================================================================
static uint64_t triggerTsNs = 0;
uint8_t readb(const uint8_t *p, uint32_t off)
{
	printf("read offset %x\n", off);
	return (*(p + off));
}
void writeb(uint8_t *p, uint32_t off, uint8_t val)
{
	printf("write offset %hhx\n", off);
	*(p + off) = val;
}

// Shutdown capture    with SIGINT
void sigintHandler(int signum)
{
	gExit = true;
}

void *irqTriggerThreadEntry(void *p)
{
	while (!gExit)
	{
		for (int i = 0; i < 2; i++)
		{
			writeb(baseAddr, (4 * i) + 1, 0xff);
		}
		sleep(1);
		for (int i = 0; i < 2; i++)
		{
			writeb(baseAddr, (4 * i) + 1, 0);
		}
		sleep(1);
	}

	printf("irqTriggerThreadEntry exiting");
	return NULL;
}

void *irqHandlerThreadEntry(void *p)
{
	uint64_t lastPpsTs = 0;
	uint8_t b = 0;
	// clear pending interrupts
	writeb(baseAddr, 0x0f, 0);
	// Enable "CoS" IRQ for A G0/1;
	writeb(baseAddr, 0x0b, 0x36);
	while (!gExit)
	{
		bool irqRecv = false;
		while (!gExit && !irqRecv)
		{
			if ((b = readb(baseAddr, 0xf)))
			{
				irqRecv = true;
				writeb(baseAddr, 0xf, 0xff);
			}
			usleep(10000);
		}
		printf("0xf:%02x", b);
	}
	// Disable "CoS" IRQs;
	writeb(baseAddr, 0x0b, 0xff);
	printf("irqHandlerThreadEntry exiting\n");
	return NULL;
}

int main(int argc, char **argv)
{
	signal(SIGTERM, sigintHandler);
	int fd = open("/dev/apci/pcie_dio_48s_0", O_RDWR);
	if (fd < 0)
	{
		printf(
			"Device file could not be opened. Please ensure the apci driver "
			"module is loaded -- you may need sudo.\n");
		exit(0);
	}
	baseAddr = (uint8_t *)mmap64(NULL, 12288, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 8192);
	printf("baseAddr: %p\n", baseAddr);
	printf("0xc:%02x\n", readb(baseAddr, 0xc));
	// Disable "CoS" interrupts
	writeb(baseAddr, 0x0b, 0xff);
	// set A input, B&C output
	writeb(baseAddr, 3, 0x90);
	writeb(baseAddr, 7, 0x90);
	printf("Group 0 Control: %02x\n", readb(baseAddr, 3));
	printf("Group 1 Control: %02x\n", readb(baseAddr, 7));
	// Clear Port B G0/G1 pins
	writeb(baseAddr, 0x1, 0);
	writeb(baseAddr, 0x5, 0);
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_t tid;
	pthread_create(&tid, &attr, irqHandlerThreadEntry, NULL);
	pthread_create(&tid, &attr, irqTriggerThreadEntry, NULL);
	while (!gExit)
	{
		sleep(1);
	}
	munmap(baseAddr, 12288);
	return 0;
}