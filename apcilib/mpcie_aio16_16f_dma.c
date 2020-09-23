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

#define DEVICEPATH "/dev/apci/mPCIe_AIO16_16F_proto_0"
#define SAMPLE_RATE 40000.0 /* Hz */

#define LOG_FILE_NAME "samples.csv"
#define AMOUNT_OF_DATA_TO_LOG 1000000 /* conversions */
#define HIGH_CHANNEL 7 /* channels 0 through HIGH_CHANNEL are sampled, simultaneously, from both ADAS3022 chips */

#define NUM_CHANNELS ((HIGH_CHANNEL+1)*2)

#define SAMPLES_PER_TRANSFER 0x800  /* FIFO Almost Full IRQ Threshold value (0 < FAF <= 0xFFF */
#define BYTES_PER_SAMPLE 8
#define BYTES_PER_TRANSFER SAMPLES_PER_TRANSFER * BYTES_PER_SAMPLE

/* Hardware registers */
#define RESETOFFSET         0x00
#define IRQENABLEOFFSET     0x01
#define DACOFFSET           0x04
#define BASECLOCKOFFSET     0x0C
#define DIVISOROFFSET       0x10
#define ADCRANGEOFFSET      0x18
#define ADC2RANGEOFFSET     (ADCRANGEOFFSET + 4)
#define AFLEVELOFFSET       0x20
#define ADCSTARTSTOPOFFSET  0x38
#define ADCIMMEDIATEOFFSET  0x3A

#define ADC_SAMPLE_FIFO_DEPTH 4096
#define DMA_BUFF_SIZE ADC_SAMPLE_FIFO_DEPTH * BYTES_PER_SAMPLE  * 2 /* the buffer halves are ping-ponged by the driver, filled by DMA one half per IRQ */
#define NUMBER_OF_DMA_TRANSFERS (AMOUNT_OF_DATA_TO_LOG / SAMPLES_PER_TRANSFER)

/* This simple sample uses a Ring-buffer to queue data for logging to disk via a background thread */
#define RING_BUFFER_SLOTS 5
static uint16_t ring_buffer[RING_BUFFER_SLOTS][SAMPLES_PER_TRANSFER * 4];
static sem_t ring_sem;
static int terminate;

/* background thread to save acquired data to disk.  Note this has to keep up or the current static-length ring buffer would overwrite data */
void * log_main(void *arg)
{
  int ring_read_index = 0;
  int status;
  int row = 0;
  int last_channel = -1;
  int16_t counts[NUM_CHANNELS];
  int channel;
  FILE *out = fopen(LOG_FILE_NAME, "w");

  while (!terminate)
  {
    status = sem_wait(&ring_sem);

    for (int i = 0; i < SAMPLES_PER_TRANSFER ; i++)
    {
      channel = (ring_buffer[ring_read_index][i * 4 + 1] >> 4) & 0x7; // read the conversion result's channel # out of the status word

      // print the data accumulated into counts[channel] only after the current sample ("i") indicates the channel has wrapped
      if (channel == 0)
      {
        fprintf(out, "%d", row);
        for (int j = 0 ; j < NUM_CHANNELS ; j++)
        {
          fprintf(out, ",%d", counts[j]);
        }
        fprintf(out, "\n");
        row++;
        memset(counts, 0, sizeof(counts));
      }
      counts[channel] = ring_buffer[ring_read_index][i * 4];
      counts[channel + 8] = ring_buffer[ring_read_index][i * 4 + 2];
      last_channel = channel;
    }
    ring_read_index++;
    ring_read_index %= RING_BUFFER_SLOTS;
   };
   fclose(out);
}

void set_acquisition_rate (int fd, double *Hz)
{
  uint32_t base_clock;
  uint32_t divisor;

  apci_read32(fd, 1, 2, BASECLOCKOFFSET, &base_clock);

  divisor = round(base_clock / *Hz);
  *Hz = base_clock / divisor; /* actual Hz selected, based on the limitation caused by  integer divisors */
  printf("base_clock = %d, divisor = %d, Hz = %f\n", base_clock, divisor, *Hz);

  apci_write32(fd, 1, 2, DIVISOROFFSET, divisor);
}

  void diag_dump_buffer_half (volatile void *mmap_addr, int half)
  {
    int i;
    if (half == 1) mmap_addr += DMA_BUFF_SIZE / 2;

    for (i = 0 ; i < DMA_BUFF_SIZE / 4 ; i++)
    {
      printf("0x%x ", ((uint16_t *)mmap_addr)[i]);
      if (!(i% 16)) printf("\n");
    }
  }


