#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <linux/types.h>
#include <unistd.h>
#include <signal.h>

#include "apcilib.h"

int fd;


int main (int argc, char **argv)
{
  __u8 inputs = 0;
  int status = 0;  

  fd = open("/dev/apci/mpcie_ii_16_0", O_RDONLY);

	if (fd < 0)
	{
		printf("Device file could not be opened. Please ensure the iogen driver module is loaded.\n");
		exit(0);
	}

  //do an initial read of inputs
  status = apci_read8(fd, 1, 2, 0, &inputs);
  printf("Initial read: status = %d, inputs = 0x%x\n", status, inputs);

  //enable IRQs
  if (apci_write8(fd, 1, 2, 40, 0xff))
  {
    printf("Couldn't enable IRQ\n");
    goto err_out;
  }

  //wait for IRQ
  printf("Waiting for irq\n");
  status = apci_wait_for_irq(fd, 0);

  if (0 == status)
  {
    printf("IRQ occurred\n");
  }
  else
  {
    printf("IRQ did not occur\n");
  }

  //do a final read initial read of inputs
  status = apci_read8(fd, 1, 2, 0, &inputs);
  printf("final read: status = %d, inputs = 0x%x\n", status, inputs);  
  
  //Disable IRQ
  apci_write8(fd, 1, 2, 40, 0);

err_out:
  close(fd);


  return 0;
}