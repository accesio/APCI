#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <linux/types.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

#include "apcilib.h"
#include "kbhit.inc"

#define BAR_REGISTER 1

int fd;
pthread_t worker_thread;
int terminated = 0;

__u32 out32(int offset, __u32 value)
{
    printf("out32(%02X, %08X)\n", offset, value);
    return apci_write32(fd, 1, BAR_REGISTER, offset, value);
}
__u32 out8(int offset, __u8 value)
{
    printf("out8(%02X, %02hhX)\n", offset, value);
    return apci_write8(fd, 1, BAR_REGISTER, offset, value);
}
__u32 in8(int offset, __u8 *value)
{
    // printf("in8(%02X)\n", offset);
    return apci_read8(fd, 1, BAR_REGISTER, offset, value);
}
__u32 in32(int offset, __u32 *value)
{
    // printf("in8(%02X)\n", offset);
    return apci_read32(fd, 1, BAR_REGISTER, offset, value);
}

void * worker(void *arg)
{
    printf("Launched background IRQ worker thread\n");
    int status;
    do {
        status = apci_wait_for_irq(fd, 1);
        if (0 == status)
        {
            printf("CoS IRQ Generated\n");
        }
        else
        {
            if (terminated)
                printf("User canceled, aborting worker thread.\n");
            else
                printf("unrecognized error occurred, aborting worker thread.\n");
        }
    } while (!terminated);
}

void abort_handler(int s){
  printf("Caught signal %d\nAborting.\n",s);

  terminated = 1;
  apci_cancel_irq(fd, 1);
  pthread_join(worker_thread, NULL);
  close(fd);
  exit(1);
}


int main (int argc, char **argv)
{
    signal(SIGINT, &abort_handler);
    printf("DIO & CoS IRQ sample for mPCIe-DIO-24A\nPlease disconnect anything sensitive to output changes and press any key.\n");
    do {} while (!kbhit()); // wait for keystroke
    tcflush(0, TCIFLUSH);
    time_t the_time;
    int status = 0;
    __u8 inputData;

#define deviceFileName "/dev/apci/mpcie_dio_24a_0"

    fd = open(deviceFileName, O_RDONLY);

    if (fd < 0)
    {
        printf("Device file %s could not be opened. Please ensure device filname and permissions ('sudo?') and that apci.ko is insmod.\n", deviceFileName);
        exit(0);
    }

    pthread_create(&worker_thread, NULL, &worker, NULL);

    out8(0xFC, 0x04); // BOARD RESET

    printf("Changing Port A to OUTPUT.\n");
    out8(3, 0b10011001);
    out8(0x28, 0xFF); // global enable the IRQ like C# sample does
    out32(0x30, 0x000000FF); // mask Port A CoS IRQ feature; it is an output

    for (int bit = 0; bit < 8; ++bit)
    {
        out32((bit+1) * 0x100 + 0x10, 0xFD); // generate an IRQ on *each* event
        out32((bit+1) * 0x100 + 0x04, 0x00000002); // enable rising and falling edge Events on bit 0
        // out32((bit+1) * 0x100 + 0xC, 1); // generate an IRQ on *each* event
    }

    out32(0x40, 0x1); // clear any pending Event IRQs

    // out8(0x29, 0xFF); // clear IRQ latch like C# sample does just in case

    printf("Testing CoS IRQ on A&C while toggling port B bits.  Press any key to stop waiting.\nWaiting for irq @ %s:\n", ctime(&the_time));

    __u8 pattern = 1;
    do // wait for IRQ
    {
		out8(1, pattern); // write data to output bits 8-15
        pattern <<= 1;
        if (pattern == 0)
            pattern = 1;
        usleep(1000000);
    } while (!kbhit());

    tcflush(0, TCIFLUSH);


    in8(0, &inputData);
    printf(" read of DIO bits 0-7 = %02hhX\n", inputData);

    in8(1, &inputData);
    printf(" read of DIO bits 8-15 = %02hhX\n", inputData);

    in8(2, &inputData);
    printf(" read of DIO bits 16-24 = %02hhX\n", inputData);

    printf("Done. Exiting\n");
err_out:
    terminated = 1;
    apci_cancel_irq(fd, 1);
    pthread_cancel(worker_thread);
    out8(0xFC,0x04);// BOARD RESET
    close(fd);

    return 0;
}