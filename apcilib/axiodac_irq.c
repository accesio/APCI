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
#define bmDacFifoHalf (1 << 28)        // bit 28 @ +4
#define bmDacWaveformPlaying (1 << 29) // bit 29 @ +4
#define ofsIRQEN 0x40
#define bmDACFifoHalfIrqEn (1 << 9) // bit 9 @ +40
#define ofsDACFIFO 0x50
#define ofsDACnSetting 0x54
#define maxtimeout 0x10000 // TODO: use RTC-based timeout

int DACFIFO_Samples = 8192;

#define DACBufferLength (65536 * 4)
int numDACs = 4;
double DacWaveformHz = 125000.0;
uint32_t data[DACBufferLength];
int SampleNumber = 0;

int apci;

static int terminate = 0;
void set_DACWaveform_rate(int fd, double *Hz);
void abort_handler(int s);
void *worker_main(void *arg);

//------------------------------------------------------------------------------------
int main(int argc, char **argv)
{
    struct sigaction sigIntHandler;

    sigIntHandler.sa_handler = abort_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGINT, &sigIntHandler, NULL);
    sigaction(SIGABRT, &sigIntHandler, NULL);

    printf("\nAxIO Family Timed DAC Waveform Output Sample 0\n   --- add parameter /dev/apci/devicefilename or %s will be assumed\n", DEVICEPATH);
    apci = -1;
    if (argc > 1)
    {
        apci = open(argv[1], O_RDONLY); // open device file from command line
        if (apci < 0)
            printf("Couldn't open device file on command line [%s]: do you need sudo? /dev/apci? \n", argv[1]);
    }

    if (apci < 0) // if the command line didn't work, or if they didn't pass a parameter
    {
        apci = open(DEVICEPATH, O_RDONLY);
        if (apci < 0)
        {
            printf("Device file %s could not be opened. Please ensure the APCI driver module is loaded or try sudo?.\n", DEVICEPATH);
            return 1;
        }
    }

    printf("Generating Waveform Data (32-bit)\n");
    for (int i = 0; i < DACBufferLength; i++)
    {
        switch (i % numDACs)
        {
        case 0:
            data[i] = 0x000000 | (uint16_t)(cos(2 * M_PI * i / DACBufferLength) * 0x7FFF + 0x8000); // cosine wave;
            break;
        case 1:
            data[i] = 0x010000 | ((i >> 2) % 0x10000);
            break;
        case 2:
            data[i] = 0x020000 | (i % 0x10000);
            break;
        case 3:
            data[i] = 0x030000 | (i % 0x10000);
            break;
        }
    }

    printf("Performing a software reset of the device.\n");
    apci_write32(apci, 1, BAR_REGISTER, 0x00, 1);

    uint32_t Version = 0;
    apci_read32(apci, 1, BAR_REGISTER, 0x68, &Version);
    printf("Detected FPGA revision: \"%08X\"\n", Version);

    printf("Putting DACs in +/- 10V range\n");
    apci_write32(apci, 1, BAR_REGISTER, ofsDAC, 0xE00003); // "Write Span to All"; 3 = +/- 10V

    usleep(100); // better would be to read ofsDAC and check if bit 31 is set until it is clear; this bit indicates SPI busy

    printf("Setting all DACs to 0V\n");
    apci_write32(apci, 1, BAR_REGISTER, ofsDAC, 0xA08000); // "Write Code to All, Update, and power up"

    printf("NYI: Setting Waveform to %d DAC Samples per Point\n", numDACs);
    apci_write32(apci, 1, BAR_REGISTER, ofsDACnSetting, numDACs); // NYI

    printf("Writing one DAC Waveform FIFO worth of Sample Data (%d) to DAC FIFO\n", DACFIFO_Samples);
    SampleNumber = 0; // note: redundant but reassuring
    apci_writebuf32(apci, 1, BAR_REGISTER, ofsDACFIFO, data, DACFIFO_Samples);
    SampleNumber += DACFIFO_Samples;
 
    printf("enabling DAC FIFO PAE IRQ\n");
    apci_write32(apci, 1, BAR_REGISTER, ofsIRQEN, bmDACFifoHalfIrqEn); // "Enable FIFO PAE IRQ"


    // Launch background thread that will write the DAC FIFO on each "half empty" IRQ
    pthread_create(&worker_thread, NULL, &worker_main, NULL);

    printf("Starting Waveform Output @ %5.1f\n[[ CTRL-C to end ]]\n", DacWaveformHz);
    set_DACWaveform_rate(apci, &DacWaveformHz);

    uint32_t regvalue = 0;
    while (!terminate)
    {
        apci_read32(apci, 1, BAR_REGISTER, ofsDACWaveformStatus, &regvalue);
        if (regvalue & bmDacWaveformPlaying)
        {
            printf("\nDAC FIFO is empty -- ABORTING\n");
            uint32_t roomInFIFO_Samples = 0;
            terminate = 1;
        }
        else
        {
            // put idle / foreground code here
        }
        usleep(100);
    }
    apci_cancel_irq(apci, 1);
    pthread_join(worker_thread, NULL);

    printf("Waiting for DAC Waveform to finish playing\n");
    do
    {
        apci_read32(apci, 1, BAR_REGISTER, ofsDACWaveformStatus, &regvalue);
        // printf(".");
    } while ((regvalue & bmDacWaveformPlaying) == 0);
    printf("\n");

    printf("DAC Waveform finished playing %d Points at %6.1f Hz\n", DACBufferLength, DacWaveformHz);

    double Hz = 0;
    set_DACWaveform_rate(apci, &Hz);

    apci_write32(apci, 1, BAR_REGISTER, ofsReset, 1); // board reset

    printf("Setting all DACs to 0V\n");

    printf("\nSample Done.\n");
}




