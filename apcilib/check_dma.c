/* TODO: Make sure FIFO_SIZE is correctly autodetecting */

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

#define BAR_REGISTER 1
// Note: This is the overall sample rate, sample rate of each channel is SAMPLE_RATE / CHANNEL_COUNT
//#define SAMPLE_RATE 1000000.0 /* Hz */
#define SAMPLE_RATE 10000.0 /* Hz */

#define LOG_FILE_NAME "samples.csv"
#define SECONDS_TO_LOG 120.0

uint8_t CHANNEL_COUNT = 1;            /* For single ended inputs, maximum CHANNEL_COUNT is 8 or 16, depending on model */
#define HIGH_CHANNEL (CHANNEL_COUNT-1) /* channels 0 through HIGH_CHANNEL are sampled, simultaneously, from both ADAS3022 chips */
#define NUM_CHANNELS (2 * CHANNEL_COUNT)
#define AMOUNT_OF_SAMPLES_TO_LOG (SECONDS_TO_LOG * SAMPLE_RATE * 2)

// Note: In all other ADC Start Modes, the ADC FIFO is 64-bits wide, holds two ADC conversions (+statuses) per FIFO entry,
//       therefore the FIFO thus holds 8190 conversion/status pairs.
#define FIFO_SIZE 0xF00  /* FIFO Almost Full IRQ Threshold value (0 < FAF <= 0xFFF */
#define SAMPLES_PER_FIFO_ENTRY   2
#define BYTES_PER_FIFO_ENTRY     (4 * SAMPLES_PER_FIFO_ENTRY)
#define BYTES_PER_TRANSFER       (FIFO_SIZE * BYTES_PER_FIFO_ENTRY)
#define SAMPLES_PER_TRANSFER     (FIFO_SIZE * SAMPLES_PER_FIFO_ENTRY)

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
#define ADC_START_MASK 			0x30000

/* This simple sample uses a Ring-buffer to queue data for logging to disk via a background thread */
/* It is possible for the driver and the user to have a different number of slots, but making them match is less complicated */
#define RING_BUFFER_SLOTS 4
static uint32_t ring_buffer[RING_BUFFER_SLOTS][SAMPLES_PER_TRANSFER];
static sem_t ring_sem;
static sem_t logger_sem;
static pthread_mutex_t ring_logger_mutex;
volatile static int terminate;

//#define ADC_SAMPLE_FIFO_DEPTH   4096
#define DMA_BUFF_SIZE           (BYTES_PER_TRANSFER * RING_BUFFER_SLOTS)
#define NUMBER_OF_DMA_TRANSFERS ((__u32)((AMOUNT_OF_SAMPLES_TO_LOG + SAMPLES_PER_TRANSFER - 1) / SAMPLES_PER_TRANSFER))

#define mPCIe_ADIO_IRQStatusAndClearOffset (0x40)
#define bmADIO_FAFIRQStatus (1<<20)
#define bmADIO_DMADoneStatus (1<<18)
#define bmADIO_DMADoneEnable (1<<2)
#define bmADIO_ADCTRIGGERStatus (1<<16)
#define bmADIO_ADCTRIGGEREnable (1<<0)

int fd;
pthread_t logger_thread;
pthread_t worker_thread;

// opens the only device file found in /dev/apci or presents menu to select from if more than 1 found
int OpenDevFile(); // located in apci_pnp.c

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

	terminate = 2;
	pthread_join(logger_thread, NULL);
	exit(1);
}

/* background thread to save acquired data to disk.
 * Note this has to keep up or the current static-length ring buffer would overwrite data
 * Launched from Worker Thread
 */
