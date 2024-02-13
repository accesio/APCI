#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <linux/types.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>

#include "apcilib.h"

int fd;
pthread_t worker_thread;
static int terminate = 0;
int IRQCount = 0;
void abort_handler(int s);
void *worker_main(void *arg);

int main (int argc, char **argv)
{
  __u8 in_byte = 0;
  int status = 0;
  int oIRQCount = 0;

  fd = open("/dev/apci/pcie_idio_24_0", O_RDONLY);

	if (fd < 0)
	{
		printf("Device file could not be opened. Please ensure the iogen driver module is loaded.\n");
		exit(0);
	}
  struct sigaction sigIntHandler;

  sigIntHandler.sa_handler = abort_handler;
  sigemptyset(&sigIntHandler.sa_mask);
  sigIntHandler.sa_flags = 0;
  sigaction(SIGINT, &sigIntHandler, NULL);
  sigaction(SIGABRT, &sigIntHandler, NULL);


  pthread_create(&worker_thread, NULL, &worker_main, NULL);

  //soft reset
  //OutByte( Base + 0x0F, 0 );
  apci_write8(fd, 1, 2, 0xf, 0);


  //set ttl out
  //OutByte( a + 0x0C, ( InByte( a + 0x0C ) & 0x0F ) | 0x02 );
  apci_read8(fd, 1, 2, 0xc, &in_byte);
  apci_write8(fd, 1, 2, 0xc, (in_byte & 0xf) | 0x2);

  //do an initial read of inputs 0-7
  status = apci_read8(fd, 1, 2, 4, &in_byte);
  printf("Reading input bits 0-7: status = %d, in_byte = 0x%x\n", status, in_byte);

  //enable COS
  apci_write8(fd, 1, 2, 0xe, 0xf);

  printf("This sample will now generate random IRQs by occasionally writing random data to bits 0-7\n");
  printf("if you have looped output bits 0-7 to input bits you should see a couple of IRQs/second depending on CPU speed.\n");
  // note: this timing method is silly.  it should be "every one second" or something, but I only *know* how to do that in C++
  printf("If you have not looped bits back, generate changes of state on the inputs, now.\n");
  printf("Press CTRL-C to exit the sample.\n");
  while (!terminate)
  {
    if (IRQCount != oIRQCount)
    {
      oIRQCount = IRQCount;
      printf("IRQs detected: %d\n", oIRQCount);
    }
    if (rand() % 10000000 == 0)
    {
      apci_write8(fd, 1, 2, 0, rand() % 256);
    }
  }

   apci_cancel_irq(fd, 1);
   pthread_join(worker_thread, NULL);

  //do a final read of inputs 0-7
  status = apci_read8(fd, 1, 2, 4, &in_byte);
  printf("Reading input bits 0-7.\n");
  printf("final read: status = %d, in_byte = 0x%x\n", status, in_byte);

err_out:
  close(fd);


  return 0;
}


/* Background thread to wait for IRQs */
void *worker_main(void *arg)
{
    do
    {
        int status = apci_wait_for_irq(fd, 1);
        if (status < 0)
        {
            printf("Error waiting for IRQ: %d\n", status);
            break;
        }

        IRQCount++;
    } while (!terminate);

    printf("  Worker Thread: exiting.\n");
}

void abort_handler(int s)
{
    terminate = 1;
}
