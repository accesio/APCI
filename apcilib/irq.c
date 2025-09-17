#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <linux/types.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

#include "apcilib.h"

#include "kbhit.inc"

int fd;
pthread_t worker_thread;
int terminated = 0;

void *worker(void *arg)
{
  int status;
  __u8 inputs = 0;
  do
  {
    status = apci_wait_for_irq(fd, 1);
    if (0 == status)
    {
      printf("IRQ occurred: ");
      // do a final read of inputs
      status = apci_read8(fd, 1, 2, 0, &inputs);
      printf("A = 0x%x   ", inputs);
      status = apci_read8(fd, 1, 2, 1, &inputs);
      printf("B = 0x%x   ", inputs);
      status = apci_read8(fd, 1, 2, 2, &inputs);
      printf("C = 0x%x\n", inputs);
    }
    else
    {
      printf("IRQ did not occur. Aborting.\n");
      terminated = 1;
    }
  } while (!terminated);
  return 0;
  (void)arg;
}

void abort_handler(int s)
{
  printf("Caught signal %d\n", s);

  terminated = 1;
  pthread_join(worker_thread, NULL);
  exit(1);
}

int main(int argc, char **argv)
{
  __u8 inputs = 0;
  int status = 0;
  time_t the_time;
  struct sigaction sigIntHandler;

  sigIntHandler.sa_handler = abort_handler;
  sigemptyset(&sigIntHandler.sa_mask);
  sigIntHandler.sa_flags = 0;

  sigaction(SIGINT, &sigIntHandler, NULL);
  sigaction(SIGABRT, &sigIntHandler, NULL);

  fd = open("/dev/apci/pcie_dio_24s_0", O_RDWR);

  if (fd < 0)
  {
    printf("Device file could not be opened. Please ensure the apci driver module is loaded.\n");
    exit(0);
  }

  status = apci_write8(fd, 1, 2, 3, 0x80); // all outputs

  // do an initial read of inputs
  status = apci_read8(fd, 1, 2, 0, &inputs);
  printf("Initial read: status = %d, inputs A = 0x%x\n", status, inputs);
  status = apci_read8(fd, 1, 2, 1, &inputs);
  printf("Initial read: status = %d, inputs B = 0x%x\n", status, inputs);
  status = apci_read8(fd, 1, 2, 2, &inputs);
  printf("Initial read: status = %d, inputs C = 0x%x\n", status, inputs);
  status = apci_read8(fd, 1, 2, 3, &inputs);
  printf("Initial read: status = %d, control C = 0x%x\n", status, inputs);

  // enable IRQs
  if (status = apci_write8(fd, 1, 2, 11, 0xFF)) // disable CoS but won't disable C3 if IENx is installed
  {
    printf("Couldn't enable IRQ: status = %d\n", status);
    goto err_out;
  }

  // clear pending IRQs
  if (status = apci_write8(fd, 1, 2, 0x0F, 0xFF))
  {
    printf("Couldn't clear pending IRQs: status = %d\n", status);
    goto err_out;
  }

  pthread_create(&worker_thread, NULL, &worker, NULL);

  time(&the_time);
  printf("Testing C3 IRQ in OUTPUT mode: 1Hz toggle of C3 bit.\nPress any key to exit.\nWaiting for irq @ %s", ctime(&the_time));
  // wait for IRQ
  do
  {
    sleep(1);
    printf("Lowering output C3\n");
    apci_write8(fd, 1, 2, 2, 0x00);
    sleep(1);
    printf("Raising output C3 - should get IRQ\n");
    apci_write8(fd, 1, 2, 2, 0x08); // trigger C3 output IRQ
  } while (!kbhit());

  getchar();

  time(&the_time);
  status = apci_write8(fd, 1, 2, 3, 0x9B); // all Inputs
  printf("Testing C3 IRQ in INPUT mode.  TOGGLE C3 to generate IRQs.\nPress any key to exit.\nWaiting for irq @ %s", ctime(&the_time));
  // wait for IRQ
  do
  {

  } while (!kbhit());

  printf("Done.");

  terminated = 1;
  pthread_join(worker_thread, NULL);

err_out:
  close(fd);

  return 0;
  (void)argc;
  (void)argv;
}
