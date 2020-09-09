	// The card has 3 BARs, and thus 3 base addresses; only the 3rd is used for this, so that's BaseAddr in this code. (The first 2 are for performant IRQ/DMA streaming which isn't up and running yet.)
	// In Linux, you can get the base addresses with lspci in verbose mode, giving it the VendorID 494F to only list our devices.
	// The card uses 2 ADCs("sequencers"), each of which handles 8 channels. In immediate mode, you can only use one ADC at a time, so this code acquires them in turn.
	// This code assumes TUInt8, TUInt16, TUInt32, TSInt8, etc. are all typedeffed.
	// This code wraps memory register accesses as In8, In16, In32, Out8, etc. These ultimately compile to simple assignments, but that's not very readable.


#include <stdint.h>
#include <math.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>

#include "apcilib.h"

const uint16_t BaseADCCommand_SE = 0x8C4E;
const uint16_t BaseADCCommand_Diff = 0x844E;
const unsigned int ADCDataRegisterOffset[2] = { 0x30, 0x34 };
const unsigned int ADCImmediateOffset[2] = { 0x3A, 0x3E };

//[J4D] This assumes that there is only one card in the system so we pass around
//the file descriptor and hard code device index 1

void GetADCDataRaw(int fd, unsigned int iSequencer, uint16_t RangeCode, uint32_t ADCData[])
{
	unsigned int iChannel;
	uint16_t ImmediateCmd;
  unsigned int Toss;

	// Clear the FIFO and reset the internal pipeline.
  apci_write32(fd, 1, 2, ADCDataRegisterOffset[iSequencer], 0);

	for ( iChannel = 0; iChannel <= 7; ++iChannel )
	{
		// Command an immediate conversion on each channel.
		// You can use a different range for each channel if you want, rather than a single variable.
		ImmediateCmd = BaseADCCommand_SE | (iChannel << 12) | (RangeCode << 7);
    apci_write16(fd, 1, 2, ADCImmediateOffset[iSequencer], ImmediateCmd);
	}
	for ( iChannel = 8; iChannel <= 14; ++iChannel )
	{
		// Command extra immediate conversions, to push the real data from the pipeline to the FIFO.
		// In our code these repeat the last command to avoid analog disruptions, from changing ranges or the like.
    apci_write16(fd, 1, 2, ADCImmediateOffset[iSequencer], ImmediateCmd);
	}

	// Consume the stream preamble.
  apci_read32(fd, 1, 2, ADCDataRegisterOffset[iSequencer], &Toss);

	for ( iChannel = 0; iChannel <= 7; ++iChannel )
	{
		// Read out the data for each channel.
    apci_read32(fd, 1, 2, ADCDataRegisterOffset[iSequencer], &ADCData[iSequencer*8 + iChannel]);
	}

	// Will need to clear the FIFO and reset the internal pipeline before the next ADC operation.
}

double VFromRaw(uint32_t RawSample)
{
	// All ranges are bipolar, all counts are 16-bit, so the 0x8000 constant is half the span of counts.
	// These numbers include the extra 2.4% margin for calibration.
	const double RangeScale[8] =
	{
		24.576/0x8000, // code 0
		10.24/0x8000, // code 1
		5.12/0x8000, // code 2
		2.56/0x8000, // code 3
		1.28/0x8000, // code 4
		0.64/0x8000, // code 5
		20.48/0x8000, // code 6
		NAN // code 7
	};
	uint16_t Counts = RawSample & 0xFFFF;
	uint16_t RangeCode = (RawSample >> 16) & 0x7;
	// The other status bits are useful for streaming.

	return RangeScale[RangeCode] * (int16_t)Counts;
}

void GetADCDataV(int fd, uint16_t RangeCode, double ADCDataV[])
{
	uint32_t ADCDataRaw[16];
	unsigned int iChannel;

	// Fetch the raw data(numeric counts plus status bits) for each ADC.
	GetADCDataRaw(fd, 0, RangeCode, &ADCDataRaw[0]);
	GetADCDataRaw(fd, 1, RangeCode, &ADCDataRaw[8]);

	// Convert all 16 channels to V.
	for ( iChannel = 0; iChannel <= 15; ++iChannel )
	{
		ADCDataV[iChannel] = VFromRaw(ADCDataRaw[iChannel]);
	}
}

int main (int argc, char **argv)
{
  int fd;
  
  fd = open("/dev/apci/mpcie_aio16_16f", O_RDONLY);

	if (fd < 0)
	{
		printf("Device file could not be opened. Please ensure the iogen driver module is loaded.\n");
		exit(0);
	}

  double ADCDataV[16];

  GetADCDataV(fd, 0, ADCDataV);

  for (int i = 0; i < 16; i++)
  {
    printf("%d = %f\n", i, ADCDataV[i]);
  }

}




