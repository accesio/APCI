#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <linux/types.h>
#include <unistd.h>
#include <curses.h>
#include "apcilib.h"
#include "kbhit.inc"

int apci;
int terminated = 0;

int main (int argc, char **argv)
{ 
    int status = 0;  
    __u8 relayData;
    __u8 inputData;

#define deviceFileName "/dev/apci/mpcie_iiro_8_0"

    apci = open(deviceFileName, O_RDONLY);

    if (apci < 0)
    {
        printf("Device file %s could not be opened. Please ensure device filname and permissions ('sudo?') and that apci.ko is insmod.\n", deviceFileName);
        exit(0);
    }

    apci_read8(apci, 1, 2, 0, &relayData);
    printf(" read of relay bits 0-7 = %02hhX\n", relayData);

    apci_read8(apci, 1, 2, 1, &inputData);
    printf(" read of input bits 0-7 = %02hhX\n", inputData);
    
    printf("\nPress any number 0 through 7 to toggle that relay. SPACE to READ.  Press ESC to exit.\n");

    do
    {
        char key = 0;
        if (kbhit())
             key = getchar();
        
        if (('0' <= key)&& (key <='7'))
        {
            printf("\nToggling relay #%c\n", key);
            relayData ^= 1<< (key-'0');
            apci_write8(apci, 1, 2, 0, relayData);
            key=' ';
        }
        
        if (key == 27) terminated = 1;

        if (key==' ')
        {
            apci_read8(apci, 1, 2, 1, &inputData);
            apci_read8(apci, 1, 2, 0, &relayData);
            printf(" read of relay bits = %02hhX;  inputs = %02hhX\n", relayData, inputData);
        }

    }while (!terminated);

    terminated = 1;

err_out:
  close(apci);

  return 0;
}