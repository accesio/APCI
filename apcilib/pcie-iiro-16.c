#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <linux/types.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include "apcilib.h"
#include "kbhit.inc"

int fd;
pthread_t worker_thread;
int terminated = 0;


void * worker(void *arg)
{ 
    int status;
    __u8 inputs = 0;
    do {
        status = apci_wait_for_irq(fd, 1); 
        if (0 == status)
        {
            printf("IRQ occurred: ");
                //do a final read of inputs
            status = apci_read8(fd, 1, 2, 0, &inputs);
            printf("in0-7 = 0x%hhx   ", inputs);
            status = apci_read8(fd, 1, 2, 1, &inputs);
            printf("in8-15 = 0x%hhx\n", inputs);
        }
        else
        {
            printf("IRQ did not occur. Aborting.\n");
            terminated = 1;
        }
    } while (!terminated);

}

void abort_handler(int s){
  printf("Caught signal %d\n",s);


  terminated = 1;
  pthread_join(worker_thread, NULL);
  exit(1);
}

int main (int argc, char **argv)
{
    time_t the_time;
    int status = 0;  
    __u8 relayData;
    __u8 inputData;
    struct sigaction sigIntHandler;

    sigIntHandler.sa_handler = abort_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;

    sigaction(SIGINT, &sigIntHandler, NULL);
    sigaction(SIGABRT, &sigIntHandler, NULL);

#define deviceFileName "/dev/apci/pcie_iiro_16_0"

    fd = open(deviceFileName, O_RDONLY);

    if (fd < 0)
    {
        printf("Device file %s could not be opened. Please ensure device filname and permissions ('sudo?') and that apci.ko is insmod.\n", deviceFileName);
        exit(0);
    }

    apci_read8(fd, 1, 2, 0, &relayData);
    printf(" read of relay bits 0-7 = %02hhX\n", relayData);

    apci_read8(fd, 1, 2, 4, &relayData);
    printf(" read of relay bits 8-15 = %02hhX\n", relayData);

    apci_read8(fd, 1, 2, 1, &inputData);
    printf(" read of input bits 0-7 = %02hhX\n", inputData);

    apci_read8(fd, 1, 2, 5, &inputData);
    printf(" read of input bits 8-15= %04hhX\n", inputData);
   

    pthread_create(&worker_thread, NULL, &worker, NULL);
    
    time(&the_time);
    printf("Testing CoS IRQ.  Press any key to exit.\nWaiting for irq @ %s: Please toggle any input bit.\n", ctime(&the_time));
    apci_read8(fd, 1, 2, 2, &inputData); //enable COS

    do     // wait for IRQ
    {
        printf(".");sleep(1);
    }while (!kbhit());

    terminated = 1;
    apci_cancel_irq(fd, 1);

    apci_read8(fd, 1, 2, 1, &inputData);
    printf(" read of input bits 0-7 = %02hhX\n", inputData);

    apci_read8(fd, 1, 2, 5, &inputData);
    printf(" read of input bits 8-15= %04hhX\n", inputData);

err_out:
  close(fd);

  return 0;
}