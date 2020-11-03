all: mpcie_quad_8 mpcie-ii-16-irq pcie-idio-24-irq mpcie-dio-24s-irq mpcie_aio16_16f mpcie_aio16_16f_dma

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

clean:
	rm mpcie_quad_8 mpcie-ii-16-irq pcie-idio-24-irq mpcie-dio-24s-irq mpcie_aio16_16f mpcie_aio16_16f_dma
