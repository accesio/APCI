#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdint.h>

#include <sys/mman.h>

#include <unistd.h>

#include <math.h>

#include "apcilib.h"

#define DMA_BUFF_SIZE 4096 * 2 * 2

#define RESETOFFSET         0x00
#define IRQENABLEOFFSET     0x01
#define DACOFFSET           0x04
#define BASECLOCKOFFSET     0x0C
#define DIVISOROFFSET       0x10
#define ADCRANGEOFFSET      0x18
#define AFLEVELOFFSET       0x20
#define ADCDEPTHOFFSET      0x28
#define ADCDATAOFFSET1      0x30
#define ADCDATAOFFSET2      0x34
#define ADCSTARTSTOPOFFSET  0x38
#define ADCIMMEDIATEOFFSET  0x3A


#define SAMPLE_RATE 10000.0
#define HIGH_CHANNEL 4

int set_rate (int fd, double *Hz)
{
  uint32_t base_clock;
  uint32_t divisor;
  int status;

  status = apci_read32(fd, 1, 2, BASECLOCKOFFSET, &base_clock);

  printf("status = %d, base_clock = %d\n", status, base_clock);

  divisor = round(base_clock / *Hz);
  *Hz = base_clock /divisor;

  printf("divisor = %d, Hz = %f\n", divisor, *Hz);

  status = apci_write32(fd, 1, 2, DIVISOROFFSET, divisor);

  return status;

}

void dump_buffer_half (volatile void *mmap_addr, int half)
{
  int i;
  if (half == 1) mmap_addr += DMA_BUFF_SIZE / 2;

  for (i = 0 ; i < DMA_BUFF_SIZE / 4 ; i++)
  {
    printf("0x%x ", ((uint16_t *)mmap_addr)[i]);
    if (!(i% 16)) printf("\n");
  }
}

int main (int argc, char **argv)
{
	int fd;
	int i;
	volatile void *mmap_addr;
	int status = 0;
  double rate = SAMPLE_RATE;
	uint32_t depth_readback;
	uint16_t start_command;


	fd = open("/dev/apci/mPCIe_AIO16_16F_proto_0", O_RDONLY);

	if (fd < 0)
	{
		printf("Device file could not be opened. Please ensure the iogen driver module is loaded.\n");
		return -1;
	}

  //reset everything
  apci_write8(fd, 1, 2, RESETOFFSET, 0xf);

	sleep(1);

	mmap_addr = mmap(NULL, DMA_BUFF_SIZE, PROT_READ, MAP_SHARED, fd, 0);

	if (mmap_addr == NULL)
	{
		printf("mmap_addr is NULL\n");
		return -1;
	}

  set_rate(fd, &rate);

  //set ranges
  apci_write32(fd, 1, 2, ADCRANGEOFFSET, 0);
  apci_write32(fd, 1, 2, ADCRANGEOFFSET + 8, 0);



  //set depth of FIFO to generate IRQ
  apci_write32(fd, 1, 2, AFLEVELOFFSET, 0xf00);
	apci_read32(fd, 1, 2, AFLEVELOFFSET, &depth_readback);

	printf("depth_readback = 0x%x\n", depth_readback);

  //enable IRQ
  apci_write8(fd, 1, 2, IRQENABLEOFFSET, 1);

  //Start command?
	start_command = 0xfcee;
	start_command &= ~(7 << 12);
	start_command |= HIGH_CHANNEL << 12;
  apci_write16(fd, 1, 2, ADCSTARTSTOPOFFSET, start_command);

	printf("start_command = 0x%x\n", start_command);

  status = apci_wait_for_irq(fd, 1);

  printf("status after irq = %d\n", status);

  if (status >= 0)
  {
    dump_buffer_half(mmap_addr, status);
  }

  //disable IRQ
  apci_write8(fd, 1, 2, IRQENABLEOFFSET, 0);


}
