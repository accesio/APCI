
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
#define DEVICEPATH "/dev/apci/mpcie_aio16_16f_0"
#define DEV2PATH "/dev/apci/mpcie_adio16_8f_0"

#define BAR_REGISTER 2
pthread_t worker_thread;
static int terminate;
int apci;

/* Hardware register offsets */
#define RESETOFFSET				0x00
#define ADCBASECLOCKOFFSET		0x0C
#define ADCRATEDIVISOROFFSET	0x10
#define ADCRANGEOFFSET			0x18
#define FAFIRQTHRESHOLDOFFSET	0x20
#define ADCFIFODepthOffset		0x28
#define ADCDataRegisterOffset	0x30
#define ADCControlOffset		0x38
#define IRQENABLEOFFSET			0x40

/* ADC control register bit masks */
#define ADC_RESERVED_MASK		0xFFF88405 // CFG, RSV, REFEN, CPHA are reserved bits
#define ADC_CFG_MASK   			0x00028000
#define ADC_START_MASK 			0x00010000
#define ADC_CHANNEL_MASK		0x00007000
#define ADC_CHANNEL_SHIFT		12
#define ADC_NOT_DIFF_MASK		0x00000800
#define ADC_GAIN_MASK			0x00000380
#define ADC_GAIN_SHIFT			7
#define ADC_NOT_AUX_MASK		0x00000040
#define ADC_ADVANCED_SEQ_MASK	0x00000020
#define ADC_NOT_TEMP_MASK		0x00000008
#define ADC_NOT_FAST_MASK		0x00000002 // clear this bit for speeds > 900kHz

#define RAWrunningshift 30
#define RAWdioshift 26
#define RAWtempshift 25
#define RAWmuxshift 24
#define RAWchannelshift 20
#define RAWdiffshift 19
#define RAWgainshift 16
#define RAWrunningshift 30
uint8_t CHANNEL_COUNT = 16;

const uint32_t BaseADCCommand_SE = 0b1000101111111100 | ADC_CFG_MASK | ADC_START_MASK ;
//const uint32_t BaseADCCommand_SE_Immediate = 0b1000100001001100 | ADC_CFG_MASK | ADC_START_MASK;
const uint32_t BaseADCCommand_SE_Immediate = 0x3884A;

void BRD_Reset(int apci); // forward reference

				void abort_handler(int s)
				{
					printf("\n\nCaught signal %d\n",s);

					terminate = 1;
					pthread_join(worker_thread, NULL);

					/* put the card back in the power-up state */
					BRD_Reset(apci);
					exit(1);
				}

	/*
	Assemble an ADC control register command uint32 piecemeal
	all params except channel_lastchannel are bools, 0==false
	  channel is between 0 and 7 inclusive, 
	  channel is either "the one channel to acquire" (when bSequencedMode is false)
	  or channel is "the last channel in the sequence to acquire" (when bSequencedMode is true)
	*/
	uint32_t ADC_BuildControlValue(int bStartADC, int channel_lastchannel, int bDifferential, int gainCode, int bAuxMux, int bSequencedMode, int bTemp, int bFast)
	{
		uint32_t controlValue = 0x00000000;
		controlValue |= ADC_CFG_MASK;
		if (bStartADC) 
			controlValue |= ADC_START_MASK;
		if ((channel_lastchannel < 8) && (channel_lastchannel >= 0)) 
			controlValue |= channel_lastchannel << ADC_CHANNEL_SHIFT;
		else return -1;	// invalid channel / last_channel
		if (! bDifferential) 
			controlValue |= ADC_NOT_DIFF_MASK;
		if ((gainCode < 8) && (gainCode >= 0)) 
			controlValue |= gainCode << ADC_GAIN_SHIFT;
		else return -2;	// invalid gain code
		if (bSequencedMode)
		{
			controlValue |= ADC_ADVANCED_SEQ_MASK;
			if (! bAuxMux) 
				controlValue |= ADC_NOT_AUX_MASK;
			if (! bTemp)
				controlValue |= ADC_NOT_TEMP_MASK;
		}
		else // "On Demand Conversion Mode" uses the AUX and TEMP bits differently:
		{
			if (bAuxMux && bTemp) return -3;	// invalid to select both Aux and Temp inputs in Immediate mode (On Demand Conversion Mode)
			if (bAuxMux || bTemp) 
				controlValue = controlValue & ~ADC_NOT_AUX_MASK;
			if (!bTemp)
				controlValue |= ADC_NOT_TEMP_MASK; // select NOT temp thus AUX
		}

		if (! bFast) 
			controlValue |= ADC_NOT_FAST_MASK;
		return controlValue;
	}

