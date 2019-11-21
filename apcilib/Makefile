default: all

all: mpcie-ii-16-irq pcie-idio-24-irq mpcie-dio-24s-irq

mpcie-ii-16-irq: mpcie-ii-16-irq.c apcilib.h apcilib.c
	cc -o mpcie-ii-16-irq mpcie-ii-16-irq.c apcilib.c

pcie-idio-24-irq: pcie-idio-24-irq.c apcilib.h apcilib.c
	cc -o pcie-idio-24-irq pcie-idio-24-irq.c apcilib.c

mpcie-dio-24s-irq: mpcie-dio-24s-irq.c apcilib.h apcilib.c
	cc -o mpcie-dio-24s-irq mpcie-dio-24s-irq.c apcilib.c