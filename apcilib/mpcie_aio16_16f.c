
#include <stdint.h>
#include <math.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>

#include "apcilib.h"
#define DEV1PATH "/dev/apci/mPCIe_AIO16_16F_proto_0"
#define DEV2PATH "/dev/apci/mpcie_aio16_16f_0"
#define DEVICEPATH DEV2PATH

#define REGISTER_BAR 1

const uint16_t BaseADCCommand_SE = 0x8C4E;
const uint16_t BaseADCCommand_Diff = 0x844E;
const unsigned int ADCDataRegisterOffset[2] = { 0x30, 0x34 };
const unsigned int ADCImmediateOffset[2] = { 0x3A, 0x3E };


const unsigned int LTC2664Cmd_Data      = 0x3; //Write code to N, update N, power up.
const unsigned int LTC2664Cmd_Range     = 0x6; //Write span to N.
const unsigned int LTC2664Cmd_Data_All  = 0xA; //Write code to all, update all, power up.
const unsigned int LTC2664Cmd_Range_All = 0xE; //Write span to all.

const unsigned int DACOffset = 0x04;

uint32_t LastDACRanges[4] = {3, 3, 3, 3};

// [J4D] This assumes that there is only one card in the system so we pass around
// the file descriptor and hard code device index 1

int LTC2664_WriteDAC(int fd, uint32_t Command, uint32_t Address, uint32_t Data)
{
	Data = (Data & 0xFFFF) | ((Address & 0xF) << 16) | ((Command & 0xF) << 20);
	return apci_write32(fd, 1, REGISTER_BAR, DACOffset, Data);
}

int DAC_SetRange1(int fd, uint32_t Ch, uint32_t Range)
/*
	Range 0 = 0-5V
	Range 1 = 0-10V
	Range 2 = �5V
	Range 3 = �10V
	Range 4 = �2.5V
*/
{
	uint32_t Result = LTC2664_WriteDAC(fd, LTC2664Cmd_Range, Ch, Range);
	LastDACRanges[Ch] = Range;
	return Result;
}

int DAC_OutputV(int fd, uint32_t Ch, double V)
{
	int bRangeBipolar[5] = { 0, 0, 1 , 1 , 1  };
	double RangeSpan[5] = {   5  ,  10  ,  10  ,  20  ,   5   };

	uint32_t Range = LastDACRanges[Ch];
	signed int Data = (V / RangeSpan[Range] * 0x10000);
	if ( bRangeBipolar[Range] ) Data = Data - 0x8000;
	if ( (Data < 0x0000) || (Data > 0xFFFF) ) return -65536;

	return LTC2664_WriteDAC(fd, LTC2664Cmd_Data, Ch, Data);
}


double VFromRaw(uint32_t RawSample)
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
		20.48 / 0x8000, // code 6
		NAN // code 7
	};
	uint16_t Counts = RawSample & 0xFFFF;
	uint16_t RangeCode = (RawSample >> 16) & 0x7;
	// The other status bits are useful for streaming.

	return RangeScale[RangeCode] * (int16_t)Counts;
}

void GetADCDataRaw(int fd, unsigned int iSequencer, uint16_t RangeCode, uint32_t ADCData[])
{
	unsigned int iChannel;
	uint16_t ImmediateCmd;
	unsigned int Toss;
	printf("Acquiring 8 channels from Sequencer %d\n", iSequencer);

	// Clear the FIFO and reset the internal pipeline.
	// apci_write32(fd, 1, REGISTER_BAR, ADCDataRegisterOffset[iSequencer], 0);

	for ( iChannel = 0; iChannel < 8; ++iChannel )
	{
		// Command an immediate conversion on each channel.
		// You can use a different range for each channel if you want, rather than a single variable.
		ImmediateCmd = BaseADCCommand_SE | (iChannel << 12) | (RangeCode << 7);
  		apci_write16(fd, 1, REGISTER_BAR, ADCImmediateOffset[iSequencer], ImmediateCmd);
	}

	for ( iChannel = 8; iChannel <= 14; ++iChannel )
	{
		  // Command extra immediate conversions, to push the real data from the pipeline to the FIFO.
		  // In our code these repeat the last command to avoid analog disruptions, from changing ranges or the like.
		  apci_write16(fd, 1, REGISTER_BAR, ADCImmediateOffset[iSequencer], ImmediateCmd);
	}

	// Consume the stream preamble.
	apci_read32(fd, 1, REGISTER_BAR, ADCDataRegisterOffset[iSequencer], &Toss);

	for ( iChannel = 0; iChannel <= 7; ++iChannel )
	{
		// Read out the data for each channel.
		apci_read32(fd, 1, REGISTER_BAR, ADCDataRegisterOffset[iSequencer], &ADCData[iChannel]);
	}

	// Will need to clear the FIFO and reset the internal pipeline before the next ADC operation.
}

int main (int argc, char **argv)
{
	int fd;
	printf("mPCIe-AIO16-16F Family ADC Sample 0\n");
	fd = open(DEVICEPATH, O_RDONLY);

	if (fd < 0)
	{
		printf("Device file could not be opened. Please ensure the APCI driver module is loaded.\n");
		exit(0);
	}

	apci_write8(fd, 1, REGISTER_BAR, 0, 0xff); // board reset

	double ADCDataV[16];

	uint32_t ADCDataRaw[16];
	unsigned int iChannel;

	// Fetch the raw data(numeric counts plus status bits) for each ADC.
	GetADCDataRaw(fd, 0, 0, &ADCDataRaw[0]);
	GetADCDataRaw(fd, 1, 1, &(ADCDataRaw[8]));

	// Convert all 16 channels to V.
	for ( iChannel = 0; iChannel <= 15; ++iChannel )
	{
		ADCDataV[iChannel] = VFromRaw(ADCDataRaw[iChannel]);
	}

	for (int i = 0; i < 16; i++)
	{
		printf("%2d = %11.8f\t[raw: %08X ]\n", i, ADCDataV[i], ADCDataRaw[i]);
	}
	
	DAC_OutputV(fd, 0, 0.0);
	DAC_OutputV(fd, 1, 0.0);
	DAC_OutputV(fd, 2, 0.0);
	DAC_OutputV(fd, 3, 0.0);
}




