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

#define DEVICEPATH "/dev/apci/mpcie_adio16_8e_1"
#define DEV2PATH "/dev/apci/mpcie_adio16_8e_0"

#define BAR_REGISTER 1
#define SAMPLE_RATE 100000.0 /* Hz */

#define LOG_FILE_NAME "samples.csv"
#define SECONDS_TO_LOG 4.0
#define AMOUNT_OF_DATA_TO_LOG (SECONDS_TO_LOG * SAMPLE_RATE)

uint8_t CHANNEL_COUNT = 16;
#define HIGH_CHANNEL (CHANNEL_COUNT-1) /* channels 0 through HIGH_CHANNEL are sampled, simultaneously, from both ADAS3022 chips */

#define NUM_CHANNELS (CHANNEL_COUNT) 

#define SAMPLES_PER_TRANSFER 0xF00  /* FIFO Almost Full IRQ Threshold value (0 < FAF <= 0xFFF */
#define BYTES_PER_SAMPLE 4
#define BYTES_PER_TRANSFER (SAMPLES_PER_TRANSFER * BYTES_PER_SAMPLE)

/* Hardware registers */
#define RESETOFFSET				0x00
#define DACOFFSET				0x04
#define BASECLOCKOFFSET			0x0C
#define DIVISOROFFSET			0x10
#define ADCRANGEOFFSET			0x18
#define FAFIRQTHRESHOLDOFFSET	0x20
#define FIFOLEVELOFFSET			0x28
#define ADCCONTROLOFFSET		0x38
#define IRQENABLEOFFSET			0x40
#define ADC_START_MASK 0x30000

/* This simple sample uses a Ring-buffer to queue data for logging to disk via a background thread */
/* It is possible for the driver and the user to have a different number of slots, but making them match is less complicated */
#define RING_BUFFER_SLOTS 4
//volatile static uint16_t ring_buffer[RING_BUFFER_SLOTS][SAMPLES_PER_TRANSFER * 2];
static uint16_t ring_buffer[RING_BUFFER_SLOTS][SAMPLES_PER_TRANSFER * 2];
static sem_t ring_sem;
volatile static int terminate;

#define ADC_SAMPLE_FIFO_DEPTH 4096
#define DMA_BUFF_SIZE BYTES_PER_TRANSFER * RING_BUFFER_SLOTS
#define NUMBER_OF_DMA_TRANSFERS ((__u32)(AMOUNT_OF_DATA_TO_LOG / SAMPLES_PER_TRANSFER))

#define mPCIe_ADIO_IRQStatusAndClearOffset (0x40)
#define bmADIO_FAFIRQStatus (1<<20)
#define bmADIO_DMADoneStatus (1<<18)
#define bmADIO_DMADoneEnable (1<<2)
#define bmADIO_ADCTRIGGERStatus (1<<16)
#define bmADIO_ADCTRIGGEREnable (1<<0)

int fd, fd2;
pthread_t logger_thread;
pthread_t worker_thread;
pthread_t logger_thread2;
pthread_t worker_thread2;

			/* diagnostic data dump function; unused */ 
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

void abort_handler(int s){
	printf("Caught signal %d\n",s);
	/* put the card back in the power-up state */
	apci_write32(fd, 1, BAR_REGISTER, RESETOFFSET, 0x1);

	terminate = 1;
	pthread_join(logger_thread, NULL);
	exit(1);
}

/* background thread to save acquired data to disk.  
 * Note this has to keep up or the current static-length ring buffer would overwrite data 
 * Launched from Worker Thread 
 */
