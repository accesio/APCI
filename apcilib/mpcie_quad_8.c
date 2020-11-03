#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <linux/types.h>
#include <unistd.h>
#include <signal.h>
#include <asm-generic/int-ll64.h>

#include "apcilib.h"

// Forward declarations -- TODO: move into library
void Init_7766( __u16 CtrlReg );
__u32 Read_7766_Counter( int channel );
__u8 ReadFlags_7766( int channel );
void ResetCount_7766( int channel );
void SetQuadCountMode_7766(int channel, int index);
void displayCounts();

int apci;
__u16 CtrlRegStore;
int number_of_channels = 8;


// ---------------------------------------------------------------------------------------------------
int main (int argc, char **argv)
{
    __u8 inputs = 0;
    int status = 0;  

    apci = open("/dev/apci/mpcie_quad_8_0", O_RDONLY);

    if (apci < 0)
    {
        printf("Device file could not be opened. Try sudo, or chmod the device file, or please ensure the apci driver module is loaded.\n");
        exit(0);
    }

    printf("mPCIe-QUAD sample\n"
           "This sample assumes 8-channels but works with 4\n\n"
          );

    Init_7766(0x2831); // init all 

    displayCounts();
    
    printf("cause inputs to change (move encoder)\n"
           "five seconds...\n"
          );
    sleep(5);

    displayCounts();

    close(apci);
    return 0;
}

void displayCounts()
{
    printf("%2s %10s","CH", "COUNTS\n");
    for (int ch=0; ch < number_of_channels; ch++)
        printf("%2d %10d\n", ch, Read_7766_Counter(ch));
}

void Init_7766( __u16 CtrlReg )
{
    // See spec for control register options etc
    // for example: MCR0 and MCR1 == 0xA001  == 1010 000 000 0001 == MCR1 B7........MCR0 B0
    // Init all 8 channels the same way for free running count mode acquisition:
    CtrlRegStore = CtrlReg;
    for (int ch = 0; ch < number_of_channels; ch++)
    {
        apci_write16(apci, 1, 2, (uint)(0 + (ch * 8)), CtrlReg);  // default 0x2831 init PCI board mode see spec
        apci_write8(apci, 1, 2, (uint)(7 + (ch * 8)), 0x01);	// swap inA and inB reverse direction value: (optional) (CPLD)
        apci_write8(apci, 1, 2, (uint)(6 + (ch * 8)), 0x09);	// reset all flags and counters
    }
}

__u32 Read_7766_Counter(int channel)
{
    __u32 data = 0;
    apci_write8(apci, 1, 2, 6 + channel * 8, 0x04);	// load ODR from CNTR
    apci_read32(apci, 1, 2, 2 + channel * 8, &data);
    return data;
}

__u8 ReadFlags_7766(int channel)
{
    __u8 flags;
    apci_read8(apci, 1, 2, 6 + channel * 8, &flags);
    return flags;
}

void ResetCount_7766(int channel)
{
    // Reset the counters for one channel:
    apci_write8(apci, 1, 2, 6 + channel * 8, 0x09);		// reset all flags and counters   
}

void SetQuadCountMode_7766(int channel, int index)
{
    // See spec for options  
    // MCR0 B1B0  ==  00 v 01 v 10 v 11
    // Use exiting MCR0 value  and reinit with new count mode bits:

    // Set for one channel by index 0-4:
    // Note: ctrReg is our global default set at startup and used in init func as well
    ushort mask = CtrlRegStore;
    ushort B1B0 = 0x0001;

    // 00 is Non quadrature mode which is A and B data are count and direction
    // So this mode wont be appropriate for some encoders
    switch (index)
    {
        case 0:
            B1B0 = 0x00;
            break;
        case 1:
            B1B0 = 0x01;
            break;
        case 2:
            B1B0 = 0x02;
            break;
        case 3:
            B1B0 = 0x03;
            break;
        default:
            B1B0 = 0x00;
            break;
    }

    // MCR0 and MCR1
    mask = CtrlRegStore & ((0xFFFF) << 2);  // clear 2 right bits
    CtrlRegStore = mask | B1B0; // set new value

    apci_write16(apci, 1, 2, channel * 8, CtrlRegStore);   // rewrite control register
    apci_write8(apci, 1, 2, 7 + channel * 8, 0x01); // swap inA and inB reverse direction value: (optional) (CPLD)
    apci_write8(apci, 1, 2, 6 + channel * 8, 0x09); // reset all flags and counters
}
