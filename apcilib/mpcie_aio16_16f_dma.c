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
#include <signal.h>
#include <stdlib.h>

#include "apcilib.h"

#define DEVICEPATH "/dev/apci/mPCIe_AIO16_16F_proto_0"
#define DEV2PATH "/dev/apci/mpcie_aio16_16f_0"
#define REGISTER_BAR 1

#define SAMPLE_RATE 62500.0 /* Hz */

#define LOG_FILE_NAME "samples.csv"
#define SECONDS_TO_LOG 2.0
#define AMOUNT_OF_DATA_TO_LOG (SECONDS_TO_LOG * SAMPLE_RATE)
#define HIGH_CHANNEL 7 /* channels 0 through HIGH_CHANNEL are sampled, simultaneously, from both ADAS3022 chips */

#define NUM_CHANNELS ((HIGH_CHANNEL+1)*2) /* conversions (on TWO channels, simultaneously) */

#define SAMPLES_PER_TRANSFER 0xf00  /* FIFO Almost Full IRQ Threshold value (0 < FAF <= 0xFFF */
#define BYTES_PER_SAMPLE 8
#define BYTES_PER_TRANSFER (SAMPLES_PER_TRANSFER * BYTES_PER_SAMPLE)

/* Hardware registers */
#define RESETOFFSET         0x00
#define IRQENABLEOFFSET     0x01
#define DACOFFSET           0x04
#define BASECLOCKOFFSET     0x0C
#define DIVISOROFFSET       0x10
#define ADCRANGEOFFSET      0x18
#define ADC2RANGEOFFSET     (ADCRANGEOFFSET + 4)
#define FAFIRQTHRESHOLDOFFSET       0x20
#define ADCSTARTSTOPOFFSET  0x38
#define ADCIMMEDIATEOFFSET  0x3A

/* This simple sample uses a Ring-buffer to queue data for logging to disk via a background thread */
/* It is possible for the driver and the user to have a different number of slots, but making them match is less complicated */
#define RING_BUFFER_SLOTS 10
static uint16_t ring_buffer[RING_BUFFER_SLOTS][SAMPLES_PER_TRANSFER * 4];
static sem_t ring_sem;
static int terminate;

#define ADC_SAMPLE_FIFO_DEPTH 4096
#define DMA_BUFF_SIZE BYTES_PER_TRANSFER * RING_BUFFER_SLOTS
#define NUMBER_OF_DMA_TRANSFERS (AMOUNT_OF_DATA_TO_LOG / SAMPLES_PER_TRANSFER)

int fd;
pthread_t logger_thread;

void abort_handler(int s){
  printf("Caught signal %d\n",s);
  /* put the card back in the power-up state */
  apci_write8(fd, 1, REGISTER_BAR, RESETOFFSET, 0xf);

  terminate = 1;
  pthread_join(logger_thread, NULL);
  exit(1);
}

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

  if (out == NULL)
  {
    printf("Error opening file\n");
    apci_write8(fd, 1, REGISTER_BAR, RESETOFFSET, 0xf);
    exit(1);
  }

  while (!terminate)
  {
    status = sem_wait(&ring_sem);
    if (terminate) goto endlog;

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
   endlog:
   fclose(out);
}

