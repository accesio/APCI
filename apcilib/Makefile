all: mpcie_quad_8 mpcie-ii-16-irq pcie-idio-24-irq mpcie-dio-24s-irq mpcie_aio16_16f mpcie_aio16_16f_dma pcie_iiro_16_irq irq

irq: irq.c apcilib.h apcilib.c
	gcc -o irq irq.c apcilib.c -lm -lpthread -O3

mpcie-ii-16-irq: mpcie-ii-16-irq.c apcilib.h apcilib.c
	gcc -o mpcie-ii-16-irq mpcie-ii-16-irq.c apcilib.c

pcie-idio-24-irq: pcie-idio-24-irq.c apcilib.h apcilib.c
	gcc -o pcie-idio-24-irq pcie-idio-24-irq.c apcilib.c

mpcie-dio-24s-irq: mpcie-dio-24s-irq.c apcilib.h apcilib.c
	gcc -o mpcie-dio-24s-irq mpcie-dio-24s-irq.c apcilib.c

mpcie_aio16_16f: mpcie_aio16_16f.c apcilib.h apcilib.c
	gcc -o mpcie_aio16_16f mpcie_aio16_16f.c apcilib.c

mpcie_aio16_16f_dma: mpcie_aio16_16f_dma.c apcilib.h apcilib.c
	gcc -o mpcie_aio16_16f_dma mpcie_aio16_16f_dma.c apcilib.c -lm -lpthread -O3

mpcie_quad_4:
	gcc -o mpcie_quad_8 mpcie_quad_8.c apcilib.c

mpcie_quad_8: mpcie_quad_8.c apcilib.h apcilib.c
	gcc -o mpcie_quad_8 mpcie_quad_8.c apcilib.c

pcie_iiro_16_irq: pcie-iiro-16.c apcilib.h apcilib.c
	gcc -o pcie-iiro-16 pcie-iiro-16.c apcilib.c -lm -lpthread -O3

clean:
	rm pcie-iiro-16 mpcie_quad_8 mpcie-ii-16-irq pcie-idio-24-irq mpcie-dio-24s-irq mpcie_aio16_16f mpcie_aio16_16f_dma irq