void * log_main(void *arg)
{

    int fd = *(int *)arg;
	int ring_read_index = 0;
	int status;
	int row = 0;
	int last_channel = -1;
	int16_t counts[NUM_CHANNELS];
	int channel;
	char index = (fd & 0x0f) +0x30;

    char *name = malloc(sizeof(char) * 1024);
	sprintf(name, "samples%c.csv", index);


	FILE *out = fopen(name, "w");

	if (out == NULL)
	{
		printf("Error opening file\n");
		apci_write32(fd, 1, BAR_REGISTER, RESETOFFSET, 0x1);
		exit(1);
	}

	fprintf(out, "Row,CH0,CH1,CH2,CH3,CH4,CH5,CH6,CH7");
	if (CHANNEL_COUNT == 16) fprintf(out, ",CH8,CH9,CHA,CHB,CHC,CHD,CHE,CHF");
	fprintf(out, "\n");
	
	while (!terminate)
	{
		status = sem_wait(&ring_sem);
		if (terminate) goto endlog;

		for (int i = 0; i < SAMPLES_PER_TRANSFER ; i++)
		{
			channel = (ring_buffer[ring_read_index][i * 2 + 1] >> 4) & 0xF; // read the conversion result's channel # out of the status word

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
			counts[channel] = ring_buffer[ring_read_index][i * 2];
			counts[channel + 1] = ring_buffer[ring_read_index][i * 2 + 1];
			last_channel = channel;
		}
		ring_read_index++;
		ring_read_index %= RING_BUFFER_SLOTS;
	 };
	 endlog:
	 fclose(out);
}

/* Background thread to acquire data and queue to logger_thread */ 
void * worker_main(void *arg)
{

	int fd = *(int *)arg;
	int status;

	//map the DMA destination buffer
	void * mmap_addr = (void *) mmap(NULL, DMA_BUFF_SIZE, PROT_READ, MAP_SHARED, fd, 0);
	if (mmap_addr == NULL) 	{
		printf("  Worker Thread: mmap_addr is NULL\n");
		return NULL; // was -1
	}

	status = sem_init(&ring_sem, 0, 0);
	if (status)   {
		printf("  Worker Thread: Unable to init semaphore\n");
		return NULL; // was -1
	}
	
	pthread_create(&logger_thread, NULL, &log_main, &fd); 
	printf("  Worker Thread: launched Logging Thread\n");

	int transfer_count = 0;
	int num_slots;
	int first_slot;
	int data_discarded;
	int buffers_queued;

	do
	{
		if (0) printf("  Worker Thread: About to call apci_dma_data_ready()\n");
		fflush(stdout);
		status = apci_dma_data_ready(fd, 1, &first_slot, &num_slots, &data_discarded);      

		if (data_discarded != 0)
		{
			printf("  Worker Thread: first_slot = %d, num_slots = %d, data_discarded = %d\n", first_slot, num_slots, data_discarded);
		}

		if (num_slots == 0)
		{
			if (0) printf("  Worker Thread: No data pending; Waiting for IRQ\n");
			status = apci_wait_for_irq(fd, 1);  // thread blocking
			if (status)
			{
				printf("  Worker Thread: Error waiting for IRQ\n");
				break;
			}
			continue;
		}

		if (0) printf("  Worker Thread: data [%d slots] in slot %d\n", num_slots, first_slot);
		
		
		//if ((num_slots >0) && (first_slot + num_slots <= RING_BUFFER_SLOTS)) // J2H version
		if (first_slot + num_slots <= RING_BUFFER_SLOTS)
		{
			if (0) printf("  Worker Thread: Copying contiguous buffers from ring\n");
			memcpy(ring_buffer[first_slot], mmap_addr + (BYTES_PER_TRANSFER * first_slot), BYTES_PER_TRANSFER * num_slots);
			//memcpy(ring_buffer[0], mmap_addr + (BYTES_PER_TRANSFER * 0), BYTES_PER_TRANSFER * 1);
		}
		else
		{
			if (0) printf("  Worker Thread: Copying non-contiguous buffers from ring\n");
			memcpy(ring_buffer[first_slot],
					mmap_addr + (BYTES_PER_TRANSFER * first_slot),
					BYTES_PER_TRANSFER * (RING_BUFFER_SLOTS - first_slot));
			memcpy(ring_buffer[0],
					mmap_addr,
					BYTES_PER_TRANSFER * (num_slots - (RING_BUFFER_SLOTS - first_slot)));
		}

		__sync_synchronize();

		if (0) printf("  Worker Thread: Telling driver we've taken %d buffer%c\n", num_slots, (num_slots == 1) ? ' ':'s');
		apci_dma_data_done(fd, 1, num_slots);

		for (int i = 0; i < num_slots; i++)
		{
			sem_post(&ring_sem);
		}

		sem_getvalue(&ring_sem, &buffers_queued);
		if (buffers_queued >= RING_BUFFER_SLOTS) {
			printf("  Worker Thread: overran the ring buffer.  Saving the log was too slow. Aborting.\n");
			break;
		}
		transfer_count += num_slots;
		if (0) printf("  Worker Thread: transfer count == %d / %d\n", transfer_count, NUMBER_OF_DMA_TRANSFERS);
	}while (transfer_count < NUMBER_OF_DMA_TRANSFERS);
	printf("  Worker Thread: exiting; data acquisition complete.\n");
	terminate = 1;
}

