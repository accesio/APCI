#include <dirent.h>
#include <stdint.h>
#include <math.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "apcilib.h"

#define MAX_DEVICES 100

uint8_t CHANNEL_COUNT = 16; // change to 8 for M.2-/mPCIe-ADIO16-8F Family cards

int bDiagnostic = 0;

#define BAR_REGISTER 1

/* Hardware register offsets */
#define ofsReset				0x00
#define ofsDAC					0x04
#define ofsDACWaveformStatus	0x04
#define ofsDACRateDivisor		0x08
#define ofsADCBaseClock			0x0C
#define ofsDACBaseRate			0x0C
#define ofsADCRateDivisor		0x10
#define ofsADCRange				0x18
#define ofsFAFIRQThreshold		0x20
#define ofsADCFIFODepth			0x28
#define ofsADCFIFO				0x30
#define ofsADCControl			0x38
#define ofsIRQEnable			0x40
#define ofsDIOControl			0x48
#define ofsDACFIFO				0x50
#define ofsDACnSetting			0x54
#define ofsDACFifoSize			0x58
#define ofsFPGAVersion			0x68

#define bmDacFifoHalf (1 << 28)        // bit 28 @ +4
#define bmDacWaveformPlaying (1 << 29) // bit 29 @ +4
#define bmDACFifoHalfIrqEn (1 << 9)    // bit 9 @ +40
#define bmDioAdcStart (1 << 18)        // edges on DIO 14 will cause an ADC Start
#define bmDioAdcStartEdge (1 << 19)

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

/* FORWARD REFERENCES */
int OpenDevFile(void);
void BRD_Reset(int apci);
int ParseADCRawData(uint32_t rawData, uint32_t *channel, double *volts, uint8_t *gainCode, uint16_t *digitalData, int *differential, int *bTemp, int *bAux, int *running );

/* GLOBALS */
static int terminate;
int apci;
enum
{
	bStartADC = 1,
	bDifferential = 1,
	bSingleEnded = 0,
	bSequenceMode = 1,
	bChannelMode = 0,
	bFast = 1,
	bSlow = 0
};

int OpenDevFile(void)
{
	char *devicefiles[MAX_DEVICES] = {0};
	int device_count = 0;
	const char *devicepath = "/dev/apci/";
	DIR *dir;
	struct dirent *entry;
	int selected_device = 0;
	int device_handle = -1;

	printf("Attempting to locate device files in %s\n", devicepath);

	dir = opendir(devicepath);
	if (dir == NULL)
	{
		printf("Unable to open %s\nPerhaps apci.ko is not loaded, or you need sudo.\n", devicepath);
		perror("opendir");
		return -1;
	}

	while ((entry = readdir(dir)) != NULL && device_count < MAX_DEVICES)
	{
		char devfile[512];
		struct stat st;

		snprintf(devfile, sizeof(devfile), "%s%s", devicepath, entry->d_name);
		if (stat(devfile, &st) == 0 && S_ISCHR(st.st_mode))
		{
			printf("Found device file %s\n", entry->d_name);
			devicefiles[device_count] = strdup(devfile);
			if (devicefiles[device_count] == NULL)
			{
				perror("strdup");
				closedir(dir);
				goto cleanup;
			}
			device_count++;
		}
	}

	closedir(dir);

	if (device_count == 0)
	{
		printf("No valid device files found in %s\n", devicepath);
		goto cleanup;
	}

	if (device_count > 1)
	{
		char input[64];

		printf("Multiple device files found:\n");
		for (int i = 0; i < device_count; i++)
			printf("%d: %s\n", i + 1, devicefiles[i]);

		while (1)
		{
			char *end;
			long selection;

			printf("Select a device file to open (1-%d): ", device_count);
			fflush(stdout);
			if (fgets(input, sizeof(input), stdin) == NULL)
			{
				printf("No device selected.\n");
				goto cleanup;
			}

			selection = strtol(input, &end, 10);
			if (end != input && (*end == '\n' || *end == '\0') && selection >= 1 && selection <= device_count)
			{
				selected_device = selection - 1;
				break;
			}
			printf("Invalid selection. Please try again.\n");
		}
	}

	device_handle = open(devicefiles[selected_device], O_RDONLY);
	if (device_handle >= 0)
	{
		printf("Successfully opened device at %s\n", devicefiles[selected_device]);
		if (strstr(devicefiles[selected_device], "adio16_8") != NULL)
			CHANNEL_COUNT = 8;
	}
	else
	{
		perror("open");
	}

cleanup:
	for (int i = 0; i < device_count; i++)
		free(devicefiles[i]);

	return device_handle;
}

