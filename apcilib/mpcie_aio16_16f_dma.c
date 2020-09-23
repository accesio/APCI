#include <stdio.h>
#include <string.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdint.h>

#include <sys/mman.h>

#include <unistd.h>

#include <math.h>

#include <semaphore.h>
#include <pthread.h>

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


#define SAMPLE_RATE 60000.0
#define HIGH_CHANNEL 7
#define SAMPLES_PER_TRANSFER 0xf00
#define BYTES_PER_TRANSFER SAMPLES_PER_TRANSFER * 8
#define NUM_CHANNELS 16




#define RING_BUFFER_SLOTS 5
static uint16_t ring_buffer[RING_BUFFER_SLOTS][SAMPLES_PER_TRANSFER * 4];
static sem_t ring_sem;
static int terminate;



void * log_main(void *arg)
{
  int ring_index = 0;
  int status;
  int row = 0;
  int last_channel = -1;
  uint16_t counts[NUM_CHANNELS];
  int channel;
  FILE *out = fopen("samples.csv", "w");
  while (!terminate)
  {
    status = sem_wait(&ring_sem);
    printf("status after sem_wait = %d, ring_index = %d\n", status, ring_index);

    for (int i = 0; i < SAMPLES_PER_TRANSFER ; i++)
    {
      channel = (ring_buffer[ring_index][i * 4 + 1] >> 4) & 0x7;
      if (channel == 0)
      {
        fprintf(out, "%d", row);
        for (int j = 0 ; j < NUM_CHANNELS ; j++)
        {
          fprintf(out, ",0x%x", counts[j]);
        }
        fprintf(out, "\n");
        row++;
        memset(counts, 0, sizeof(counts));
      }
      counts[channel] = ring_buffer[ring_index][i * 4];
      counts[channel + 8] = ring_buffer[ring_index][i * 4 + 2];
      last_channel = channel;
    }
    ring_index++;
    ring_index = ring_index == RING_BUFFER_SLOTS ? 0 : ring_index;
   };

}

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
  pthread_t logger_thread;
  int ring_index = 0;
  int last_status = -1;
  struct timespec dma_delay = {0};

  dma_delay.tv_nsec = 10;


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

  status = sem_init(&ring_sem, 0, 0);

  if (status)
  {
    printf("Unable to init semaphore\n");
    return -1;
  }
  pthread_create(&logger_thread, NULL, &log_main, NULL);


  //reset everything
  apci_write8(fd, 1, 2, RESETOFFSET, 0xf);
	sleep(1);

  //set depth of FIFO to generate IRQ
  apci_write32(fd, 1, 2, AFLEVELOFFSET, SAMPLES_PER_TRANSFER);
	apci_read32(fd, 1, 2, AFLEVELOFFSET, &depth_readback);
	printf("depth_readback = 0x%x\n", depth_readback);

  apci_dma_transfer_size(fd, 1, BYTES_PER_TRANSFER);

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

  //dump_buffer_half(mmap_addr, 0);

  for (int i = 0 ; i < 10 ; i++)
  {
    status = apci_wait_for_irq(fd, 1);
    //nanosleep(&dma_delay, NULL);
    memcpy(ring_buffer[ring_index],
            mmap_addr + (status * DMA_BUFF_SIZE/2),
            BYTES_PER_TRANSFER);
    sem_post(&ring_sem);

    if (status == last_status)
    {
      printf("Status repeat. Probably missed an IRQ\n");
    }
    last_status = status;
    //printf("status after irq = %d, ring_index = %d\n", status, ring_index);
    ring_index++;
    ring_index = ring_index == RING_BUFFER_SLOTS ? 0 : ring_index;
  }

  terminate = 1;

  //disable IRQ
  apci_write8(fd, 1, 2, IRQENABLEOFFSET, 0);
  //clear IRQs (tried with and without this)
  //apci_write8(fd, 1, 2, 0x2, 0xff);
  apci_write8(fd, 1, 2, RESETOFFSET, 0xf);

  pthread_join(logger_thread, NULL);


}
