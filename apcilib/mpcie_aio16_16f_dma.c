#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdint.h>

#include <sys/mman.h>

#include <unistd.h>

#include <math.h>

#include "apcilib.h"

#define DMA_BUFF_SIZE 4096 * 8 * 2

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


#define SAMPLE_RATE 200.0
#define HIGH_CHANNEL 7
#define SAMPLES_FOR_IRQ 0x800

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


	mmap_addr = mmap(NULL, DMA_BUFF_SIZE, PROT_READ, MAP_SHARED, fd, 0);

	if (mmap_addr == NULL)
	{
		printf("mmap_addr is NULL\n");
		return -1;
	}

  //reset everything
  apci_write8(fd, 1, 2, RESETOFFSET, 0xf);
	sleep(1);

  //set depth of FIFO to generate IRQ
  apci_write32(fd, 1, 2, AFLEVELOFFSET, SAMPLES_FOR_IRQ);
	apci_read32(fd, 1, 2, AFLEVELOFFSET, &depth_readback);
	printf("depth_readback = 0x%x\n", depth_readback);

  apci_dma_transfer_size(fd, 1, SAMPLES_FOR_IRQ * 8);

  set_rate(fd, &rate);

  //set ranges
  apci_write32(fd, 1, 2, ADCRANGEOFFSET, 0);
  apci_write32(fd, 1, 2, ADCRANGEOFFSET + 4, 0);

  //enable IRQ
  apci_write8(fd, 1, 2, IRQENABLEOFFSET, 0x1);

  //clear IRQs (tried with and without this)
  apci_write8(fd, 1, 2, 0x2, 0xff);


  //Start command?
	start_command = 0xfcee;
	start_command &= ~(7 << 12);
	start_command |= HIGH_CHANNEL << 12;
  start_command |= 1;
  apci_write16(fd, 1, 2, ADCSTARTSTOPOFFSET, start_command);

	printf("start_command = 0x%x\n", start_command);

  for (int i = 0 ; i < 30 ; i++)
  {
    uint8_t irq_register;
    uint32_t current_depth;
    uint8_t irq_enable;
    apci_read8(fd, 1, 2, 2, &irq_register);
    apci_read32(fd, 1, 2,ADCDEPTHOFFSET, &current_depth);
    apci_read8(fd, 1, 2, 1, &irq_enable);
    printf("irq_register = %d, current_depth = 0x%x, irq_enable=0x%x\n", irq_register, current_depth, irq_enable);
    sleep(1);
  }


  //dump_buffer_half(mmap_addr, 0);

    for (int i = 0 ; i < 10 ; i++)
  {
    status = apci_wait_for_irq(fd, 1);
    printf("status after irq = %d\n", status);
  }

//  status = apci_wait_for_irq(fd, 1);


  //printf("status after irq = %d\n", status);

  // if (status >= 0)
  // {
  //   dump_buffer_half(mmap_addr, status);
  // }

  //disable IRQ
  apci_write8(fd, 1, 2, IRQENABLEOFFSET, 0);
  //clear IRQs (tried with and without this)
  //apci_write8(fd, 1, 2, 0x2, 0xff);
  apci_write8(fd, 1, 2, RESETOFFSET, 0xf);


}
