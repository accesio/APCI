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

#define DEVICEPATH "/dev/apci/pcie_adio16_16fds_0"

#define BAR_REGISTER 1
#define SAMPLE_RATE 10000.0 /* Hz */

#define LOG_FILE_NAME "samples.csv"
#define SECONDS_TO_LOG 2.0
#define AMOUNT_OF_DATA_TO_LOG (SECONDS_TO_LOG * SAMPLE_RATE)

uint8_t CHANNEL_COUNT = 8;
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

#define RAWinvalidshift 15
#define RAWrunningshift 14
#define RAWdioshift     10
#define RAWtempshift     9
#define RAWmuxshift      8
#define RAWchannelshift  4
#define RAWdiffshift     3
#define RAWgainshift     0

/* This simple sample uses a Ring-buffer to queue data for logging to disk via a background thread */
/* It is possible for the driver and the user to have a different number of slots, but making them match is less complicated */
#define RING_BUFFER_SLOTS 4

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

// All ranges are bipolar, all counts are 16-bit, so the 0x8000 constant is half the span of counts.
// These numbers include the extra 2.4% margin for calibration (these are uncalibrated).
// The other status bits are useful for streaming.
const double RangeScale[8] =
{
	24.576 / 0x8000, // code 0
	10.24 / 0x8000, // code 1
	5.12 / 0x8000, // code 2
	2.56 / 0x8000, // code 3
	1.28 / 0x8000, // code 4
	0.64 / 0x8000, // code 5
	NAN, // code 6 is "Invalid" according to ADAS3022 chip spec
	20.48 / 0x8000, // code 7
};

int fd, save_chan;
pthread_t logger_thread;
pthread_t worker_thread;

short int count_value_to_dec(int16_t count)
{
    #define SIGN_MASK 0x8000

    if((count & SIGN_MASK) == 0)
    {
        return count;
    }
    else
    {
        return -(~count + 1);
    }
} /* count_value_to_dec */

/* diagnostic data dump function; unused */
void diag_dump_buffer_half (volatile void *mmap_addr, int half)
{
	int i;
	if (half == 1) mmap_addr += DMA_BUFF_SIZE / 2;

	printf("[00000] ");

	for (i = 0 ; i < DMA_BUFF_SIZE / 4 ; i++)
	{
	    uint16_t value = ((uint16_t *)mmap_addr)[i];

	    if(i & 0x01)
	    {
            int invalid = (value >> RAWinvalidshift) & 0x1;
            int running = (value >> RAWrunningshift) & 0x1;
            int channel = (value >> RAWchannelshift) & 0XF;
    		printf("%c%c%d ", invalid ? 'I' : 'V',
    		                  running ? 'R' : 'S', channel);
		}
		else
		{
    		printf("%+06d ", count_value_to_dec(value));
		}

		if ((i & 0xF) == 0xF) printf("\n[%05d] ", i + 1);
	}
}

char *status_descrition(int16_t status)
{
    static char status_desc[64];

	int invalid = (status >> RAWinvalidshift) & 0x1;
	int running = (status >> RAWrunningshift) & 0x1;
	int tempera = (status >> RAWtempshift) & 0x1;
	int auximux = (status >> RAWmuxshift) & 0x1;
    int channel = (status >> RAWchannelshift) & 0XF;
    int differe = (status >> RAWdiffshift) & 0X1;
    int gaincod = (status >> RAWgainshift) & 0X7;

    sprintf (status_desc, "I[%d]R[%d]T[%d]M[%d]C[%d]D[%d]G[%d]",
             invalid, running, tempera, auximux, channel, differe, gaincod);

    return status_desc;
}

