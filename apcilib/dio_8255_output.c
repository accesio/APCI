#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdlib.h>
#include <linux/types.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "apcilib.h"

int fd;

/*
Registers (all hex offsets) +0, +1, +2, +3, +8, +28, +29 are all implemented in both BAR[1] and BAR[2] (although APCI muddies this a little).
Registers +2c, +30, +40, +50, +FC, and all registers at or above +0x100 are implemented only in BAR[1].
Registers +68, +70, +74, and +78 are implemented only in BAR[2].
*/
int main(int argc, char **argv)
{
  __u8 inputs = 0;
  int status = 0;
  int bar = 2;
  int membar = 1;
  int ignored = 0;

  fd = open("/dev/apci/mpcie_dio_24s_0", O_RDWR);

  if (fd < 0)
  {
    printf("Device file could not be opened. Please ensure the iogen driver module is loaded.\n");
    exit(0);
  }

  // Reset card
  status = apci_write8(fd, ignored, membar, 0xFC, 0xFF);
  printf("write 0xfc: status = %d - Reset Card\n", status);

  // Read FPGA revision code
  uint32_t rev_code = 0;
  status = apci_read32(fd, ignored, bar, 0x68, &rev_code);
  printf("read +68: status = %d, rev code = 0x%08x\n", status, rev_code);

  // Write known initial values
  status = apci_write8(fd, ignored, bar, 0, 0);
  printf("write +0 <= 0x00: status = %d\n", status);
  status = apci_write8(fd, ignored, bar, 1, 0);
  printf("write +1 <= 0x00 status = %d\n", status);
  // Set PortA and B to output
  status = apci_write8(fd, ignored, bar, 3, 0x89);
  printf("write +3 <= 0x89: status = %d\n", status);

  while (1)
  {
    status = apci_read8(fd, ignored, bar, 3, &inputs);
    printf("read +3: status = %d, control = 0x%02hhx\n", status, inputs);
    apci_write8(fd, ignored, bar, 0, 1);
    status = apci_read8(fd, 1, bar, 0, &inputs);
    printf("read +0: status = %d, output A = 0x%02hhx\n", status, inputs);
    sleep(1);
    apci_write8(fd, ignored, bar, 0, 2);
    status = apci_read8(fd, ignored, bar, 0, &inputs);
    printf("read +0: status = %d, output A = 0x%02hhx\n", status, inputs);
    sleep(1);
  }
  (void)argc;
  (void)argv;
}