int main (void)
{
	int fd;
	int i;
	volatile void *mmap_addr;
	int status = 0;
  double rate = SAMPLE_RATE;
	uint32_t depth_readback;
	uint16_t start_command;
  pthread_t logger_thread;
  int ring_write_index = 0;
  int last_status = -1;
  struct timespec dma_delay = {0};

  printf("mPCIe-AIO16-16F Family ADC logging sample.\n");

  dma_delay.tv_nsec = 10;

	fd = open(DEVICEPATH, O_RDONLY);

	if (fd < 0)	{
		printf("Device file could not be opened. Please ensure the iogen driver module is loaded.\n");
		return -1;
	}

  //allocate the ping-pong DMA destination buffer
	mmap_addr = mmap(NULL, DMA_BUFF_SIZE, PROT_READ, MAP_SHARED, fd, 0);
	if (mmap_addr == NULL) 	{
		printf("mmap_addr is NULL\n");
		return -1;
	}

  status = sem_init(&ring_sem, 0, 0);
  if (status)   {
    printf("Unable to init semaphore\n");
    return -1;
  }
  pthread_create(&logger_thread, NULL, &log_main, NULL);


  //reset everything
  apci_write8(fd, 1, 2, RESETOFFSET, 0xf);

  //set depth of FIFO to generate IRQ
  apci_write32(fd, 1, 2, AFLEVELOFFSET, SAMPLES_PER_TRANSFER);
	apci_read32(fd, 1, 2, AFLEVELOFFSET, &depth_readback);
	printf("depth_readback = 0x%x\n", depth_readback);

  apci_dma_transfer_size(fd, 1, BYTES_PER_TRANSFER);

  set_acquisition_rate(fd, &rate);

  //set ranges
  apci_write32(fd, 1, 2, ADCRANGEOFFSET, 0);
  apci_write32(fd, 1, 2, ADC2RANGEOFFSET , 0);

  //enable FAF Threshold Reached IRQ
  apci_write8(fd, 1, 2, IRQENABLEOFFSET, 0b00000001);

  //Start command
	start_command = 0xfcee;
	start_command &= ~(7 << 12);
	start_command |= HIGH_CHANNEL << 12;
  start_command |= 1;
  apci_write16(fd, 1, 2, ADCSTARTSTOPOFFSET, start_command);
	printf("start_command = 0x%04x\n", start_command);

  //diag_dump_buffer_half(mmap_addr, 0);

  for (int i = 0 ; i < NUMBER_OF_DMA_TRANSFERS ; i++)
  {
    status = apci_wait_for_irq(fd, 1); /* status indicates which half of the DMA buffer was just filled (0 or 1) */

    if (last_status == -1)
    {
      last_status = status;
      continue;
    }

    memcpy(ring_buffer[ring_write_index],
            mmap_addr + (last_status * DMA_BUFF_SIZE/2),
            BYTES_PER_TRANSFER);
    sem_post(&ring_sem);

    int buffers_queued;
    sem_getvalue(&ring_sem, &buffers_queued);
    if (buffers_queued >= RING_BUFFER_SLOTS) {
      printf("overran the ring buffer.  Saving the log was too slow. Aborting.\n");
      break;
    }

    if (status == last_status) {
      printf("DMA ping-pong repeat. Probably missed an IRQ: acquisition rate too fast. Continuing. Log is suspect.\n");
    }
    last_status = status;
    //printf("status after irq = %d, ring_write_index = %d\n", status, ring_write_index);
    ring_write_index++;
    ring_write_index = ring_write_index == RING_BUFFER_SLOTS ? 0 : ring_write_index;
  }

  terminate = 1;
  pthread_join(logger_thread, NULL);

  /* put the card back in the power-up state */
  apci_write8(fd, 1, 2, RESETOFFSET, 0xf);

  printf("Done. Data logged to %s\n", LOG_FILE_NAME);
}