void * log_main(void *arg)
{
	int samples = 0;
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
		apci_write32(fd, 1, BAR_REGISTER, RESETOFFSET, 0x1);
		exit(1);
	}
	while (1)
	{
		// Output is Ch 0, Ch 8, Ch 1, Ch 9, ....
		int buffers_queued;
		sem_getvalue(&ring_sem, &buffers_queued);
		if (terminate == 1 && buffers_queued == 0) break;

		status = sem_wait(&ring_sem);

		if (terminate == 2) break;

		pthread_mutex_lock(&ring_logger_mutex);

		for (int ii = 0; ii < SAMPLES_PER_TRANSFER; ii+=NUM_CHANNELS)
		{
			++samples;
			for (int jj = 0; jj < NUM_CHANNELS; ++jj)
			{
				//(ring_buffer[ring_read_index][ii+jj] >> 20) & 0xF, // RAWchannelshift
				//(ring_buffer[ring_read_index][ii+jj] >> 19) & 0xF, // RAWdiffshift
				//(ring_buffer[ring_read_index][ii+jj] >> 16) & 0x7, // RAWgainshift

				int16_t dval = ring_buffer[ring_read_index][ii+jj] & 0xFFFF;
				fprintf(out, "%d,", dval);
				//int16_t chan = (ring_buffer[ring_read_index][ii+jj] >> 20) & 0xF;
				//fprintf(out, "%d,%d,", chan, dval);
			}
				fprintf(out, "\n");
		}

		pthread_mutex_unlock(&(ring_logger_mutex));
		sem_post(&logger_sem);

		ring_read_index++;
		ring_read_index %= RING_BUFFER_SLOTS;
	};
	 fflush(out);
				 fclose(out);
	 printf("Recorded %d samples on %d channels at rate %f\n", samples, NUM_CHANNELS, SAMPLE_RATE);
	 printf("Duration: %f\n", (CHANNEL_COUNT/SAMPLE_RATE) * samples);
}

