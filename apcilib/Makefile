all: axiodac axiodac_irq pcie_dio_48s mpcie_quad_8 mpcie-ii-16-irq pcie-idio-24-irq mpcie-dio-24s-irq mpcie_aio16_16f_dma mpcie_aio16_16f pcie_iiro_16_irq irq check check_dma dac

GCC ?= aarch64-none-linux-gnu-gcc

axiodac_irq: axiodac_irq.c apcilib.h apcilib.c
	$(GCC) -o axiodac_irq axiodac_irq.c apcilib.c -lm -lpthread

axiodac: axiodac.c apcilib.h apcilib.c
	$(GCC) -o axiodac axiodac.c apcilib.c -lm -lpthread -O3

pcie_dio_48s:
	$(GCC) -o pcie-dio-48s-irq pcie-dio-48s-irq.c apcilib.c

mpcie_quad_4:
	$(GCC) -o mpcie_quad_8 mpcie_quad_8.c apcilib.c

mpcie_quad_8: mpcie_quad_8.c apcilib.h apcilib.c
	$(GCC) -o mpcie_quad_8 mpcie_quad_8.c apcilib.c

pcie_iiro_16_irq: pcie-iiro-16.c apcilib.h apcilib.c
	$(GCC) -o pcie-iiro-16 pcie-iiro-16.c apcilib.c -lm -lpthread -O3

irq: irq.c apcilib.h apcilib.c
	$(GCC) -o irq irq.c apcilib.c -lm -lpthread -O3

mpcie-ii-16-irq: mpcie-ii-16-irq.c apcilib.h apcilib.c
	$(GCC) -o mpcie-ii-16-irq mpcie-ii-16-irq.c apcilib.c

pcie-idio-24-irq: pcie-idio-24-irq.c apcilib.h apcilib.c
	$(GCC) -o pcie-idio-24-irq pcie-idio-24-irq.c apcilib.c

mpcie-dio-24s-irq: mpcie-dio-24s-irq.c apcilib.h apcilib.c
	$(GCC) -o mpcie-dio-24s-irq mpcie-dio-24s-irq.c apcilib.c

mpcie_aio16_16f_dma: mpcie_aio16_16f_dma.c apcilib.h apcilib.c
	$(GCC) -o mpcie_aio16_16f_dma mpcie_aio16_16f_dma.c apcilib.c -lm -lpthread -O3

mpcie_aio16_16f: mpcie_aio16_16f.c apcilib.h apcilib.c
	$(GCC) -o mpcie_aio16_16f mpcie_aio16_16f.c apcilib.c -lm -lpthread -O3

check: check.c apcilib.h apcilib.c
	$(GCC) -o check check.c apcilib.c -lm -lpthread -O3

check_dma: check_dma.c apcilib.h apcilib.c
	$(GCC) -g -o check_dma check_dma.c apcilib.c -lm -lpthread -O3

dac: dac.c apcilib.h apcilib.c
	$(GCC) -o dac dac.c apcilib.c


clean:
	rm axiodac axiodac_irq pcie-dio-48s-irq pcie-iiro-16 mpcie_quad_8 mpcie-ii-16-irq pcie-idio-24-irq mpcie-dio-24s-irq mpcie_aio16_16f mpcie_aio16_16f_dma check check_dma irq dac

