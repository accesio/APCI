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
#include <sys/mman.h>

pthread_t worker_thread;

#include "apcilib.h"
#define DEVICEPATH "/dev/apci/mpcie_aio16_16fds_0"

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
#define ofsDACFifoSize 0x58
#define ofsFPGAVersion 0x68

int DACFIFOSamples = 8192;

#define DACBufferLength (65536 * 4)
#define DacsPerPoint 1
double DacWaveformPPS = 1000000.0 / DacsPerPoint;
void *mmap_ptr;
uint32_t *data;

int SampleNumber = 0;
uint64_t SamplesUploaded = 0;

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
        apci = open(argv[1], O_RDWR); // open device file from command line
        if (apci < 0)
            printf("Couldn't open device file on command line [%s]: do you need sudo? /dev/apci?\n", argv[1]);
    }

    if (apci < 0) // if the command line didn't work, or if they didn't pass a parameter
    {
        apci = open(DEVICEPATH, O_RDWR);
        if (apci < 0)
        {
            printf("Device file %s could not be opened. Please ensure the APCI driver module is loaded or try sudo?.\n", DEVICEPATH);
            return 1;
        }
    }

    // TODO: allow user to specify rate and number of DACs as parameters on the command line

    //need to set the dac buffer size before mmap. setting it again will cause
    //the kernel buffer to be freed underneath the map.
    apci_dac_buffer_size(apci, DACBufferLength * sizeof(uint32_t));

    //send PAGE_SIZE * 1 as last paramater to map the DAC buffer. 
    mmap_ptr = (void *) mmap(NULL, DACBufferLength * sizeof(uint32_t), PROT_READ | PROT_WRITE, MAP_SHARED, apci, getpagesize());
    if (MAP_FAILED == mmap_ptr)
    {
        printf("mmap_failed\n");
        return -1;
    }

    data = (uint32_t *) mmap_ptr;

    printf("Generating Waveform Data (32-bit) on %d DACs for playback at %2fHz\n", DacsPerPoint, DacWaveformPPS);
    for (int i = 0; i < DACBufferLength; i++)
    {
        switch (i % DacsPerPoint)
        {
        case 0:
            data[i] = 0x000000 | (uint16_t)(cos(2 * M_PI * i / DACBufferLength) * 0x7FFF + 0x8000); // cosine wave;
            //data[i] = 65535 * ((i/DacsPerPoint) & 1); // diagnostic version fastest possible square wave. note: DAC output slew rate is 5V/µSec
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
    apci_write32(apci, 1, BAR_REGISTER, ofsReset, 1);

    uint32_t Version = 0;
    apci_read32(apci, 1, BAR_REGISTER, ofsFPGAVersion, &Version);
    printf("Detected FPGA revision: \"%08X\"\n", Version);

    apci_read32(apci, 1, BAR_REGISTER, ofsDACFifoSize, &DACFIFOSamples);
    printf("DAC FIFO size detected as %d samples\n", DACFIFOSamples);

    printf("Putting DACs in +/- 10V range\n");
    apci_write32(apci, 1, BAR_REGISTER, ofsDAC, 0xE00003); // "Write Span to All" = 0xE; 3 = +/- 10V

    usleep(100); // better would be to read ofsDAC and check if bit 31 is set until it is clear; this bit indicates SPI busy

    printf("Setting all DACs to 0V\n");
    apci_write32(apci, 1, BAR_REGISTER, ofsDAC, 0xA08000); // "Write Code to All, Update, and power up" = 0xA

    printf("Setting Waveform to %d DAC Samples per Point\n", DacsPerPoint);
    apci_write32(apci, 1, BAR_REGISTER, ofsDACnSetting, DacsPerPoint);

    printf("Pre-loading one DAC Waveform FIFO worth of Sample Data (%d) to DAC FIFO\n", DACFIFOSamples);
    apci_writebuf32(apci, 1, BAR_REGISTER, ofsDACFIFO, 0, DACFIFOSamples);
    SampleNumber += DACFIFOSamples;
    SamplesUploaded += DACFIFOSamples;

    // Launch background thread that will keep the DAC FIFO filled
    pthread_create(&worker_thread, NULL, &worker_main, NULL);

    printf("Starting Waveform Output @ %5.1f *points* per second on %d DACs per point\n[[ CTRL-C to end ]]\n", DacWaveformPPS, DacsPerPoint);
    set_DACWaveform_rate(apci, &DacWaveformPPS);

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
    } while ((regvalue & bmDacWaveformPlaying) == 0);
    printf("\n");

    printf("DAC Waveform finished playing %ld Points at %6.1f Hz / DAC\n", SamplesUploaded, DacWaveformPPS);

    double Hz = 0;
    set_DACWaveform_rate(apci, &Hz);

    apci_write32(apci, 1, BAR_REGISTER, ofsReset, 1);

    printf("\nSample Done.\n");
}




/* Background thread to transmit data to DAC Waveform FIFO */
void *worker_main(void *arg)
{
    uint32_t roomInFIFO_Samples = 0;
    int32_t overage = 0;
    do
    {
        apci_read32(apci, 1, BAR_REGISTER, ofsDACFIFO, &roomInFIFO_Samples);
        roomInFIFO_Samples = (DACFIFOSamples - roomInFIFO_Samples);

        // Use buffered kernel write to fill FIFO, handle data[] wrap
        overage = SampleNumber + roomInFIFO_Samples - DACBufferLength;
        if (overage <= 0) // no wrap, simple:
        {
            apci_writebuf32(apci, 1, BAR_REGISTER, ofsDACFIFO, SampleNumber * sizeof(uint32_t), roomInFIFO_Samples);
            SampleNumber += roomInFIFO_Samples;
            SampleNumber %= DACBufferLength;
        }
        else // handle wrap case:
        {
            apci_writebuf32(apci, 1, BAR_REGISTER, ofsDACFIFO, SampleNumber * sizeof(uint32_t), DACBufferLength - SampleNumber);
            SampleNumber = 0;
            apci_writebuf32(apci, 1, BAR_REGISTER, ofsDACFIFO, SampleNumber * sizeof(uint32_t), overage);
            SampleNumber += overage;
        }
        SamplesUploaded += roomInFIFO_Samples;


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
        divisor = floor(base_clock / *Hz);
        *Hz = base_clock / divisor;
        apci_write32(fd, 1, BAR_REGISTER, ofsDACRateDivisor, divisor);
        printf(" rate = %5.1f Hz\n", *Hz);
    }
}

void abort_handler(int s)
{
    terminate = 1;
}
