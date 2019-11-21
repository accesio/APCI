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

  fd = open("/dev/apci/mpcie_dio_24s_0", O_RDWR);

  if (fd < 0)
  {
	printf("Device file could not be opened. Please ensure the iogen driver module is loaded.\n");
	exit(0);
  }

  //do an initial read of inputs
  status = apci_read8(fd, 1, 2, 0, &inputs);
  printf("Initial read: status = %d, inputs A = 0x%x\n", status, inputs);
  status = apci_read8(fd, 1, 2, 1, &inputs);
  printf("Initial read: status = %d, inputs B = 0x%x\n", status, inputs);
  status = apci_read8(fd, 1, 2, 2, &inputs);
  printf("Initial read: status = %d, inputs C = 0x%x\n", status, inputs);
  
  //enable IRQs
  if (status = apci_write8(fd, 1, 2, 40, 0xff))
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
  
  //unmask all ports for CoS IRQs
  if (status = apci_write8(fd, 1, 2, 11, 0))
  {
    printf("Couldn't unmask all ports for CoS IRQs: status = %d\n", status);
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

  //do a final read of inputs
  status = apci_read8(fd, 1, 2, 0, &inputs);
  printf("final read: status = %d, inputs A = 0x%x\n", status, inputs);
  status = apci_read8(fd, 1, 2, 1, &inputs);
  printf("final read: status = %d, inputs B = 0x%x\n", status, inputs); 
  status = apci_read8(fd, 1, 2, 2, &inputs);
  printf("final read: status = %d, inputs C = 0x%x\n", status, inputs);
  
  //mask all ports for CoS IRQs
  apci_write8(fd, 1, 2, 11, 0xff);
  
  //disable IRQ
  apci_write8(fd, 1, 2, 40, 0);

err_out:
  close(fd);


  return 0;
}