/* Background thread to acquire data and queue to logger_thread */
void * worker_main(void *arg)
{
	int status;

	//map the DMA destination buffer
	void * mmap_addr = (void *) mmap(NULL, DMA_BUFF_SIZE, PROT_READ, MAP_SHARED, fd, 0);
	if (mmap_addr == NULL) 	{
		printf("  Worker Thread: mmap_addr is NULL\n");
		return NULL; // was -1
	}

	status = sem_init(&ring_sem, 0, 0);
	status |= sem_init(&logger_sem, 0, RING_BUFFER_SLOTS);
	status |= pthread_mutex_init(&ring_logger_mutex, NULL);
	if (status)   {
		printf("  Worker Thread: Unable to init semaphore\n");
		return NULL; // was -1
	}

	pthread_create(&logger_thread, NULL, &log_main, NULL);
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

		for (int i = 0 ; i < num_slots ; i++)
		{
			sem_wait(&logger_sem);
			pthread_mutex_lock(&ring_logger_mutex);
			memcpy(ring_buffer[(first_slot + i) % RING_BUFFER_SLOTS],
							mmap_addr + (BYTES_PER_TRANSFER * ((first_slot + i) % RING_BUFFER_SLOTS)),
							BYTES_PER_TRANSFER);
			pthread_mutex_unlock(&ring_logger_mutex);
			sem_post(&(ring_sem));

			apci_dma_data_done(fd, 1, 1);
		}
		if (1) printf("  Worker Thread: Telling driver we've taken %d buffer%c\n", num_slots, (num_slots == 1) ? ' ':'s');


		__sync_synchronize();
		transfer_count += num_slots;
		if (1) printf("  Worker Thread: transfer count == %d / %d\n", transfer_count, NUMBER_OF_DMA_TRANSFERS);
	} while (transfer_count < NUMBER_OF_DMA_TRANSFERS);
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

	terminate = 0;

	sigIntHandler.sa_handler = abort_handler;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;

	 sigaction(SIGINT, &sigIntHandler, NULL);
	 sigaction(SIGABRT, &sigIntHandler, NULL);

	printf("AxIO Family ADC logging sample.\n");
	printf("Supports  M.2-/mPCIe-AIO16-16F, M.2-/mPCIe-ADIO16-8F, M.2-/mPCIe-ADIODF16-8F, mPCIe-DAAI16-8F, and PCIe-ADIO16-16F\n");
	printf(" ... note: small things like `CHANNEL_COUNT` may need to change to work perfectly...\n");

	dma_delay.tv_nsec = 10;

	fd = OpenDevFile();
	if (fd < 0)
	{
		printf("Failed to open APCI Devicefile.\n");
		exit(0);
	} else
	{

		printf("Press ENTER to continue sample or CTRL-C to abort.\n");
		getchar();
	}

	//Setup dma ring buffer in driver
	status = apci_dma_transfer_size(fd, 1, RING_BUFFER_SLOTS, BYTES_PER_TRANSFER);
	printf("Setting bytes per transfer: 0x%x\n", BYTES_PER_TRANSFER);

	if (status)  {
		printf("Error setting transfer_size\n");
		return -1;
	}

	pthread_create(&worker_thread, NULL, &worker_main, NULL);

	//reset everything
	apci_write32(fd, 1,BAR_REGISTER, RESETOFFSET, 0x1);

	//set depth of FIFO to generate IRQ
	apci_write32(fd, 1, BAR_REGISTER, FAFIRQTHRESHOLDOFFSET, FIFO_SIZE);
	apci_read32(fd, 1, BAR_REGISTER, FAFIRQTHRESHOLDOFFSET, &depth_readback);
	printf("FIFO Almost Full (FAF) IRQ Threshold set to = 0x%x\n", depth_readback);

	set_acquisition_rate(fd, &rate);
	printf("ADC Rate: (%lf Hz)\n", rate);

	//set ranges
	apci_write32(fd, 1, BAR_REGISTER, ADCRANGEOFFSET, 0);

	//enable ADC Trigger IRQ
	apci_write32(fd, 1, BAR_REGISTER, IRQENABLEOFFSET, bmADIO_ADCTRIGGEREnable|bmADIO_DMADoneEnable);

	// start_command = 0xf4ee; // differential //note: logger thread would need refactoring to handle differential well, it will currently report "0"
	start_command = 0xfcee; // single-ended

	start_command &= ~(7 << 12);
	start_command |= HIGH_CHANNEL << 12;
	start_command |= ADC_START_MASK;

	time_t timerStart, timerEnd;
		char buffer[30];
		struct tm tm_info;

		timerStart = time(NULL);

	apci_write32(fd, 1, BAR_REGISTER, ADCCONTROLOFFSET+4, start_command);
	apci_write32(fd, 1, BAR_REGISTER, ADCCONTROLOFFSET, start_command);

	printf("start_command = 0x%05x\n", start_command);

	//diag_dump_buffer_half(mmap_addr, 0);

	do {	} while (! terminate);

	printf("Terminating\n");

	{ // wait for log data to spool to disk
		int buffers_queued;
		do
		{
			sem_getvalue(&ring_sem, &buffers_queued);
			usleep(1000);
		} while (buffers_queued > 0);
	}


err_out: //Once a start has been issued to the card we need to tell it to stop before exiting
	/* put the card back in the power-up state */
	apci_write32(fd, 1, BAR_REGISTER, RESETOFFSET, 0x1);

	terminate = 1;
	sem_post(&ring_sem);
	printf("Done acquiring %3.2f second%c. Waiting for log file to flush.\n", (SECONDS_TO_LOG), (SECONDS_TO_LOG==1)?' ':'s');
	fflush(stdout);
	pthread_join(logger_thread, NULL);

			timerEnd = time(NULL);
	time_t timeDelta = timerEnd-timerStart;

	printf("Finished recording in %ld seconds\n", timeDelta);
			localtime_r(&timerStart, &tm_info);
	strftime(buffer, 30, "Start: %Y-%m-%d %H:%M:%S", &tm_info);
				puts(buffer);
			localtime_r(&timerEnd, &tm_info);
	strftime(buffer, 30, "End:   %Y-%m-%d %H:%M:%S", &tm_info);
				puts(buffer);

	printf("Done. Data logged to %s\n", LOG_FILE_NAME);
}
