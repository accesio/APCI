all: check check_dma isp-fpga

GCC ?= aarch64-none-linux-gnu-gcc

check: check.c apcilib.h apcilib.c
	$(GCC) -o check check.c apcilib.c -lm -lpthread -O3

check_dma: check_dma.c apcilib.h apcilib.c
	$(GCC) -g -o check_dma check_dma.c apcilib.c -lm -lpthread -O3

isp-fpga: isp-fpga.c apcilib.h apcilib.c
	$(GCC) -o isp-fpga isp-fpga.c apcilib.c -lm

clean:
	rm check check_dma isp-fpga
