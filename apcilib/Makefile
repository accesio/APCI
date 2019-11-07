default: all

all: tester mpcie-ii-16-irq

tester: tester.c apci.h apcilib.c apcilib.h
	cc -o tester tester.c apcilib.c


mpcie-ii-16-irq: mpcie-ii-16-irq.c apcilib.h apcilib.c
	cc -o mpcie-ii-16-irq mpcie-ii-16-irq.c apcilib.c