#pragma region /* UTILITY FUNCTIONS */
				void abort_handler(int s)
				{
					printf("\n\nCaught signal %d\n",s);

					terminate = 1;

					/* put the card back in the power-up state */
					BRD_Reset(apci);
					exit(1);
				}

				/*
					Assemble an ADC control register command uint32 piecemeal
					all params except `channel_lastchannel` are bools, 0==false
					channel is between 0 and 7 inclusive,
					channel is either "the one channel to acquire" (when bSequencedMode is false)
					or channel is "the last channel in the sequence to acquire" (when bSequencedMode is true)
				*/
				uint32_t ADC_BuildControlValue(int bStartADC, int channel_lastchannel, int bDifferential, int gainCode, int bSequencedMode, int bFast)
				{
					int bTemp = 0; // temp readings are for factory use only
					int bAux = 0;  // the aux inputs aren't connected to anything

					uint32_t controlValue = 0x00000000;
					controlValue |= ADC_CFG_MASK;
					if (bStartADC)
						controlValue |= ADC_START_MASK;

					if ((channel_lastchannel < 8) && (channel_lastchannel >= 0))
						controlValue |= channel_lastchannel << ADC_CHANNEL_SHIFT;
					else
						return -1;	// invalid channel / last_channel

					if (! bDifferential)
						controlValue |= ADC_NOT_DIFF_MASK;

					if ((gainCode < 8) && (gainCode >= 0))
						controlValue |= gainCode << ADC_GAIN_SHIFT;
					else return -2;	// invalid gain code


					// TEMP and AUX are interpreted differently in Sequenced vs "On Demand Conversion" ("Immediate") mode
					if (bSequencedMode)
					{
						controlValue |= ADC_ADVANCED_SEQ_MASK;
						if (! bAux)
							controlValue |= ADC_NOT_AUX_MASK;
						if (! bTemp)
							controlValue |= ADC_NOT_TEMP_MASK;

						// sequenced differential only accepts even channel#s
						if (bDifferential)
							controlValue &= ~(1 << ADC_CHANNEL_SHIFT);
					}
					else // "On Demand Conversion Mode" uses the AUX and TEMP bits differently:
					{
						if (bAux && bTemp) return -3;	// invalid to select both Aux and Temp inputs in Immediate mode (On Demand Conversion Mode)

						if (!bAux && !bTemp)
							controlValue = controlValue | ADC_NOT_AUX_MASK | ADC_NOT_TEMP_MASK;

						if (bAux || bTemp)
							controlValue = controlValue & ~(ADC_NOT_AUX_MASK | ADC_NOT_TEMP_MASK);

						if (!bTemp)
							controlValue |= ADC_NOT_TEMP_MASK; // select NOT temp thus AUX
					}

					if (! bFast)
						controlValue |= ADC_NOT_FAST_MASK;

					if (bDiagnostic)
						printf("  ADC Control Value %8X %s %s CH%2d%s%s\n", controlValue, bTemp?"TEMP":"", bAux?"AUX":"", channel_lastchannel,bSequencedMode?" SEQ":" Imm", bDifferential?" Diff":" S.E.");

					return controlValue;
				}

				/* print ADC data as Volts etc., and if bDiagnostic, in parsed-raw and raw-raw forms */
				void pretty_print_ADC_raw_data(uint32_t AdcRawData, int bCompact)
				{
					unsigned int iChannel;
					double ADCDataV;
					uint8_t gainCode;
					int bTemp, bAux, bDifferential, bRunning;
					uint16_t digitalData;

					ParseADCRawData(AdcRawData, &iChannel, &ADCDataV, &gainCode, &digitalData, &bDifferential, &bTemp, &bAux, &bRunning);
					if ((iChannel == 0) && (!bTemp)) printf("\n  ");

					if (bDiagnostic > 0)
					{
						printf(" [%08X", AdcRawData);
						if (bDiagnostic > 1)
						{
							printf(" %s %s CH%02d G%d DIO%2x %s", bTemp?"TEMP":"", bAux?"AUX":"", iChannel, gainCode, digitalData, bDifferential?"Diff":"S.E.");
						}
						printf("] ");
					}

					{
						if (bTemp)
							printf("ADC %d Temp =% 5.1fC",iChannel?1:0, ADCDataV);
						else
						if (bAux)
							printf("ADC %d Aux =% 5.1f",iChannel?1:0, ADCDataV);
						else
							printf("CH %2d =% 8.4f", iChannel, ADCDataV);
					}

					if (bCompact)
						printf(", ");
					else
						printf("\n  ");
				}
