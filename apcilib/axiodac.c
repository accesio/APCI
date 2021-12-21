#include <stdint.h>
#include <math.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

pthread_t worker_thread;

#include "apcilib.h"
#define DEVICEPATH "/dev/apci/mpcie_aio16_16f_0"

#define BAR_REGISTER 1
#define ofsReset 0x0
#define ofsDAC 0x04
#define ofsDACRateDivisor 0x08
#define ofsDACBaseRate 0x0C
#define ofsDACWaveformStatus 0x4
#define bmDacFifoHalf (1<<28) // bit 28 @ +4
#define bmDacWaveformPlaying (1<<29) // bit 29 @ +4
#define ofsDACFIFO 0x50
#define ofsDACnSetting 0x54
#define maxtimeout 0x10000  


#define  DACBufferLength (65536*4)
int numDACs = 4;
double DacWaveformHz = 125000.0;
int DACFIFOSamples = 1024;
uint32_t data[DACBufferLength][4];

int apci;

static int terminate=0;


/* configure DAC "Point" output rate */
void set_DACWaveform_rate (int fd, double *Hz)
{
    uint32_t base_clock;
    uint32_t divisor = 0;

    if(*Hz != 0)
    {
        apci_read32(fd, 1, BAR_REGISTER, ofsDACBaseRate, &base_clock);
        printf("  set_waveform_rate: TargetHz (%5.1f) = base_clock (%d) / ", *Hz, base_clock);
        divisor = round(base_clock / *Hz);
        *Hz = base_clock / divisor; /* actual Hz selected, based on the limitation caused by integer divisors */
    }
    printf("divisor (%d) = ", divisor);

    apci_write32(fd, 1, BAR_REGISTER, ofsDACRateDivisor, divisor);
    printf(" rate = %5.1f Hz\n", *Hz);
}

void abort_handler(int s)
{
    terminate = 1;
}



//------------------------------------------------------------------------------------
int main (int argc, char **argv)
{
    struct sigaction sigIntHandler;

	sigIntHandler.sa_handler = abort_handler;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;
	sigaction(SIGINT, &sigIntHandler, NULL);
	sigaction(SIGABRT, &sigIntHandler, NULL);
	


	printf("\nAxIO Family Timed DAC Waveform Output Sample 0\n   --- add parameter /dev/apci/devicefilename or %s will be assumed\n",DEVICEPATH);
	apci = -1;
	if (argc > 1)
	{
		apci = open(argv[1], O_RDONLY);	// open device file from command line
		if (apci < 0)
			printf("Couldn't open device file on command line [%s]: do you need sudo? /dev/apci? \n", argv[1]);
	}

	if (apci < 0) // if the command line didn't work, or if they didn't pass a parameter
	{
		apci = open(DEVICEPATH, O_RDONLY);
		if (apci < 0)
		{
			printf("Device file %s could not be opened. Please ensure the APCI driver module is loaded or try sudo?.\n", DEVICEPATH);
		}
	}

    printf("Generating Waveform Data (32-bit)\n");
    
    for (int i = 0; i < DACBufferLength; i++)
    {
        //data[i][0] = 0x000000 | (((i & 1) == 0) ? i%0x10000:0xFFFF-(i%0x10000)); 
 
        data[i][0] = 0x000000 | (uint16_t) (cos(2 * M_PI * i / DACBufferLength) * 0x7FFF + 0x8000 ); // cosine wave;
        data[i][1] = 0x010000 | (uint16_t) (sin(2 * M_PI * i / DACBufferLength) * 0x7FFF + 0x8000 ); // sine wave
        data[i][2] = 0x020000 | (uint16_t) (cos(2 * M_PI * i / DACBufferLength) * 0x7FFF + 0x8000 ); // cosine wave;
        data[i][3] = 0x030000 | (uint16_t) (cos(2 * M_PI * i / DACBufferLength) * 0x7FFF + 0x8000 ); // cosine wave;
    }

    printf("Performing a software reset of the device.\n");
    apci_write32(apci, 1, BAR_REGISTER, 0x00, 1);
    
    //usleep(100);
    
    uint32_t Version = 0;
    apci_read32(apci, 1, BAR_REGISTER, 0x68, &Version);
    printf("Detected FPGA revision: \"%08X\"\n", Version);

    printf("Putting DACs in +/- 10V range\n");
    apci_write32(apci, 1, BAR_REGISTER, ofsDAC, 0xE00003); // "Write Span to All"; 3 = +/- 10V

    usleep(100);

    printf("Setting all DACs to 0V\n");
    apci_write32(apci, 1, BAR_REGISTER, ofsDAC, 0xA08000); // "Write Code to All, Update, and power up"

    //printf("NYI: Setting Waveform to %d DAC Samples per Point\n", numDACs);
    // apci_write32(apci, 1, BAR_REGISTER, ofsDACnSetting, numDACs);// NYI

    printf("Writing one DAC Waveform FIFO worth of Sample Data to DAC FIFO\n");
    int PointNumber = 0;
    for (size_t i = 0; i < DACFIFOSamples / numDACs; i++)
    {
        for (size_t dacnum=0; dacnum < numDACs; dacnum++)
        {
            apci_write32(apci, 1, BAR_REGISTER, ofsDACFIFO, data[i][dacnum]);
        }
        PointNumber++;
    }

    uint32_t timeout = maxtimeout; //awk: convert to time-based not loop-based timeouts
    uint32_t regvalue = 0; 

    printf("Starting Waveform Output @ %5.1f\n[[ CTRL-C to end ]]\n", DacWaveformHz);
    set_DACWaveform_rate(apci, &DacWaveformHz);

    do
    {
        // wait until DAC FIFO is less than half full (@+4.28 set)
        int bDACFIFOHalfFull;
        do
        {
            apci_read32(apci, 1, BAR_REGISTER, ofsDACWaveformStatus, &regvalue);
            bDACFIFOHalfFull = (regvalue & bmDacFifoHalf) != 0;
        } while (!bDACFIFOHalfFull && --timeout);

        if (timeout == 0)
        {
            printf("Timeout waiting for DAC FIFO half empty\n");
            return 1;
        }

        if (timeout == maxtimeout-1)
        {
            printf("DAC FIFO half full reached instantly, could be underrunning FIFO %08x\n", regvalue);
        }

        timeout = maxtimeout;

        // write half a FIFO worth of data
        for (size_t i = 0; i < DACFIFOSamples/numDACs/2; i++)
        {
            for (size_t dacnum=0; dacnum < numDACs; dacnum++)
            {
                apci_write32(apci, 1, BAR_REGISTER, ofsDACFIFO, data[PointNumber][dacnum]);
                // printf("  Point %d: %08X\n", PointNumber, data[PointNumber][dacnum]);
            }
            PointNumber++;
            PointNumber %= DACBufferLength;
        }
    } while (!terminate); 

    printf("Waiting for DAC Waveform to finish playing\n");
    do
    {
        apci_read32(apci, 1, BAR_REGISTER, ofsDACWaveformStatus, &regvalue);
        //printf(".");
    } while ((regvalue & bmDacWaveformPlaying) == 0);
    printf("\n");

    printf("DAC Waveform finished playing %d Points at %6.1f\n", DACBufferLength, DacWaveformHz);

    double Hz = 0;
    set_DACWaveform_rate(apci, &Hz);

    apci_write32(apci, 1, BAR_REGISTER, ofsReset, 1); // board reset
    
    printf("Setting all DACs to 0V\n");
    
    printf("\nSample Done.\n"); 
}