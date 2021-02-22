
#include <stdint.h>
#include <math.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "apcilib.h"
#define DEVICEPATH "/dev/apci/mpcie_adio16_8f_0"
#define BAR_REGISTER 1
pthread_t worker_thread;
static int terminate;
int apci;

/* Hardware registers */
#define RESETOFFSET				0x00
#define ADCBASECLOCKOFFSET		0x0C
#define ADCRATEDIVISOROFFSET	0x10
#define ADCRANGEOFFSET			0x18
#define FAFIRQTHRESHOLDOFFSET	0x20
#define ADCFIFODepthOffset		0x28
#define ADCDataRegisterOffset	0x30
#define ADCControlOffset		0x38
#define IRQENABLEOFFSET			0x40
#define ADC_CFG_MASK   0x20000
#define ADC_START_MASK 0x10000

#define RAWrunningshift 30
#define RAWdioshift 26
#define RAWtempshift 25
#define RAWmuxshift 24
#define RAWchannelshift 20
#define RAWdiffshift 19
#define RAWgainshift 16
#define RAWrunningshift 30

const uint32_t BaseADCCommand_SE = 0b1000101111111100 | ADC_CFG_MASK | ADC_START_MASK ;
const uint32_t BaseADCCommand_SE_Immediate = 0b1000100001001100 | ADC_CFG_MASK | ADC_START_MASK;

void BRD_Reset(int apci)
{
	apci_write32(apci, 1, BAR_REGISTER, RESETOFFSET, 0x1);	
}

void abort_handler(int s)
{
	printf("Caught signal %d\n",s);

	terminate = 1;
	pthread_join(worker_thread, NULL);

	/* put the card back in the power-up state */
	BRD_Reset(apci);
	exit(1);
}

double VFromRaw(uint32_t rawData)
{
	// All ranges are bipolar, all counts are 16-bit, so the 0x8000 constant is half the span of counts.
	// These numbers include the extra 2.4% margin for calibration.
	const double RangeScale[8] =
	{
		24.576 / 0x8000, // code 0
		10.24 / 0x8000, // code 1
		5.12 / 0x8000, // code 2
		2.56 / 0x8000, // code 3
		1.28 / 0x8000, // code 4
		0.64 / 0x8000, // code 5
		NAN, // code 6
		20.48 / 0x8000, // code 7
	};
	uint16_t Counts = rawData & 0xFFFF;
	uint16_t RangeCode = (rawData >> 16) & 0x7;
	// The other status bits are useful for streaming.

	return rawData & (1<<31) // if invalid
	               ? NAN
			       : RangeScale[RangeCode] * (int16_t)Counts;
}

int ParseADCRawData(uint32_t rawData, uint32_t *channel, double *volts, uint8_t *gainCode, uint16_t *digitalData, int *differential, int *temp, int *mux, int *running )
{
	int bInvalid = rawData & (1<<31);
	if (!bInvalid)
	{
		if (channel) *channel = (rawData >> RAWchannelshift) & 0x07;
		if (gainCode) *gainCode = (rawData >> RAWgainshift) & 0x07;
		if (digitalData) *digitalData = (rawData >> RAWdioshift) & 0x0F;
		if (differential) *differential = (rawData >> RAWdiffshift) & 0x01;
		if (temp) *temp = (rawData >> RAWtempshift) & 0x01;
		if (mux) *mux = (rawData >> RAWmuxshift) & 0x01;
		if (running) *running = (rawData >> RAWrunningshift) & 0x01;
		if (volts) *volts = VFromRaw(rawData);
	}
	return bInvalid;
}

/* Background thread to acquire data and queue to logger_thread */ 
void * worker_main(void *arg)
{
	int status;
	int ADCFIFODepth;
	uint32_t ADCDataRaw;
	unsigned int iChannel;
	double ADCDataV;

	printf("  Worker Thread: Polling for ADC Data\n");

	do
	{
		// Check ADC FIFO Depth
		/* In all Timed and External ADC Start modes the FIFO holds data in pairs-of-conversions
		 * so this code reads out TWO ADC conversions per ONE ADC FIFO Depth
		 */
		apci_read32(apci, 1, BAR_REGISTER, ADCFIFODepthOffset, &ADCFIFODepth);
		
		while (ADCFIFODepth > 0)
		{
			// read data, 1st conversion result
			apci_read32(apci, 1, BAR_REGISTER, ADCDataRegisterOffset, &ADCDataRaw);

			ParseADCRawData(ADCDataRaw, &iChannel, &ADCDataV, NULL, NULL, NULL, NULL, NULL, NULL);
			
			printf("%d=% f, ", iChannel, ADCDataV);

			// read data, 2nd conversion result	
			apci_read32(apci, 1, BAR_REGISTER, ADCDataRegisterOffset, &ADCDataRaw);

			ParseADCRawData(ADCDataRaw, &iChannel, &ADCDataV, NULL, NULL, NULL, NULL, NULL, NULL);		

			printf("%d=% f ", iChannel, ADCDataV);
			if (iChannel == 7) printf("\n");

			apci_read32(apci, 1, BAR_REGISTER, ADCFIFODepthOffset, &ADCFIFODepth);
		};
	}while (!terminate);
	printf("  Worker Thread: ADC Data Polling END\n");
}