double count_value(int16_t count, __u16 status)
{
	int invalid = (status >> RAWinvalidshift) & 0x1;
    int gaincod = (status >> RAWgainshift) & 0X7;

    double value = (invalid ? NAN : RangeScale[gaincod] * count);

    return value;
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
	int ret;
	int row = 0;
	__u16 count, flags;
	__u16 counts[2 * NUM_CHANNELS], status[2 * NUM_CHANNELS];
	int running, channel;
	char index = (fd & 0x0f) + 0x30;
    char dt_str[64];

    time_t t;
    struct tm *tm;

    t = time(NULL);
    tm = localtime(&t);

    sprintf(dt_str, "%d-%02d-%02d %02d:%02d:%02d",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec);

    char *name = malloc(sizeof(char) * 1024);
	sprintf(name, "samples%d_%s.txt", save_chan, dt_str);

	FILE *out = fopen(name, "w");

	if (out == NULL)
	{
		printf("Error opening file\n");
		apci_write32(fd, 1, BAR_REGISTER, RESETOFFSET, 0x1);
		exit(1);
	}

	//fprintf(out, "Row,CH0");
	//fprintf(out, "\n");

	while (!terminate)
	{
		ret = sem_wait(&ring_sem);
		if (terminate) goto endlog;

		for (int i = 0; i < SAMPLES_PER_TRANSFER ; i++)
		{
		    count = ring_buffer[ring_read_index][i * 2];
		    flags = ring_buffer[ring_read_index][i * 2 + 1];

            running = (flags >> RAWrunningshift) & 0x1;
			channel = (flags >> RAWchannelshift) & 0xF;
			channel = (channel >> 1);

			counts[channel] = count;
			status[channel] = flags;

			if ((channel == save_chan) && (running == 1))
			{
				fprintf(out, "%06d", row);

#if 1
    			fprintf(out, " %8.6f", count_value(counts[channel], status[channel]));
#else
    			fprintf(out, " %04x [%s] %d %8.6f", counts[channel],
    			        status_descrition(status[channel]),
    			        count_value_to_dec(counts[channel]),
    			        count_value(counts[channel], status[channel]));
#endif

				fprintf(out, "\n");

				row++;

				memset(counts, 0, sizeof(counts));
				memset(status, 0, sizeof(status));
			}
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
			if (1) printf("  Worker Thread: No data pending; Waiting for IRQ\n");
			status = apci_wait_for_irq(fd, 1);  // thread blocking
			if (status)
			{
				printf("  Worker Thread: Error waiting for IRQ\n");
				break;
			}
			continue;
		}

		if (1) printf("  Worker Thread: data [%d slots] in slot %d\n", num_slots, first_slot);


		//if ((num_slots >0) && (first_slot + num_slots <= RING_BUFFER_SLOTS)) // J2H version
		if (first_slot + num_slots <= RING_BUFFER_SLOTS)
		{
			if (1) printf("  Worker Thread: Copying contiguous buffers from ring\n");

            //diag_dump_buffer_half(mmap_addr, 0);

			memcpy(ring_buffer[first_slot], mmap_addr + (BYTES_PER_TRANSFER * first_slot), BYTES_PER_TRANSFER * num_slots);
		}
		else
		{
			if (1) printf("  Worker Thread: Copying non-contiguous buffers from ring\n");
			memcpy(ring_buffer[first_slot],
					mmap_addr + (BYTES_PER_TRANSFER * first_slot),
					BYTES_PER_TRANSFER * (RING_BUFFER_SLOTS - first_slot));
			memcpy(ring_buffer[0],
					mmap_addr,
					BYTES_PER_TRANSFER * (num_slots - (RING_BUFFER_SLOTS - first_slot)));
		}

		__sync_synchronize();

		if (1) printf("\n  Worker Thread: Telling driver we've taken %d buffer%c\n", num_slots, (num_slots == 1) ? ' ':'s');
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
		if (1) printf("  Worker Thread: transfer count == %d / %d\n", transfer_count, NUMBER_OF_DMA_TRANSFERS);
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

    divisor = divisor / CHANNEL_COUNT;
    printf(" [intrascan conversion divisor = %d Hz] ", divisor);

    apci_write32(fd, 1, BAR_REGISTER, DIVISOROFFSET + 4, divisor);
    apci_read32(fd, 1, BAR_REGISTER, DIVISOROFFSET + 4, &divisor);

    printf(" [intrascan divisor readback = %d] ", divisor);
}

void set_gain_code (int fd, int code)
{
    int i;
	__u32 gain1, gain2;

    apci_read32(fd, 1, BAR_REGISTER, ADCRANGEOFFSET, &gain1);
    apci_read32(fd, 1, BAR_REGISTER, ADCRANGEOFFSET + 4, &gain2);

    printf("\nPrev Gain Code: [%08x][%08x]\n", gain1, gain2);

    for(i = 0; i < 8; ++i)
    {
        gain1 |= (code << (i * 4));
        gain2 |= (code << (i * 4));
    }

    apci_write32(fd, 1, BAR_REGISTER, ADCRANGEOFFSET, gain1);
    apci_write32(fd, 1, BAR_REGISTER, ADCRANGEOFFSET + 4, gain2);

    apci_read32(fd, 1, BAR_REGISTER, ADCRANGEOFFSET, &gain1);
    apci_read32(fd, 1, BAR_REGISTER, ADCRANGEOFFSET + 4, &gain2);

    printf("Current Gain Code: [%08x][%08x]\n", gain1, gain2);
}

int main (int argc, char *argv[])
{
	int i;
	volatile void *mmap_addr;
	int status = 0;
	double rate = SAMPLE_RATE;
	uint32_t depth_readback;
	uint32_t start_command, read_command1, read_command2;
	int ring_write_index = 0;
	int last_status = -1;
	struct timespec dma_delay = {0};

	struct sigaction sigIntHandler;

	sigIntHandler.sa_handler = abort_handler;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;

	 sigaction(SIGINT, &sigIntHandler, NULL);
	 sigaction(SIGABRT, &sigIntHandler, NULL);

	printf("PCIe-ADIO12-16F Family ADC logging sample.\n");
    printf("SAMPLES_PER_TRANSFER[%d]BYTES_PER_SAMPLE[%d]BYTES_PER_TRANSFER[%d]\n",
           SAMPLES_PER_TRANSFER, BYTES_PER_SAMPLE, BYTES_PER_TRANSFER);
	printf("ring_buffer[%ld]\n", sizeof(ring_buffer));

	save_chan = 0;
	if (argc == 2)
	{
	    save_chan = atoi (argv[1]);
	}

	printf("Save channel: %d\n", save_chan);

	dma_delay.tv_nsec = 10;

	fd = open(DEVICEPATH, O_RDONLY);
	if (fd < 0)
	{
		printf("Device file %s could not be opened. Please ensure the APCI driver module is loaded or try sudo?.\n", DEVICEPATH);
		return -1;
	}

	uint32_t Version = 0;
	apci_read32(fd, 1, BAR_REGISTER, 0x68, &Version);

	printf("\nFPGA Rev %08X\n", Version);

	//Setup dma ring buffer in driver
	status = apci_dma_transfer_size(fd, 1, RING_BUFFER_SLOTS, BYTES_PER_TRANSFER);

	if (status)  {
		printf("Error setting transfer_size\n");
		return -1;
	}

for(int i = 0; i < 5; i++)
{
	pthread_create(&worker_thread, NULL, &worker_main, &fd);

	//reset everything
	apci_write32(fd, 1,BAR_REGISTER, RESETOFFSET, 0x1);

	//set depth of FIFO to generate IRQ
	apci_write32(fd, 1, BAR_REGISTER, FAFIRQTHRESHOLDOFFSET, SAMPLES_PER_TRANSFER);
	apci_read32(fd, 1, BAR_REGISTER, FAFIRQTHRESHOLDOFFSET, &depth_readback);

	printf("FIFO Almost Full (FAF) IRQ Threshold set to = 0x%x\n", depth_readback);

	set_acquisition_rate(fd, &rate);
	set_gain_code (fd, 4);

	printf("ADC Rate: (%lf Hz)\n", rate);

	//enable ADC Trigger IRQ
	apci_write32(fd, 1, BAR_REGISTER, IRQENABLEOFFSET, bmADIO_ADCTRIGGEREnable|bmADIO_DMADoneEnable);

	start_command = 0x7f4ee; // differential //note: logger thread would need refactoring to handle differential well, it will currently report "0"
	//start_command = 0xfcee; // single-ended

	start_command &= ~(7 << 12);
	start_command |= (HIGH_CHANNEL & 0x06) << 12;
	start_command |= ADC_START_MASK;

	//apci_write32(fd, 1, BAR_REGISTER, ADCCONTROLOFFSET+4, start_command);
	apci_write32(fd, 1, BAR_REGISTER, ADCCONTROLOFFSET, start_command);

	printf("start_command = 0x%05x\n", start_command);

	apci_read32(fd, 1, BAR_REGISTER, ADCCONTROLOFFSET, &read_command1);
	apci_read32(fd, 1, BAR_REGISTER, ADCCONTROLOFFSET+4, &read_command2);

	printf("read_command1 = 0x%05x\n", read_command1);
	printf("read_command2 = 0x%05x\n", read_command2);

	//diag_dump_buffer_half(mmap_addr, 0);

	do {	} while (! terminate);

	apci_read32(fd, 1, BAR_REGISTER, ADCCONTROLOFFSET, &start_command);

	start_command &= ~ADC_START_MASK;

	apci_write32(fd, 1, BAR_REGISTER, ADCCONTROLOFFSET, start_command);

	printf("Terminating\n");

	{ // wait for log data to spool to disk
		int buffers_queued;
		do
		{
			sem_getvalue(&ring_sem, &buffers_queued);
			usleep(100);
		} while (buffers_queued > 0);
	}

	printf("Waiting\n");
    sleep(5);

	terminate = 0;
}

err_out: //Once a start has been issued to the card we need to tell it to stop before exiting
	/* put the card back in the power-up state */
	apci_write32(fd, 1, BAR_REGISTER, RESETOFFSET, 0x1);

	terminate = 1;
	sem_post(&ring_sem);
	printf("Done acquiring %3.2f second%c. Waiting for log file to flush.\n", (SECONDS_TO_LOG), (SECONDS_TO_LOG==1)?' ':'s');
	pthread_join(logger_thread, NULL);

	printf("Done. Data logged to %s\n", LOG_FILE_NAME);
}