/* Background thread to transmit data to DAC Waveform FIFO */
void *worker_main(void *arg)
{
    do
    {
        int status = apci_wait_for_irq(apci, 1);
        if (status < 0)
        {
            printf("Error waiting for IRQ: %d\n", status);
            break;
        }

        uint32_t roomInFIFO_Samples = 0;
        apci_read32(apci, 1, BAR_REGISTER, ofsDACFIFO, &roomInFIFO_Samples);
        roomInFIFO_Samples = (DACFIFO_Samples - roomInFIFO_Samples);

        // Use buffered kernel write to fill FIFO, handle data[] wrap
        int32_t overage = SampleNumber + roomInFIFO_Samples - DACBufferLength;
        if (overage <= 0) // no wrap, simple:
        {
            apci_writebuf32(apci, 1, BAR_REGISTER, ofsDACFIFO, &data[SampleNumber], roomInFIFO_Samples);
            SampleNumber += roomInFIFO_Samples;
            SampleNumber %= DACBufferLength;
        }
        else // handle wrap case:
        {
            apci_writebuf32(apci, 1, BAR_REGISTER, ofsDACFIFO, &data[SampleNumber], DACBufferLength - SampleNumber);
            SampleNumber = 0;
            apci_writebuf32(apci, 1, BAR_REGISTER, ofsDACFIFO, &data[SampleNumber], overage);
            SampleNumber += overage;
        }

    } while (!terminate);

    printf("  Worker Thread: exiting; Waveform Playback ended.\n");
}

/* configure DAC "Point" output rate */
void set_DACWaveform_rate(int fd, double *Hz)
{
    uint32_t base_clock;
    uint32_t divisor = 0;

    if (*Hz != 0)
    {
        apci_read32(fd, 1, BAR_REGISTER, ofsDACBaseRate, &base_clock);
        printf("  set_waveform_rate: TargetHz (%5.1f) = base_clock (%d) / ", *Hz, base_clock);
        divisor = round(base_clock / *Hz);

        apci_write32(fd, 1, BAR_REGISTER, ofsDACRateDivisor, divisor);
        printf(" rate = %5.1f Hz\n", *Hz);
    }
}

void abort_handler(int s)
{
    terminate = 1;
}