void set_acquisition_rate (int fd, double *Hz)
{
	uint32_t base_clock;
	uint32_t divisor;

	apci_read32(fd, 1, BAR_REGISTER, BASECLOCKOFFSET, &base_clock);
	printf("  set_acquisition_rate: base_clock (%d) / ", base_clock);

	divisor = round(base_clock / *Hz);
	*Hz = base_clock / divisor; /* actual Hz selected, based on the limitation caused by integer divisors */
	printf("divisor (%d) = ", divisor);

	apci_write32(fd, 1, BAR_REGISTER, DIVISOROFFSET, divisor);
	apci_read32(fd,1,BAR_REGISTER, DIVISOROFFSET, &divisor);

	printf(" [divisor readback=%d] ", divisor);

}

/* mPCIe-ADIO16-8F Family:  ADC Data Acquisition sample (with logging to sample.csv)
 * This program acquires ADC data for a source-configurable number of seconds at a source-configurable rate
 * and logs all data into a csv file.
 * Please note the mPCIe-ADIO16-8F synchronously acquires some DIO data, but this sample discards it.
 */
int main (void)
{
	int i;
	volatile void *mmap_addr;
	int status = 0;
	double rate = SAMPLE_RATE;
	uint32_t depth_readback;
	uint32_t start_command;
	int ring_write_index = 0;
	int last_status = -1;
	struct timespec dma_delay = {0};

	struct sigaction sigIntHandler;

	sigIntHandler.sa_handler = abort_handler;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;

	 sigaction(SIGINT, &sigIntHandler, NULL);
	 sigaction(SIGABRT, &sigIntHandler, NULL);

	printf("mPCIe-ADIO16-16F Family ADC logging sample.\n");
	
	dma_delay.tv_nsec = 10;
    CHANNEL_COUNT = 8;
	fd = open(DEVICEPATH, O_RDONLY);
	//fd2 = open(DEV2PATH, O_RDONLY);
	if ((fd < 0) || (fd2<0))
	{
		printf("Device file %s could not be opened. Please ensure the APCI driver module is loaded or try sudo?.\nTrying Alternate [ %s ]...\n", DEVICEPATH, DEV2PATH);
		fd = open(DEV2PATH, O_RDONLY);
		if (fd < 0)
		{
			printf("Device file %s could not be opened. Please ensure the APCI driver module is loaded or try sudo?.\n", DEV2PATH);
			exit(0);
		} else
		{
			CHANNEL_COUNT = 8;
		}
	}
	uint32_t Version = 0;
	apci_read32(fd, 1, BAR_REGISTER, 0x68, &Version);
	printf("\nFPGA Rev %08X\n", Version);
	if (fd2) apci_read32(fd2, 1, BAR_REGISTER, 0x68, &Version);
	if (fd2) printf("FPGA Rev %08X\n", Version);
	//Setup dma ring buffer in driver
	status = apci_dma_transfer_size(fd, 1, RING_BUFFER_SLOTS, BYTES_PER_TRANSFER);
	if (fd2) status = apci_dma_transfer_size(fd2, 1, RING_BUFFER_SLOTS, BYTES_PER_TRANSFER);
	printf("Setting bytes per transfer: 0x%x\n", BYTES_PER_TRANSFER);

	if (status)  {
		printf("Error setting transfer_size\n");
		return -1;
	}

	pthread_create(&worker_thread, NULL, &worker_main, &fd);
	if (fd2) pthread_create(&worker_thread2, NULL, &worker_main, &fd2);

	//reset everything
	apci_write32(fd, 1,BAR_REGISTER, RESETOFFSET, 0x1);
	if (fd2) apci_write32(fd2, 1,BAR_REGISTER, RESETOFFSET, 0x1);
	
	//set depth of FIFO to generate IRQ
	apci_write32(fd, 1, BAR_REGISTER, FAFIRQTHRESHOLDOFFSET, SAMPLES_PER_TRANSFER);
	apci_read32(fd, 1, BAR_REGISTER, FAFIRQTHRESHOLDOFFSET, &depth_readback);
	if (fd2) apci_write32(fd2, 1, BAR_REGISTER, FAFIRQTHRESHOLDOFFSET, SAMPLES_PER_TRANSFER);
	if (fd2) apci_read32(fd2, 1, BAR_REGISTER, FAFIRQTHRESHOLDOFFSET, &depth_readback);
	printf("FIFO Almost Full (FAF) IRQ Threshold set to = 0x%x\n", depth_readback);

	set_acquisition_rate(fd, &rate);
	if (fd2) set_acquisition_rate(fd2, &rate);
	printf("ADC Rate: (%lf Hz)\n", rate);

	//set ranges
	apci_write32(fd, 1, BAR_REGISTER, ADCRANGEOFFSET, 0);
	if (fd2) apci_write32(fd2, 1, BAR_REGISTER, ADCRANGEOFFSET, 0);

	//enable ADC Trigger IRQ
	apci_write32(fd, 1, BAR_REGISTER, IRQENABLEOFFSET, bmADIO_ADCTRIGGEREnable|bmADIO_DMADoneEnable);
	if (fd2) apci_write32(fd2, 1, BAR_REGISTER, IRQENABLEOFFSET, bmADIO_ADCTRIGGEREnable|bmADIO_DMADoneEnable);

	// start_command = 0xf4ee; // differential //note: logger thread would need refactoring to handle differential well, it will currently report "0"
	start_command = 0xfcee; // single-ended
	
	start_command &= ~(7 << 12);
	start_command |= HIGH_CHANNEL << 12;
	start_command |= ADC_START_MASK;

	apci_write32(fd, 1, BAR_REGISTER, ADCCONTROLOFFSET+4, start_command);
	apci_write32(fd, 1, BAR_REGISTER, ADCCONTROLOFFSET, start_command);
	if (fd2) apci_write32(fd2, 1, BAR_REGISTER, ADCCONTROLOFFSET+4, start_command);
	if (fd2) apci_write32(fd2, 1, BAR_REGISTER, ADCCONTROLOFFSET, start_command);

	printf("start_command = 0x%05x\n", start_command);

	//diag_dump_buffer_half(mmap_addr, 0);
	
	do {	} while (! terminate);
	
	printf("Terminating\n");

	{ // wait for log data to spool to disk
		int buffers_queued;
		do
		{
			sem_getvalue(&ring_sem, &buffers_queued);
			usleep(100);
		} while (buffers_queued > 0);
	}


err_out: //Once a start has been issued to the card we need to tell it to stop before exiting
	/* put the card back in the power-up state */
	apci_write32(fd, 1, BAR_REGISTER, RESETOFFSET, 0x1);
	if (fd2) apci_write32(fd2, 1, BAR_REGISTER, RESETOFFSET, 0x1);

	terminate = 1;
	sem_post(&ring_sem);
	printf("Done acquiring %3.2f second%c. Waiting for log file to flush.\n", (SECONDS_TO_LOG), (SECONDS_TO_LOG==1)?' ':'s');
	pthread_join(logger_thread, NULL);
	if (fd2) pthread_join(logger_thread2, NULL);
	printf("Done. Data logged to %s\n", LOG_FILE_NAME);
}
