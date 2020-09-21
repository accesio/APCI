all: mpcie-ii-16-irq pcie-idio-24-irq mpcie-dio-24s-irq mpcie_aio16_16f mpcie_aio16_16f_dma

mpcie-ii-16-irq: mpcie-ii-16-irq.c apcilib.h apcilib.c
	cc -o mpcie-ii-16-irq mpcie-ii-16-irq.c apcilib.c

pcie-idio-24-irq: pcie-idio-24-irq.c apcilib.h apcilib.c
	cc -o pcie-idio-24-irq pcie-idio-24-irq.c apcilib.c

mpcie-dio-24s-irq: mpcie-dio-24s-irq.c apcilib.h apcilib.c
	cc -o mpcie-dio-24s-irq mpcie-dio-24s-irq.c apcilib.c

mpcie_aio16_16f: mpcie_aio16_16f.c apcilib.h apcilib.c
	cc -o mpcie_aio16_16f mpcie_aio16_16f.c apcilib.c

mpcie_aio16_16f_dma: mpcie_aio16_16f_dma.c apcilib.h apcilib.c
	cc -o mpcie_aio16_16f_dma mpcie_aio16_16f_dma.c apcilib.c -lm

clean:
	rm mpcie-ii-16-irq pcie-idio-24-irq mpcie-dio-24s-irq mpcie_aio16_16f mpcie_aio16_16f_dma