void BRD_Reset(int apci)
{
	apci_write32(apci, 1, BAR_REGISTER, RESETOFFSET, 0x1);	
}


int ParseADCRawData(uint32_t rawData, uint32_t *channel, double *volts, uint8_t *gainCode, uint16_t *digitalData, int *differential, int *bTemp, int *bAux, int *running )
{
	int bInvalid = rawData & (1<<31);
	if (!bInvalid)
	{
		if (channel) *channel = (rawData >> RAWchannelshift) & 0x0F;
		if (gainCode) *gainCode = (rawData >> RAWgainshift) & 0x07;
		if (digitalData) *digitalData = (rawData >> RAWdioshift) & 0x0F;
		if (differential) *differential = (rawData >> RAWdiffshift) & 0x01;
		if (bTemp) *bTemp = (rawData >> RAWtempshift) & 0x01 ? 0:1;
		if (bAux) *bAux = (rawData >> RAWmuxshift) & 0x01 ? 0:1;
		if (running) *running = (rawData >> RAWrunningshift) & 0x01;

		if (volts) 
		{
			int16_t Counts = rawData & 0xFFFF;
			if ((bTemp) && (*bTemp))
			{
				/*
					ADC Temp sensor is nonlinear but roughly:
						3800 counts is -45C
						5500 counts is +85C
					thus C = (counts - 3800) / (5500 - 3800) * (85--45) - 45.0;
				*/

				*volts = ((Counts * 1.0) - 3800.0) / (5500.0 - 3800.0) * (85.0 - -45.0) - 45.0;
				*channel &= 8; // mask out the actual channel bits as they don't apply if the reading was temperature input
			}
			else
			{
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
					NAN, // code 6
					20.48 / 0x8000, // code 7
				};

				uint16_t RangeCode = (rawData >> 16) & 0x7;
				*volts = rawData & (1<<31) // if invalid
					? NAN
					: RangeScale[RangeCode] * Counts;

			}
		}
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
	int bTemp, bAux;

	printf("  Worker Thread: Polling for ADC Data\n  ");

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

			ParseADCRawData(ADCDataRaw, &iChannel, &ADCDataV, NULL, NULL, NULL, &bTemp, &bAux, NULL);
			if ((iChannel == 0) && (!bTemp)) printf("\n  ");

			if (bTemp)
				printf("ADC %d Temp =% 5.1fC [%08x], ",iChannel?1:0, ADCDataV, ADCDataRaw);
			else
			if (bAux)
				printf("AUX %d=% 5.1f [%08x], ",iChannel?1:0, ADCDataV, ADCDataRaw);
			else
				printf("%d=% 10.6f, ", iChannel, ADCDataV);

			// read data, 2nd conversion result	
			apci_read32(apci, 1, BAR_REGISTER, ADCDataRegisterOffset, &ADCDataRaw);

			ParseADCRawData(ADCDataRaw, &iChannel, &ADCDataV, NULL, NULL, NULL, &bTemp, &bAux, NULL);		
			if (bTemp)
				printf("ADC %d Temp =% 5.1fC [%08x], ",iChannel?1:0, ADCDataV, ADCDataRaw);
			else
			if (bAux)
				printf("AUX %d=% 5.1f [%08x], ",iChannel?1:0, ADCDataV, ADCDataRaw);
			else
				printf("%d=% 10.6f, ", iChannel, ADCDataV);
			
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

	printf("\nmPCIe-AIO16-16F/mPCIe-ADIO16-8F Family ADC Sample 0+\n");
	uint32_t Version = 0;

	apci = open(DEVICEPATH, O_RDONLY);
	if (apci < 0)
	{
		printf("Device file %s could not be opened. Please ensure the APCI driver module is loaded or try sudo?.\nTrying Alternate [ %s ]...\n", DEVICEPATH, DEV2PATH);
		apci = open(DEV2PATH, O_RDONLY);
		if (apci < 0)
		{
			printf("Device file %s could not be opened. Please ensure the APCI driver module is loaded or try sudo?.\n", DEV2PATH);
			exit(0);
		} else
		{
			CHANNEL_COUNT = 8;
		}
	}

	apci_read32(apci, 1, BAR_REGISTER, 0x68, &Version);
	printf("  FPGA Revision %08X\n", Version);

	int ch;
	uint32_t ADCFIFODepth;
	uint32_t ADCDataRaw;
	uint32_t iChannel;
	double ADCDataV;
	uint8_t ADCGainCode;
			
	if(1)
	{
		printf("Demonstrating SOFTWARE START FOREGROUND POLLING ACQUISITION\n");

		BRD_Reset(apci);

		apci_write32(apci, 1, BAR_REGISTER, ADCRATEDIVISOROFFSET, 0); // setting ADC Rate Divisor to zero selects software start ADC mode
		for (ch=0; ch<8; ++ch)
		{
			uint32_t controlValue = ADC_BuildControlValue(1,ch,0,0,0,0,0,0);			
			apci_write32(apci, 1, BAR_REGISTER, ADCControlOffset, controlValue );	// start one conversion on selected channel
			usleep(10); // must not write to +38 faster than once every 10 microseconds in Software Start mode
		}

		if (CHANNEL_COUNT == 16)
			for (ch=0; ch<8; ++ch)
			{
			uint32_t controlValue = ADC_BuildControlValue(1,ch,0,0,0,0,0,0);			
			apci_write32(apci, 1, BAR_REGISTER, ADCControlOffset + 4, controlValue );	// start one conversion on selected channel of second ADC
			usleep(10); // must not write to +38 faster than once every 10 microseconds in Software Start mode
			}

		apci_read32(apci, 1, BAR_REGISTER, ADCFIFODepthOffset, &ADCFIFODepth);
		printf("  ADC FIFO has %d entries\n", ADCFIFODepth);

		for (ch=0; ch < ADCFIFODepth; ++ch)	// read and display data from FIFO
		{
			apci_read32(apci, 1, BAR_REGISTER, ADCDataRegisterOffset, &ADCDataRaw);
			ParseADCRawData(ADCDataRaw, &iChannel, &ADCDataV, &ADCGainCode, NULL, NULL, NULL, NULL, NULL);		
			printf("    ADC CH %2d = % 10.6f [gain code %d] [raw:%08X]\n", iChannel, ADCDataV, ADCGainCode, ADCDataRaw);
		}

		printf("Done with one scan of software-started conversions.\n\n\n");
	}

	if(1)
	{
		printf("Demonstrating TEMPERATURE acquisition\n");

		BRD_Reset(apci);

		apci_write32(apci, 1, BAR_REGISTER, ADCRATEDIVISOROFFSET, 0); // setting ADC Rate Divisor to zero selects software start ADC mode

		uint32_t controlValue = ADC_BuildControlValue(1,0,0,0,0,0,1,0);
		apci_write32(apci, 1, BAR_REGISTER, ADCControlOffset, controlValue );	// start one conversion of temperature input
		usleep(10); // must not write to +38 faster than once every 10 microseconds in Software Start mode

		if (CHANNEL_COUNT == 16) // read second ADC's temperature input sensor
		{
			uint32_t controlValue = ADC_BuildControlValue(1,0,0,0,0,0,1,0);			
			apci_write32(apci, 1, BAR_REGISTER, ADCControlOffset + 4, controlValue );	// start one conversion of temperature input
			usleep(10); // must not write to +38 faster than once every 10 microseconds in Software Start mode
		}

		apci_read32(apci, 1, BAR_REGISTER, ADCFIFODepthOffset, &ADCFIFODepth);
		printf("  ADC FIFO has %d entries\n", ADCFIFODepth);

		for (ch=0; ch < ADCFIFODepth; ++ch)	// read and display data from FIFO
		{
			int bTemp = 0;
			apci_read32(apci, 1, BAR_REGISTER, ADCDataRegisterOffset, &ADCDataRaw);
			ParseADCRawData(ADCDataRaw, &iChannel, &ADCDataV, &ADCGainCode, NULL, NULL, &bTemp, NULL, NULL);		
			if (bTemp)
				printf("    ADC %d Temp = % 3.1fC [raw:%08X]\n", iChannel?1:0, ADCDataV, ADCDataRaw);
			else
				printf("    ADC CH %2d = % 10.6f [gain code %d] [raw:%08X] [temp:%d]\n", iChannel, ADCDataV, ADCGainCode, ADCDataRaw, bTemp);
		}

		printf("Done with temperature sensor conversions.\n\n\n");
	}
	
	if(1)
	{
		printf("Demonstrating AUX acquisition\n");

		BRD_Reset(apci);

		apci_write32(apci, 1, BAR_REGISTER, ADCRATEDIVISOROFFSET, 0); // setting ADC Rate Divisor to zero selects software start ADC mode

		uint32_t controlValue = ADC_BuildControlValue(1,0,0,0,1,0,0,0);
		apci_write32(apci, 1, BAR_REGISTER, ADCControlOffset, controlValue );	// start one conversion of temperature input
		usleep(10); // must not write to +38 faster than once every 10 microseconds in Software Start mode

		if (CHANNEL_COUNT == 16) // read second ADC's temperature input sensor
		{
			uint32_t controlValue = ADC_BuildControlValue(1,0,0,0,1,0,0,0);			
			apci_write32(apci, 1, BAR_REGISTER, ADCControlOffset + 4, controlValue );	// start one conversion of temperature input
			usleep(10); // must not write to +38 faster than once every 10 microseconds in Software Start mode
		}

		apci_read32(apci, 1, BAR_REGISTER, ADCFIFODepthOffset, &ADCFIFODepth);
		printf("  ADC FIFO has %d entries\n", ADCFIFODepth);

		for (ch=0; ch < ADCFIFODepth; ++ch)	// read and display data from FIFO
		{
			int bTemp, bAux = 0;
			apci_read32(apci, 1, BAR_REGISTER, ADCDataRegisterOffset, &ADCDataRaw);
			ParseADCRawData(ADCDataRaw, &iChannel, &ADCDataV, &ADCGainCode, NULL, NULL, &bTemp, &bAux, NULL);		
			if (bTemp)
				printf("    ADC %d Temp =% 5.1fC [%08x]\n",iChannel?1:0, ADCDataV, ADCDataRaw);
			else
			if (bAux)
				printf("    AUX %d=% 5.1f [%08x]\n",iChannel?1:0, ADCDataV, ADCDataRaw);
			else
				printf("    ADC CH %2d = % 10.6f [gain code %d] [raw:%08X] [temp:%d]\n", iChannel, ADCDataV, ADCGainCode, ADCDataRaw, bTemp);			
		}

		printf("Done with AUX sensor conversions.\n\n\n");
	}

	if (1)
	{
		printf("Demonstrating TIMER-DRIVEN (Advanced Sequencer+Temperature+AUX) BACKGROUND-THREAD POLLING ACQUISITION\n");
		printf("Press CTRL-C to halt\nPRESS ENTER TO CONTINUE\n");
		getchar();
			
		BRD_Reset(apci);
		double Hz = 1.0;
		set_acquisition_rate(apci, &Hz);
		printf("ADC Rate: (%lf Hz)\n", Hz);

		// start collecting incoming data in background thread
		pthread_create(&worker_thread, NULL, &worker_main, NULL);
		
		// start sequence of conversions
		uint32_t controlValue = ADC_BuildControlValue(1,1,0,0,1,1,1,0);		
		printf("ADC Control = %08x\n", controlValue);	
		if (CHANNEL_COUNT == 16)
			apci_write32(apci, 1, BAR_REGISTER, ADCControlOffset + 4, controlValue );	
		apci_write32(apci, 1, BAR_REGISTER, ADCControlOffset, controlValue );	

		while (!terminate)
		{
			// do other work while data is collected and displayed by the worker thread.
			// use CTRL-C to exit
		};
	}

	printf("Sample Done.\n"); // never prints due to CTRL-C abort
}