// configure ADC conversion rate
void set_acquisition_rate (int fd, double *Hz)
{
	uint32_t base_clock;
	uint32_t divisor;

	apci_read32(fd, 1, BAR_REGISTER, ADCBASECLOCKOFFSET, &base_clock);
	printf("  set_acquisition_rate: base_clock (%d) / ", base_clock);

	divisor = round(base_clock / *Hz);
	*Hz = base_clock / divisor; /* actual Hz selected, based on the limitation caused by integer divisors */
	printf("divisor (%d) = ", divisor);

	apci_write32(fd, 1, BAR_REGISTER, ADCRATEDIVISOROFFSET, divisor);
}

int main (int argc, char **argv)
{
	struct sigaction sigIntHandler;

	sigIntHandler.sa_handler = abort_handler;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;
	sigaction(SIGINT, &sigIntHandler, NULL);
	sigaction(SIGABRT, &sigIntHandler, NULL);

	printf("\nmPCIe-ADIO16-8F Family ADC Sample 0\n");

	apci = open(DEVICEPATH, O_RDONLY);
	if (apci < 0)
	{
		printf("Device file could not be opened. Please ensure the APCI driver module is loaded.\n");
		exit(0);
	}

	printf("Demonstrating SOFTWARE START FOREGROUND POLLING ACQUISITION\n");

		int ch;
		uint32_t ADCFIFODepth;
		uint32_t ADCDataRaw;
		uint32_t iChannel;
		double ADCDataV;
		uint8_t ADCGainCode;
		
		BRD_Reset(apci);

		apci_write32(apci, 1, BAR_REGISTER, ADCRATEDIVISOROFFSET, 0); // setting ADC Rate Divisor to zero selects software start ADC mode
		for (ch=0; ch<8; ++ch)
		{
			apci_write32(apci, 1, 1, ADCControlOffset, BaseADCCommand_SE_Immediate | (ch << 12) );	// start one conversion
			usleep(10); // must not write to +38 faster than once every 10 microseconds in Software Start mode
		}

		apci_read32(apci, 1, BAR_REGISTER, ADCFIFODepthOffset, &ADCFIFODepth);
		printf("  ADC FIFO has %d entries\n", ADCFIFODepth);

		for (ch=0; ch < ADCFIFODepth; ++ch)	// read and display data from FIFO
		{
			apci_read32(apci, 1, BAR_REGISTER, ADCDataRegisterOffset, &ADCDataRaw);
			ParseADCRawData(ADCDataRaw, &iChannel, &ADCDataV, &ADCGainCode, NULL, NULL, NULL, NULL, NULL);		
			printf("    ADC CH %d = % f [gain code %d] [raw:%08X]\n", iChannel, ADCDataV, ADCGainCode, ADCDataRaw);
		}

		printf("Done with one scan of software-started conversions.\n\n\n");
	

	printf("Demonstrating TIMER-DRIVEN BACKGROUND-THREAD POLLING ACQUISITION\n");
		printf("Press CTRL-C to halt\nPRESS ENTER TO CONTINUE\n");
		getchar();
			
		BRD_Reset(apci);
		double Hz = 10.0;
		set_acquisition_rate(apci, &Hz);
		printf("ADC Rate: (%lf Hz)\n", Hz);

		// start collecting incoming data in background thread
		pthread_create(&worker_thread, NULL, &worker_main, NULL);
		
		// start sequence of conversions
		apci_write32(apci, 1, 1, ADCControlOffset, BaseADCCommand_SE | (7 << 12) );	

		while (!terminate)
		{
			// do other work while data is collected and displayed by the worker thread.
			// use CTRL-C to exit
		};

	printf("Sample Done."); // never prints due to CTRL-C abort
}




