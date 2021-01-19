<<<<<<< HEAD
<<<<<<< HEAD
all: mpcie_quad_8 mpcie-ii-16-irq pcie-idio-24-irq mpcie-dio-24s-irq mpcie_aio16_16f mpcie_aio16_16f_dma
=======
all: mpcie_quad_8 mpcie-ii-16-irq pcie-idio-24-irq mpcie-dio-24s-irq mpcie_aio16_16f mpcie_aio16_16f_dma pcie_iiro_16_irq irq
=======
all: mpcie_quad_8 mpcie-ii-16-irq pcie-idio-24-irq mpcie-dio-24s-irq mpcie_aio16_16f mpcie_adio16_8f mpcie_aio16_16f_dma mpcie_adio16_8f_dma pcie_iiro_16_irq irq check check_dma


mpcie_quad_4:
	gcc -o mpcie_quad_8 mpcie_quad_8.c apcilib.c

mpcie_quad_8: mpcie_quad_8.c apcilib.h apcilib.c
	gcc -o mpcie_quad_8 mpcie_quad_8.c apcilib.c

pcie_iiro_16_irq: pcie-iiro-16.c apcilib.h apcilib.c
	gcc -o pcie-iiro-16 pcie-iiro-16.c apcilib.c -lm -lpthread -O3
>>>>>>> 69d26fa... DMA and related updates to samples

irq: irq.c apcilib.h apcilib.c
	gcc -o irq irq.c apcilib.c -lm -lpthread -O3
>>>>>>> 2b7a05c... Added PCIe-IIRO-16 CoS sample

mpcie-ii-16-irq: mpcie-ii-16-irq.c apcilib.h apcilib.c
	gcc -o mpcie-ii-16-irq mpcie-ii-16-irq.c apcilib.c

pcie-idio-24-irq: pcie-idio-24-irq.c apcilib.h apcilib.c
	gcc -o pcie-idio-24-irq pcie-idio-24-irq.c apcilib.c

mpcie-dio-24s-irq: mpcie-dio-24s-irq.c apcilib.h apcilib.c
	gcc -o mpcie-dio-24s-irq mpcie-dio-24s-irq.c apcilib.c

mpcie_aio16_16f: mpcie_aio16_16f.c apcilib.h apcilib.c
	gcc -o mpcie_aio16_16f mpcie_aio16_16f.c apcilib.c

mpcie_adio16_8f: mpcie_adio16_8f.c apcilib.h apcilib.c
	gcc -o mpcie_adio16_8f mpcie_adio16_8f.c apcilib.c -lm -lpthread -O3

mpcie_aio16_16f_dma: mpcie_aio16_16f_dma.c apcilib.h apcilib.c
	gcc -o mpcie_aio16_16f_dma mpcie_aio16_16f_dma.c apcilib.c -lm -lpthread -O3

<<<<<<< HEAD
<<<<<<< HEAD

mpcie_quad_4:
	gcc -o mpcie_quad_8 mpcie_quad_8.c apcilib.c
=======
mpcie_adio16_8f_dma: mpcie_adio16_8f_dma.c apcilib.h apcilib.c
	gcc -g -o mpcie_adio16_8f_dma mpcie_adio16_8f_dma.c apcilib.c -lm -lpthread -O3
>>>>>>> 69d26fa... DMA and related updates to samples

check:
	gcc -o check check.c apcilib.c -lm -lpthread -O3

check_dma:
	gcc -g -o check_dma check_dma.c apcilib.c -lm -lpthread -O3

<<<<<<< HEAD
clean:
	rm mpcie_quad_8 mpcie-ii-16-irq pcie-idio-24-irq mpcie-dio-24s-irq mpcie_aio16_16f mpcie_aio16_16f_dma
=======
pcie_iiro_16_irq: pcie-iiro-16.c apcilib.h apcilib.c
	gcc -o pcie-iiro-16 pcie-iiro-16.c apcilib.c -lm -lpthread -O3
=======
>>>>>>> 69d26fa... DMA and related updates to samples

mpcie_quad_4:
	gcc -o mpcie_quad_8 mpcie_quad_8.c apcilib.c

mpcie_quad_8: mpcie_quad_8.c apcilib.h apcilib.c
	gcc -o mpcie_quad_8 mpcie_quad_8.c apcilib.c

clean:
<<<<<<< HEAD
	rm mpcie_quad_8 pcie-iiro-16 mpcie-ii-16-irq pcie-idio-24-irq mpcie-dio-24s-irq mpcie_aio16_16f mpcie_aio16_16f_dma irq
>>>>>>> 2b7a05c... Added PCIe-IIRO-16 CoS sample
=======
	rm pcie-iiro-16 mpcie_quad_8 mpcie-ii-16-irq pcie-idio-24-irq mpcie-dio-24s-irq mpcie_aio16_16f mpcie_adio16_8f mpcie_aio16_16f_dma mpcie_adio16_8f_dma irq check check_dma
>>>>>>> 69d26fa... DMA and related updates to samples