void set_acquisition_rate (int fd, double *Hz)
{
  uint32_t base_clock;
  uint32_t divisor;

  apci_read32(fd, 1, REGISTER_BAR, BASECLOCKOFFSET, &base_clock);

  divisor = round(base_clock / *Hz);
  *Hz = base_clock / divisor; /* actual Hz selected, based on the limitation caused by  integer divisors */
  printf("base_clock = %d, divisor = %d, Hz = %f\n", base_clock, divisor, *Hz);

  apci_write32(fd, 1, REGISTER_BAR, DIVISOROFFSET, divisor);
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
	int i;
	void *mmap_addr;
	int status = 0;
  double rate = SAMPLE_RATE;
	uint32_t depth_readback;
	uint16_t start_command;
  int ring_write_index = 0;
  int last_status = -1;
  struct timespec dma_delay = {0};

  struct sigaction sigIntHandler;

  sigIntHandler.sa_handler = abort_handler;
  sigemptyset(&sigIntHandler.sa_mask);
  sigIntHandler.sa_flags = 0;

   sigaction(SIGINT, &sigIntHandler, NULL);
   sigaction(SIGABRT, &sigIntHandler, NULL);

  printf("mPCIe-AIO16-16F Family ADC logging sample.\n");

  dma_delay.tv_nsec = 10;

	fd = open(DEVICEPATH, O_RDONLY);
	if (fd < 0)
	{
		printf("Device file %s could not be opened. Please ensure the APCI driver module is loaded or try sudo?.\nTrying Alternate [ %s ]...\n", DEVICEPATH, DEV2PATH);
		fd = open(DEV2PATH, O_RDONLY);
		if (fd < 0)
		{
			printf("Device file %s could not be opened. Please ensure the APCI driver module is loaded or try sudo?.\n", DEV2PATH);
			exit(0);
		} 
}

  //Setup dma ring buffer in driver
  status = apci_dma_transfer_size(fd, 1, RING_BUFFER_SLOTS, BYTES_PER_TRANSFER);
  printf("Setting bytes per transfer: 0x%x\n", BYTES_PER_TRANSFER);

  if (status)  {
    printf("Error setting transfer_size\n");
    return -1;
  }

  //map the DMA destination buffer
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
  apci_write8(fd, 1, REGISTER_BAR, RESETOFFSET, 0xf);

  //set depth of FIFO to generate IRQ
  apci_write32(fd, 1, REGISTER_BAR, FAFIRQTHRESHOLDOFFSET, SAMPLES_PER_TRANSFER);
	apci_read32(fd, 1, REGISTER_BAR, FAFIRQTHRESHOLDOFFSET, &depth_readback);
	printf("depth_readback = 0x%x\n", depth_readback);

  //set_acquisition_rate(fd, &rate);
  uint32_t base_clock;
  uint32_t divisor;

  apci_read32(fd, 1, REGISTER_BAR, BASECLOCKOFFSET, &base_clock);

  printf("base_clock=%d\n", base_clock);

  divisor = round(base_clock / rate);
  printf("divisor = %d\n", divisor);
  rate = base_clock / divisor; /* actual Hz selected, based on the limitation caused by  integer divisors */
  printf("base_clock = %d, divisor = %d, Hz = %f\n", base_clock, divisor, rate);

  apci_write32(fd, 1, REGISTER_BAR, DIVISOROFFSET, divisor);
  //set ranges
  apci_write32(fd, 1, REGISTER_BAR, ADCRANGEOFFSET, 0);
  apci_write32(fd, 1, REGISTER_BAR, ADC2RANGEOFFSET , 0);

  //enable FAF Threshold Reached IRQ
  apci_write8(fd, 1, REGISTER_BAR, IRQENABLEOFFSET, 0b00000001);

  //Start command
	// start_command = 0xf4ee; // differential //note: logger thread would need refactoring to handle differential well, it will currently report "0"
  start_command = 0xfcee; // single-ended
  
	start_command &= ~(7 << 12);
	start_command |= HIGH_CHANNEL << 12;
  start_command |= 1;
  apci_write16(fd, 1, REGISTER_BAR, ADCSTARTSTOPOFFSET, start_command);
	printf("start_command = 0x%04x\n", start_command);

  //diag_dump_buffer_half(mmap_addr, 0);

  {
    int transfer_count = 0;
    int num_slots;
    int first_slot;
    int data_discarded;
    int buffers_queued;
    do
    {
      status = apci_dma_data_ready(fd, 1, &first_slot, &num_slots, &data_discarded);

      if (data_discarded != 0)
      {
        printf("first_slot = %d, num_slots = %d, data_discarded = %d\n", first_slot, num_slots, data_discarded);
      }
      if (num_slots == 0)
      {
        status = apci_wait_for_irq(fd, 1);
        if (status)
        {
          printf("Error waiting for IRQ\n");
          break;
        }
        continue;
      }

      fflush(stdout);

      // if (first_slot + num_slots <= RING_BUFFER_SLOTS) --Jay's code
      if ((num_slots >0) && (first_slot + num_slots <= RING_BUFFER_SLOTS))
      {
        memcpy(ring_buffer[first_slot],
              mmap_addr + (BYTES_PER_TRANSFER * first_slot),
              BYTES_PER_TRANSFER * num_slots);
      }
      else
      {
        memcpy(ring_buffer[first_slot],
            mmap_addr + (BYTES_PER_TRANSFER * first_slot),
            BYTES_PER_TRANSFER * (RING_BUFFER_SLOTS - first_slot));
        memcpy(ring_buffer[0],
            mmap_addr,
            BYTES_PER_TRANSFER * (num_slots - (RING_BUFFER_SLOTS - first_slot)));
      }
      __sync_synchronize();
      // printf("Telling driver we've taken %d buffer%c\n", num_slots, (num_slots == 1) ? ' ':'s');
      apci_dma_data_done(fd, 1, num_slots);


      for (int i = 0; i < num_slots; i++)
      {
        sem_post(&ring_sem);
      }

      sem_getvalue(&ring_sem, &buffers_queued);
      if (buffers_queued >= RING_BUFFER_SLOTS) {
        printf("overran the ring buffer.  Saving the log was too slow. Aborting.\n");
        break;
      }
      transfer_count += num_slots;

    }while (transfer_count < NUMBER_OF_DMA_TRANSFERS);
  }

  {
    int buffers_queued;
    do
    {
      sem_getvalue(&ring_sem, &buffers_queued);
      usleep(100);
    } while (buffers_queued > 0);
  }


err_out: //Once a start has been issued to the card we need to tell it to stop before exiting
  /* put the card back in the power-up state */
  apci_write8(fd, 1, REGISTER_BAR, RESETOFFSET, 0xf);

  terminate = 1;
  sem_post(&ring_sem);
  printf("Done acquiring %3.2f second%c. Waiting for log file to flush.\n", (SECONDS_TO_LOG), (SECONDS_TO_LOG==1)?' ':'s');
  pthread_join(logger_thread, NULL);

  printf("Done. Data logged to %s\n", LOG_FILE_NAME);
}
