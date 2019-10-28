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
  __u8 in_byte = 0;
  int status = 0;  
  __u32 cos_regs;

  fd = open("/dev/apci/pcie_idio_24_0", O_RDONLY);

	if (fd < 0)
	{
		printf("Device file could not be opened. Please ensure the iogen driver module is loaded.\n");
		exit(0);
	}

  //undocumented write needed to enable irq
//  apci_write8(fd, 1, 0, 0x69, 0x9);

  status = apci_read32(fd, 1, 2, 8, &cos_regs);
  printf(" read of cos_regs status = %d, cos_regs = 0x%x\n", status, cos_regs);

  //soft reset
  //OutByte( Base + 0x0F, 0 );
  apci_write8(fd, 1, 2, 0xf, 0);

  //set ttl out
  //OutByte( a + 0x0C, ( InByte( a + 0x0C ) & 0x0F ) | 0x02 );
  apci_read8(fd, 1, 2, 0xc, &in_byte);
  apci_write8(fd, 1, 2, 0xc, (in_byte & 0xf) | 0x2);

  //do an initial read of inputs 0-7
  status = apci_read8(fd, 1, 2, 4, &in_byte);
  printf("initial read: status = %d, in_byte = 0x%x\n", status, in_byte);  

  //enable COS
  apci_write8(fd, 1, 2, 0xe, 0xf);

  status = apci_wait_for_irq(fd, 1);

  if (status)
  {
    printf("IRQ didn't occur.\n");
  }
  else
  {
    printf("IRQ did occur.\n");
  }

  status = apci_read32(fd, 1, 2, 8, &cos_regs);
  printf(" read of cos_regs status = %d, cos_regs = 0x%x\n", status, cos_regs);
  
  //do a final read of inputs 0-7
  status = apci_read8(fd, 1, 2, 4, &in_byte);
  printf("final read: status = %d, in_byte = 0x%x\n", status, in_byte);  

#if 0
  //do an initial read of inputs 0-7
  status = apci_read8(fd, 1, 2, 4, &inputs);
  printf("Initial read: status = %d, inputs = 0x%x\n", status, inputs);

  //enable IRQs for inputs 0-7, rising edge
  if (apci_write8(fd, 1, 2, 0xe, 0xff))
  {
    printf("Couldn't enable IRQ\n");
    goto err_out;
  }

  status = apci_read32(fd, 1, 2, 8, &cos_regs);
  printf(" read of cos_regs status = %d, cos_regs = 0x%x\n", status, cos_regs);
  
  status = apci_write32(fd, 1, 2, 8, cos_regs);
  if ( 0 == status)
  {
    printf("Wrote cos_regs\n");
  }
  else
  {
    printf("Couldn't write cos_regs\n");
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
    printf("IRQ did not occurr\n");
  }

  //do a final read of inputs 0-7
  status = apci_read8(fd, 1, 2, 4, &inputs);
  printf("final read: status = %d, inputs = 0x%x\n", status, inputs);  
  
  //Disable IRQ
  apci_write8(fd, 1, 2, 0xe, 0);
#endif 

err_out:
  close(fd);


  return 0;
}