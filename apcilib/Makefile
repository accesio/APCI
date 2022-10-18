all: mpcie-dio-24a-irq axiodac axiodac_irq pcie_dio_48s mpcie_quad_8 mpcie-ii-16-irq pcie-idio-24-irq mpcie-dio-24s-irq mpcie_aio16_16f_dma mpcie_aio16_16f pcie_iiro_16_irq irq check check_dma dac

ifneq ("$(wildcard /etc/redhat-release)","")
    REDHAT_VERSION := $(shell sed -n 's/.* \([0-9]\+\).*/\1/p' /etc/redhat-release | head -1)
else
    REDHAT_VERSION := 0
endif

NPROC := $(shell nproc 2>/dev/null || echo 1)
ifeq (,$(filter -j%,$(MAKEFLAGS)))
  MAKEFLAGS += -j$(NPROC)
endif

CC   := gcc
CXX  := g++
CFLAGS := -O2 -Wall -Wextra
CXXFLAGS := -O2 -Wall -Wextra
ifeq ($(REDHAT_VERSION),7)
    CFLAGS   += -std=c11 -D_XOPEN_SOURCE=700 -D_BSD_SOURCE
endif

LDFLAGS :=
LDLIBS  := -lpthread -lm

pcie_dio_48s: pcie-dio-48s-irq.c apcilib.c apcilib.h
	$(GCC) -o pcie-dio-48s-irq pcie-dio-48s-irq.c apcilib.c

mpcie_quad_4: mpcie_quad_8.c apcilib.c apcilib.h
	$(GCC) -o mpcie_quad_8 mpcie_quad_8.c apcilib.c

# Pattern rules
%.o: %.c apcilib.h
	$(CC) $(CFLAGS) -c $< -o $@

# C targets (link with CC)
dio_8255_output: dio_8255_output.o $(APCI_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

axiodac: axiodac.o $(APCI_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

axiodac_irq: axiodac_irq.o $(APCI_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

pcie_dio_48s: pcie-dio-48s-irq.o $(APCI_OBJS)
	$(CC) $(LDFLAGS) -o pcie-dio-48s-irq $^ $(LDLIBS)

mpcie_quad_8: mpcie_quad_8.o $(APCI_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

mpcie-dio-24a-irq: mpcie-dio-24a-irq.c apcilib.h apcilib.c
	$(GCC) -o mpcie-dio-24a-irq mpcie-dio-24a-irq.c apcilib.c -lpthread -O3 -lm

mpcie_aio16_16f_dma: mpcie_aio16_16f_dma.c apcilib.h apcilib.c
	$(GCC) -o mpcie_aio16_16f_dma mpcie_aio16_16f_dma.c apcilib.c -lm -lpthread -O3

pcie-idio-24-irq: pcie-idio-24-irq.o $(APCI_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ -lpthread -O3

mpcie-dio-24s-irq: mpcie-dio-24s-irq.o $(APCI_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

mpcie-dio-24x-irq: mpcie-dio-24x-irq.o $(APCI_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

pcie-iiro-16: pcie-iiro-16.o $(APCI_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

irq: irq.o $(APCI_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS) -O3

check: check.o apci_pnp.o $(APCI_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS) -O3

check_dma: check_dma.o apci_pnp.o $(APCI_OBJS)
	$(CC) -g $(LDFLAGS) -o $@ $^ $(LDLIBS) -O3

dac: dac.o $(APCI_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

mpcie_aio16_16f: mpcie_aio16_16f.o $(APCI_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS) -O3

mpcie_aio16_16f_dma: mpcie_aio16_16f_dma.o $(APCI_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS) -O3

# C++ target: compile C++/link with CXX, but still use C object for apcilib
isp-fpga: isp-fpga.cpp $(APCI_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

.PHONY: all clean
clean:
	rm -f mpcie-dio-24a-irq axiodac axiodac_irq pcie-dio-48s-irq pcie-iiro-16 mpcie_quad_8 mpcie-ii-16-irq pcie-idio-24-irq mpcie-dio-24s-irq mpcie_aio16_16f mpcie_aio16_16f_dma check check_dma irq dac