#pragma endregion

void BRD_Reset(int apci)
{
	apci_write32(apci, 1, BAR_REGISTER, ofsReset, 0x1);
}

int ParseADCRawData(uint32_t rawData, uint32_t *channel, double *volts, uint8_t *gainCode, uint16_t *digitalData, int *differential, int *bTemp, int *bAux, int *bRunning )
{
	int bInvalid = rawData & (1<<31);
	if (!bInvalid)
	{
		if (channel) *channel = (rawData >> RAWchannelshift) & 0x0F;
		if (gainCode) *gainCode = (rawData >> RAWgainshift) & 0x07;
		if (digitalData) *digitalData = (rawData >> RAWdioshift) & 0x0F;
		if (differential) *differential = (rawData >> RAWdiffshift) & 0x01;
		if (bTemp) *bTemp = (rawData >> RAWtempshift) & 0x01;
		if (bAux) *bAux = (rawData >> RAWmuxshift) & 0x01;
		if (bRunning) *bRunning = (rawData >> RAWrunningshift) & 0x01;

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
				*channel &= 8; // mask out the actual channel bits as they don't apply if the reading was temperature input, but leave "sequencer" bit
			}
			else
			{
				uint16_t RangeCode = (rawData >> RAWgainshift) & 0x7; // don't rely on "gainCode" being non-null!
				*volts = rawData & (1<<31) // if invalid
					? NAN
					: RangeScale[RangeCode] * Counts;
			}
		}
	}
	return bInvalid;
}

//------------------------------------------------------------------------------------
int main(void)
{
	struct sigaction sigIntHandler;

	sigIntHandler.sa_handler = abort_handler;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;
	sigaction(SIGINT, &sigIntHandler, NULL);
	sigaction(SIGABRT, &sigIntHandler, NULL);

	apci = OpenDevFile();
	if (apci < 0)
		return 1;

	uint32_t Version = 0;
	apci_read32(apci, 1, BAR_REGISTER, ofsFPGAVersion, &Version);
	printf("\nmPCIe-AIO16-16F/mPCIe-ADIO16-8F Family ADC Sample 0+ [FPGA Rev %08X]\n", Version);
	if (1)
	{
		printf("Demonstrating external ADC start (Advanced Sequencer) POLLING ACQUISITION\n");

		BRD_Reset(apci);
		apci_write32(apci, 1, BAR_REGISTER, ofsDIOControl, bmDioAdcStart ); // falling edges on DIO 14 will cause ADC start
		int gaincode = 0;
		int lastchan = 7;
		uint32_t AdcControlValue = (1<<17)| (1<<16) | ADC_BuildControlValue(bStartADC,lastchan,bSingleEnded,gaincode,bChannelMode,bFast);
		printf("ADC Control Value: %08X\n", AdcControlValue);
		if (CHANNEL_COUNT == 16)
			apci_write32(apci, 1, BAR_REGISTER, ofsADCControl + 4, AdcControlValue );
		apci_write32(apci, 1, BAR_REGISTER, ofsADCControl, AdcControlValue );

		printf("Produce falling edges into DIO 14 now\n CTRL-C to exit.\n");

		while (!terminate)
		{
			__u32 avail = 0;
			apci_read32(apci, 1, BAR_REGISTER, ofsADCFIFODepth, &avail);
			if (!avail) continue;
			do {
				__u32 rawdata;
				apci_read32(apci, 1, BAR_REGISTER, ofsADCFIFO, &rawdata);
				pretty_print_ADC_raw_data(rawdata, 0);
			} while(--avail);

		};
	}

	printf("\nSample Done.\n"); // never prints due to CTRL-C abort
}