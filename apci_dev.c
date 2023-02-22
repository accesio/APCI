/*
 * ACCES I/O APCI Linux driver
 *
 * Copyright 1998-2013 Jimi Damon <jdamon@accesio.com>
 *
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/sort.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/idr.h>
#include <linux/version.h>
#include <linux/list.h>

#include "apci_common.h"
#include "apci_dev.h"
#include "apci_fops.h"

#ifndef __devinit
#define __devinit
#define __devinitdata
#endif

#define mPCIe_ADIO_IRQStatusAndClearOffset (0x40)
#define mPCIe_ADIO_IRQEventMask (0xffff0000)
#define bmADIO_FAFIRQStatus (1 << 20)
#define bmADIO_DMADoneStatus (1 << 18)
#define bmADIO_DMADoneEnable (1 << 2)
#define bmADIO_ADCTRIGGERStatus (1 << 16)
#define bmADIO_ADCTRIGGEREnable (1 << 0)

/* PCI table construction */
static struct pci_device_id ids[] = {
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_IIRO_8),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_DIO_24D),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DA16_16),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DA16_8),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DA16_6),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DA16_4),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DA16_2),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DA12_16),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DA12_8),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DA12_6),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DA12_4),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DA12_2),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_DA12_16),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_DA12_8),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_DA12_6),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_DA12_4),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_DA12_2),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, P104_DIO_48S),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, P104_DIO_96),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_DIO_120),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_DIO_24D),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_DIO_24D_C),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_DIO_24H),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_DIO_24H_C),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_DIO_24S),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_DIO_48),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_DIO_48H),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_DIO_48HS),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_DIO_48S),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_DIO_72),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_DIO_96),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_DIO_96C3),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_DIO_96CT),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DIO_24),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DIO_24D),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DIO_24DC),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DIO_24DCS),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DIO_24DS),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DIO_24S),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DIO_48),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DIO_48S),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_IIRO_16),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_IIRO_16),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_IIRO_8),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_IIRO_8),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, MPCIE_DIO_24S),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, MPCIE_DIO_24S_R1),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, MPCIE_IDIO_8),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, MPCIE_IIRO_8),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, MPCIE_IDIO_4),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, MPCIE_IIRO_4),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, MPCIE_IDO_8),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, MPCIE_RO_8),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, MPCIE_II_16),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, MPCIE_II_8),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, MPCIE_II_4),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, MPCIE_QUAD_4),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, MPCIE_QUAD_8),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_QUAD_8),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_QUAD_4),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, MPCIE_DIO_24),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_IDIO_12),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_IDIO_24),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_ADIO16_16FDS),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_ADIO16_16F),
	},

	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_ADIO16_16A),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_ADIO16_16E),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_ADI16_16F),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_ADI16_16A),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_ADI16_16E),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_ADIO12_16A),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_ADIO12_16),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_ADIO12_16E),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_ADI12_16A),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_ADI12_16),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_ADI12_16E),
	},

	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO16_16FDS),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO16_16F),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO16_16A),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO16_16E),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AI16_16F),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AI16_16A),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AI16_16E),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO12_16A),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO12_16),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO12_16E),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AI12_16A),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AI12_16),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AI12_16E),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO16_16F_proto),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO16_16A_proto),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO16_16E_proto),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AI16_16F_proto),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AI16_16A_proto),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AI16_16E_proto),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO12_16A_proto),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO12_16_proto),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO12_16E_proto),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AI12_16A_proto),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AI12_16_proto),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_AI12_16E_proto),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_ADIO16_8FDS),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_ADIO16_8F),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_ADIO16_8A),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_ADIO16_8E),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_ADI16_8F),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_ADI16_8A),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_ADI16_8E),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_ADIO12_8A),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_ADIO12_8),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_ADIO12_8E),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_ADI12_8A),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_ADI12_8),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, mPCIe_ADI12_8E),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DIO_24HC),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_AI12_16_),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_AI12_16),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_AI12_16A),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCI_A12_16A),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DIO_72),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DIO_96),
	},
	{
		PCI_DEVICE(A_VENDOR_ID, PCIe_DIO_120),
	},
	{
		0,
	}
};
MODULE_DEVICE_TABLE(pci, ids);

struct apci_lookup_table_entry {
	int board_id;
	int counter;
	u8 relay_bytes;
	u8 relay_offsets[16];
	u8 input_bytes;
	u8 input_offsets[16];
	char *name;
};

/* Driver naming section */
#define BRACED_INIT_LIST(...) \
	{                     \
		__VA_ARGS__   \
	}
#define APCI_MAKE_ENTRY(X, RELAY_BYTES, RELAY, INPUT_BYTES, INPUT)      \
	{                                                               \
		X, 0, RELAY_BYTES, BRACED_INIT_LIST RELAY, INPUT_BYTES, \
			BRACED_INIT_LIST INPUT, NAME_##X                \
	}

#define APCI_MAKE_DEFAULT_ENTRY(X)                 \
	{                                          \
		X, 0, 0, { 0 }, 0, { 0 }, NAME_##X \
	}

#define APCI_MAKE_DRIVER_TABLE(...) \
	{                           \
		__VA_ARGS__         \
	}
/* acpi_lookup_table */

static int te_sort(const void *m1, const void *m2)
{
	struct apci_lookup_table_entry *mi1 =
		(struct apci_lookup_table_entry *)m1;
	struct apci_lookup_table_entry *mi2 =
		(struct apci_lookup_table_entry *)m2;
	return (mi1->board_id < mi2->board_id ? -1 :
						mi1->board_id != mi2->board_id);
}

#if 0
static struct apci_lookup_table_entry apci_driver_table[] = {
  { PCIe_IIRO_8, 0 , "iiro8" } ,
  { PCI_DIO_24D, 0 , "pci24d" },
  [0]
};

int APCI_LOOKUP_ENTRY(int x ) {
  switch(x) {
  case PCIe_IIRO_8:
    return 0;
  case PCI_DIO_24D:
    return 1;
  default:
    return -1;
  }
}

#define APCI_TABLE_SIZE \
	sizeof(apci_driver_table) / sizeof(struct apci_lookup_table_entry)
#define APCI_TABLE_ENTRY_SIZE sizeof(struct apci_lookup_table_entry)

#else
// clang-format off
static struct apci_lookup_table_entry apci_driver_table[] =
    APCI_MAKE_DRIVER_TABLE(
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DIO_24),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DIO_24D),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DIO_24S),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DIO_24DS),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DIO_24DC),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DIO_24DCS),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DIO_48),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DIO_48S),
        APCI_MAKE_ENTRY(PCIe_IIRO_8,1,(0),1,(1)),
        APCI_MAKE_ENTRY(PCIe_IIRO_16,2,(0,4),2,(1,5)),
        APCI_MAKE_DEFAULT_ENTRY(PCI_DIO_24H),
        APCI_MAKE_DEFAULT_ENTRY(PCI_DIO_24D),
        APCI_MAKE_DEFAULT_ENTRY(PCI_DIO_24H_C),
        APCI_MAKE_DEFAULT_ENTRY(PCI_DIO_24D_C),
        APCI_MAKE_DEFAULT_ENTRY(PCI_DIO_24S),
        APCI_MAKE_DEFAULT_ENTRY(PCI_DIO_48),
        APCI_MAKE_DEFAULT_ENTRY(PCI_DIO_48S),
        APCI_MAKE_DEFAULT_ENTRY(P104_DIO_48S),
        APCI_MAKE_DEFAULT_ENTRY(PCI_DIO_48H),
        APCI_MAKE_DEFAULT_ENTRY(PCI_DIO_48HS),
        APCI_MAKE_DEFAULT_ENTRY(PCI_DIO_72),
        APCI_MAKE_DEFAULT_ENTRY(P104_DIO_96),
        APCI_MAKE_DEFAULT_ENTRY(PCI_DIO_96),
        APCI_MAKE_DEFAULT_ENTRY(PCI_DIO_96CT),
        APCI_MAKE_DEFAULT_ENTRY(PCI_DIO_96C3),
        APCI_MAKE_DEFAULT_ENTRY(PCI_DIO_120),
        APCI_MAKE_DEFAULT_ENTRY(PCI_AI12_16),
        APCI_MAKE_DEFAULT_ENTRY(PCI_AI12_16_),
        APCI_MAKE_DEFAULT_ENTRY(PCI_AI12_16A),
        APCI_MAKE_DEFAULT_ENTRY(PCI_AIO12_16),
        APCI_MAKE_DEFAULT_ENTRY(PCI_A12_16A),
        APCI_MAKE_DEFAULT_ENTRY(LPCI_A16_16A),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DA16_16),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DA16_8),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DA16_6),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DA16_4),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DA16_2),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DA12_16),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DA12_8),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DA12_6),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DA12_4),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DA12_2),
        APCI_MAKE_DEFAULT_ENTRY(PCI_DA12_16),
        APCI_MAKE_DEFAULT_ENTRY(PCI_DA12_8),
        APCI_MAKE_DEFAULT_ENTRY(PCI_DA12_6),
        APCI_MAKE_DEFAULT_ENTRY(PCI_DA12_4),
        APCI_MAKE_DEFAULT_ENTRY(PCI_DA12_2),
        APCI_MAKE_DEFAULT_ENTRY(PCI_DA12_16V),
        APCI_MAKE_DEFAULT_ENTRY(PCI_DA12_8V),
        APCI_MAKE_ENTRY(LPCI_IIRO_8,1,(0),1,(1)),
        APCI_MAKE_ENTRY(PCI_IIRO_8,1,(0),1,(1)),
        APCI_MAKE_ENTRY(PCI_IIRO_16,2,(0, 4),2,(1, 5)),
        APCI_MAKE_DEFAULT_ENTRY(PCI_IDI_48),
        APCI_MAKE_DEFAULT_ENTRY(PCI_IDO_48),
        APCI_MAKE_DEFAULT_ENTRY(PCI_IDIO_16),
        APCI_MAKE_DEFAULT_ENTRY(PCI_WDG_2S),
        APCI_MAKE_DEFAULT_ENTRY(PCI_WDG_CSM),
        APCI_MAKE_DEFAULT_ENTRY(PCI_WDG_IMPAC),
        APCI_MAKE_DEFAULT_ENTRY(MPCIE_DIO_24S),
        APCI_MAKE_DEFAULT_ENTRY(MPCIE_DIO_24S_R1),
        APCI_MAKE_DEFAULT_ENTRY(MPCIE_IDIO_8),
        APCI_MAKE_ENTRY(MPCIE_IIRO_8,1,(0),1,(1)),
        APCI_MAKE_DEFAULT_ENTRY(MPCIE_IDIO_4),
        APCI_MAKE_DEFAULT_ENTRY(MPCIE_IIRO_4),
        APCI_MAKE_DEFAULT_ENTRY(MPCIE_IDO_8),
        APCI_MAKE_DEFAULT_ENTRY(MPCIE_RO_8),
        APCI_MAKE_DEFAULT_ENTRY(MPCIE_II_16),
        APCI_MAKE_DEFAULT_ENTRY(MPCIE_II_8),
        APCI_MAKE_DEFAULT_ENTRY(MPCIE_II_4),
        APCI_MAKE_DEFAULT_ENTRY(MPCIE_QUAD_4),
        APCI_MAKE_DEFAULT_ENTRY(MPCIE_QUAD_8),

        APCI_MAKE_DEFAULT_ENTRY(PCI_QUAD_8),
        APCI_MAKE_DEFAULT_ENTRY(PCI_QUAD_4),


        APCI_MAKE_DEFAULT_ENTRY(MPCIE_DIO_24),
        APCI_MAKE_DEFAULT_ENTRY(PCI_WDG_IMPAC),
        // APCI_MAKE_DEFAULT_ENTRY( MPCIE_DIO_24S ),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_IDIO_12),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_IDIO_24),

        APCI_MAKE_DEFAULT_ENTRY(PCIe_ADIO16_16FDS),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_ADIO16_16F),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_ADIO16_16A),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_ADIO16_16E),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_ADI16_16F),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_ADI16_16A),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_ADI16_16E),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_ADIO12_16A),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_ADIO12_16),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_ADIO12_16E),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_ADI12_16A),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_ADI12_16),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_ADI12_16E),

        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AIO16_16FDS),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AIO16_16F),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AIO16_16A),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AIO16_16E),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AI16_16F),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AI16_16A),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AI16_16E),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AIO12_16A),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AIO12_16),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AIO12_16E),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AI12_16A),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AI12_16),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AI12_16E),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AIO16_16F_proto),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AIO16_16A_proto),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AIO16_16E_proto),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AI16_16F_proto),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AI16_16A_proto),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AI16_16E_proto),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AIO12_16A_proto),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AIO12_16_proto),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AIO12_16E_proto),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AI12_16A_proto),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AI12_16_proto),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_AI12_16E_proto),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_ADIO16_8FDS),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_ADIO16_8F),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_ADIO16_8A),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_ADIO16_8E),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_ADI16_8F),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_ADI16_8A),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_ADI16_8E),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_ADIO12_8A),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_ADIO12_8),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_ADIO12_8E),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_ADI12_8A),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_ADI12_8),
        APCI_MAKE_DEFAULT_ENTRY(mPCIe_ADI12_8E),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DIO_24HC),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DIO_72),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DIO_96),
        APCI_MAKE_DEFAULT_ENTRY(PCIe_DIO_120), );

// clang-format on
#define APCI_TABLE_SIZE \
	sizeof(apci_driver_table) / sizeof(struct apci_lookup_table_entry)
#define APCI_TABLE_ENTRY_SIZE sizeof(struct apci_lookup_table_entry)

void *bsearch(const void *key, const void *base, size_t num, size_t size,
	      int (*cmp)(const void *key, const void *elt))
{
	int start = 0, end = num - 1, mid, result;
	if (num == 0)
		return NULL;

	while (start <= end) {
		mid = (start + end) / 2;
		result = cmp(key, base + mid * size);
		if (result < 0)
			end = mid - 1;
		else if (result > 0)
			start = mid + 1;
		else
			return (void *)base + mid * size;
	}

	return NULL;
}

int APCI_LOOKUP_ENTRY(int fx)
{
	struct apci_lookup_table_entry driver_entry;
	struct apci_lookup_table_entry *driver_num;
	driver_entry.board_id = fx;
	driver_num = (struct apci_lookup_table_entry *)bsearch(
		&driver_entry, apci_driver_table, APCI_TABLE_SIZE,
		APCI_TABLE_ENTRY_SIZE, te_sort);

	if (!driver_num) {
		return -1;
	} else {
		return (int)(driver_num - apci_driver_table);
	}
}
#endif

static struct class *class_apci;

/* PCI Driver setup */
static struct pci_driver pci_driver = {
	.name = "acpi",
	.id_table = ids,
	.probe = probe,
	.remove = remove,
};

static struct file_operations apci_child_ops = { .owner = THIS_MODULE,
						 .open = open_child_apci,
						 .read = read_child_apci,
						 .write = write_child_apci,
						 .llseek = seek_child_apci };
/* File Operations */
static struct file_operations apci_root_fops = { .owner = THIS_MODULE,
						 .read = read_apci,
						 .open = open_apci,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39)
						 .ioctl = ioctl_apci,
#else
						 .unlocked_ioctl = ioctl_apci,
#endif
						 .mmap = mmap_apci,
						 .release = release_apci };

static const int NUM_DEVICES = 4;
static const char APCI_CLASS_NAME[] = "apci";
struct cdev apci_cdev;

static struct class *class_apci;
/* struct apci_my_info *head = NULL; */
struct apci_my_info head;

static int dev_counter = 0;

#define APCI_MAJOR 247

static dev_t apci_first_dev = MKDEV(APCI_MAJOR, 0);

void *apci_alloc_driver(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct apci_my_info *ddata =
		kmalloc(sizeof(struct apci_my_info), GFP_KERNEL);
	int count, plx_bar;
	struct resource *presource;

	if (!ddata)
		return NULL;

	ddata->dac_fifo_buffer = NULL;

	/* Initialize with defaults, fill in specifics later */
	ddata->irq = 0;
	ddata->irq_capable = 0;

	ddata->dma_virt_addr = NULL;

	for (count = 0; count < 6; count++) {
		ddata->regions[count].start = 0;
		ddata->regions[count].end = 0;
		ddata->regions[count].flags = 0;
		ddata->regions[count].mapped_address = NULL;
		ddata->regions[count].length = 0;
	}

	ddata->plx_region.start = 0;
	ddata->plx_region.end = 0;
	ddata->plx_region.length = 0;
	ddata->plx_region.mapped_address = NULL;

	ddata->dev_id = id->device;

	spin_lock_init(&(ddata->irq_lock));
	/* ddata->next = NULL; */

	switch (ddata->dev_id) {
	case mPCIe_AIO16_16FDS:
	case mPCIe_AIO16_16F:
	case mPCIe_AIO16_16A:
	case mPCIe_AIO16_16E:
	case mPCIe_AI16_16F:
	case mPCIe_AI16_16A:
	case mPCIe_AI16_16E:
	case mPCIe_AIO12_16A:
	case mPCIe_AIO12_16:
	case mPCIe_AIO12_16E:
	case mPCIe_AI12_16A:
	case mPCIe_AI12_16:
	case mPCIe_AI12_16E:
	case mPCIe_ADIO16_8FDS:
	case mPCIe_ADIO16_8F:
	case mPCIe_ADI16_8F:
	case mPCIe_ADI16_8A:
	case mPCIe_ADI16_8E:
	case mPCIe_ADIO12_8A:
	case mPCIe_ADIO12_8:
	case mPCIe_ADIO12_8E:
	case mPCIe_ADI12_8A:
	case mPCIe_ADI12_8:
	case mPCIe_ADI12_8E:
	case PCIe_ADIO16_16FDS:
	case PCIe_ADIO16_16F:
	case PCIe_ADIO16_16A:
	case PCIe_ADIO16_16E:
	case PCIe_ADI16_16F:
	case PCIe_ADI16_16A:
	case PCIe_ADI16_16E:
	case PCIe_ADIO12_16A:
	case PCIe_ADIO12_16:
	case PCIe_ADIO12_16E:
	case PCIe_ADI12_16A:
	case PCIe_ADI12_16:
	case PCIe_ADI12_16E:
		break;

	default:
		if (pci_resource_flags(pdev, 0) & IORESOURCE_IO) {
			plx_bar = 0;
		} else {
			plx_bar = 1;
		}

		apci_debug("dev_id = %04x. plx_bar = %d\n", ddata->dev_id,
			   plx_bar);

		ddata->plx_region.start = pci_resource_start(pdev, plx_bar);
		if (!ddata->plx_region.start) {
			apci_error("Invalid PLX bar %d on start ", plx_bar);
		}

		ddata->plx_region.end = pci_resource_end(pdev, plx_bar);
		if (!ddata->plx_region.start) {
			apci_error("Invalid PLX bar %d on end", plx_bar);
		}
		ddata->plx_region.flags = pci_resource_flags(pdev, plx_bar);

		ddata->plx_region.length =
			ddata->plx_region.end - ddata->plx_region.start + 1;

		apci_debug("plx_region.start = %08llx\n",
			   ddata->plx_region.start);
		apci_debug("plx_region.end   = %08llx\n",
			   ddata->plx_region.end);
		apci_debug("plx_region.length= %08x\n",
			   ddata->plx_region.length);
		apci_debug("plx_region.flags = %08lx\n",
			   ddata->plx_region.flags);

		if (ddata->plx_region.flags & IORESOURCE_IO) {
			presource = request_region(ddata->plx_region.start,
						   ddata->plx_region.length,
						   "apci");
			if (presource == NULL) {
				/* We couldn't get the region.  We have only
				 * allocated ddata so release it and return an
				 * error.
				 */
				apci_error("Unable to request region.\n");
				goto out_alloc_driver;
			}
		} else {
			ddata->plx_region.mapped_address =
				ioremap(ddata->plx_region.start,
					ddata->plx_region.length);
		}
		break;
	}
	/* TODO: request and remap the region for plx */

	switch (ddata->dev_id) {
	case PCIe_DIO_24: /* group1 */
	case MPCIE_DIO_24S:
	case MPCIE_DIO_24S_R1:
	case MPCIE_IDIO_8:
	case MPCIE_IIRO_8:
	case MPCIE_IDIO_4:
	case MPCIE_IIRO_4:
	case MPCIE_IDO_8:
	case MPCIE_RO_8:
	case MPCIE_II_16:
	case MPCIE_II_8:
	case MPCIE_II_4:
	case MPCIE_QUAD_4:
	case MPCIE_QUAD_8:
	case PCI_QUAD_8:
	case PCI_QUAD_4:
	case MPCIE_DIO_24:
	case PCIe_DIO_24DS:
	case PCIe_DIO_24DC:
	case PCIe_DIO_24DCS:
	case PCIe_DIO_48:
	case PCIe_DIO_48S:
	case PCIe_IIRO_8:
	case PCIe_IIRO_16:
	case PCI_DIO_24H:
	case PCI_DIO_24D:
	case PCI_DIO_24H_C:
	case PCI_DIO_24D_C:
	case PCI_DIO_24S:
	case PCI_DIO_48:
	case PCI_DIO_48S:
	case P104_DIO_48S:
	case PCI_DIO_72:
	case PCI_DIO_96:
	case PCI_DIO_96C3:
	case PCI_DIO_120:
	case PCI_AI12_16:
	case PCI_AI12_16_:
	case PCI_AI12_16A:
	case PCI_A12_16A:
	case LPCI_IIRO_8:
	case PCI_IIRO_8:
	case PCI_IIRO_16:
	case PCI_IDI_48:
	case PCI_IDIO_16:
	case PCIe_IDIO_12:
	case PCIe_IDIO_24:
	case PCIe_DIO_24HC:
	case PCIe_DIO_72:
	case PCIe_DIO_96:
	case PCIe_DIO_120:
		ddata->regions[2].start = pci_resource_start(pdev, 2);
		ddata->regions[2].end = pci_resource_end(pdev, 2);
		ddata->regions[2].flags = pci_resource_flags(pdev, 2);
		ddata->regions[2].length =
			ddata->regions[2].end - ddata->regions[2].start + 1;
		ddata->irq = pdev->irq;
		ddata->irq_capable = 1;
		apci_debug("regions[2].start = %08llx\n",
			   ddata->regions[2].start);
		apci_debug("regions[2].end   = %08llx\n",
			   ddata->regions[2].end);
		apci_debug("regions[2].length= %08x\n",
			   ddata->regions[2].length);
		apci_debug("regions[2].flags = %lx\n", ddata->regions[2].flags);
		apci_debug("irq = %d\n", ddata->irq);
		break;

	case PCI_IDO_48: /* group2 */
		ddata->regions[2].start = pci_resource_start(pdev, 2);
		ddata->regions[2].end = pci_resource_end(pdev, 2);
		ddata->regions[2].flags = pci_resource_flags(pdev, 2);
		ddata->regions[2].length =
			ddata->regions[2].end - ddata->regions[2].start + 1;
		apci_debug("regions[2].start = %08llx\n",
			   ddata->regions[2].start);
		apci_debug("regions[2].end   = %08llx\n",
			   ddata->regions[2].end);
		apci_debug("regions[2].length= %08x\n",
			   ddata->regions[2].length);
		apci_debug("regions[2].flags = %lx\n", ddata->regions[2].flags);
		apci_debug("NO irq\n");
		break;

	case mPCIe_AIO16_16FDS:
	case mPCIe_AIO16_16F:
	case mPCIe_AIO16_16A:
	case mPCIe_AIO16_16E:
	case mPCIe_AI16_16F:
	case mPCIe_AI16_16A:
	case mPCIe_AI16_16E:
	case mPCIe_AIO12_16A:
	case mPCIe_AIO12_16:
	case mPCIe_AIO12_16E:
	case mPCIe_AI12_16A:
	case mPCIe_AI12_16:
	case mPCIe_AI12_16E:
	case mPCIe_ADIO16_8FDS:
	case mPCIe_ADIO16_8F:
	case mPCIe_ADIO16_8A:
	case mPCIe_ADIO16_8E:
	case mPCIe_ADI16_8F:
	case mPCIe_ADI16_8A:
	case mPCIe_ADI16_8E:
	case mPCIe_ADIO12_8A:
	case mPCIe_ADIO12_8:
	case mPCIe_ADIO12_8E:
	case mPCIe_ADI12_8A:
	case mPCIe_ADI12_8:
	case mPCIe_ADI12_8E:
	case PCIe_ADIO16_16FDS:
	case PCIe_ADIO16_16F:
	case PCIe_ADIO16_16A:
	case PCIe_ADIO16_16E:
	case PCIe_ADI16_16F:
	case PCIe_ADI16_16A:
	case PCIe_ADI16_16E:
	case PCIe_ADIO12_16A:
	case PCIe_ADIO12_16:
	case PCIe_ADIO12_16E:
	case PCIe_ADI12_16A:
	case PCIe_ADI12_16:
	case PCIe_ADI12_16E:
		ddata->regions[0].start = pci_resource_start(pdev, 0);
		ddata->regions[0].end = pci_resource_end(pdev, 0);
		ddata->regions[0].flags = pci_resource_flags(pdev, 0);
		ddata->regions[0].length =
			ddata->regions[0].end - ddata->regions[0].start + 1;

		ddata->regions[1].start = pci_resource_start(pdev, 2);
		ddata->regions[1].end = pci_resource_end(pdev, 2);
		ddata->regions[1].flags = pci_resource_flags(pdev, 2);
		ddata->regions[1].length =
			ddata->regions[1].end - ddata->regions[1].start + 1;

		ddata->irq = pdev->irq;
		ddata->irq_capable = 1;
		apci_debug("[%04x]: regions[0].start = %08llx\n", ddata->dev_id,
			   ddata->regions[0].start);
		apci_debug("        regions[0].end   = %08llx\n",
			   ddata->regions[0].end);
		apci_debug("        regions[0].length= %08x\n",
			   ddata->regions[0].length);
		apci_debug("        regions[0].flags = %lx\n",
			   ddata->regions[0].flags);
		apci_debug("        regions[1].start = %08llx\n",
			   ddata->regions[1].start);
		apci_debug("        regions[1].end   = %08llx\n",
			   ddata->regions[1].end);
		apci_debug("        regions[1].length= %08x\n",
			   ddata->regions[1].length);
		apci_debug("        regions[1].flags = %lx\n",
			   ddata->regions[1].flags);
		apci_debug("        irq = %d\n", ddata->irq);
		break;

	case LPCI_A16_16A:
	case PCIe_DA12_16:
	case PCIe_DA12_8:
	case PCIe_DA16_16:
	case PCIe_DA16_8:
	case PCI_DA12_16:
	case PCI_DA12_8:
	case mPCIe_AIO16_16F_proto:
	case mPCIe_AIO16_16A_proto:
	case mPCIe_AIO16_16E_proto:
	case mPCIe_AI16_16F_proto:
	case mPCIe_AI16_16A_proto:
	case mPCIe_AI16_16E_proto:
	case mPCIe_AIO12_16A_proto:
	case mPCIe_AIO12_16_proto:
	case mPCIe_AIO12_16E_proto:
	case mPCIe_AI12_16A_proto:
	case mPCIe_AI12_16_proto:
	case mPCIe_AI12_16E_proto: /* group3 */

		ddata->regions[2].start = pci_resource_start(pdev, 2);
		ddata->regions[2].end = pci_resource_end(pdev, 2);
		ddata->regions[2].flags = pci_resource_flags(pdev, 2);
		ddata->regions[2].length =
			ddata->regions[2].end - ddata->regions[2].start + 1;

		ddata->regions[3].start = pci_resource_start(pdev, 3);
		ddata->regions[3].end = pci_resource_end(pdev, 3);
		ddata->regions[3].flags = pci_resource_flags(pdev, 3);
		ddata->regions[3].length =
			ddata->regions[3].end - ddata->regions[3].start + 1;

		ddata->irq = pdev->irq;
		ddata->irq_capable = 1;
		apci_debug("regions[2].start = %08llx\n",
			   ddata->regions[2].start);
		apci_debug("regions[2].end   = %08llx\n",
			   ddata->regions[2].end);
		apci_debug("regions[2].length= %08x\n",
			   ddata->regions[2].length);
		apci_debug("regions[2].flags = %lx\n", ddata->regions[2].flags);
		apci_debug("regions[3].start = %08llx\n",
			   ddata->regions[3].start);
		apci_debug("regions[3].end   = %08llx\n",
			   ddata->regions[3].end);
		apci_debug("regions[3].length= %08x\n",
			   ddata->regions[3].length);
		apci_debug("regions[3].flags = %lx\n", ddata->regions[3].flags);
		apci_debug("irq = %d\n", ddata->irq);
		break;

	case PCI_DA12_6: /* group4 */
	case PCI_DA12_4:
	case PCI_DA12_2:
	case PCIe_DA12_6: /* group4 */
	case PCIe_DA12_4:
	case PCIe_DA12_2:
	case PCIe_DA16_6: /* group4 */
	case PCIe_DA16_4:
	case PCIe_DA16_2:
		ddata->regions[2].start = pci_resource_start(pdev, 2);
		ddata->regions[2].end = pci_resource_end(pdev, 2);
		ddata->regions[2].flags = pci_resource_flags(pdev, 2);
		ddata->regions[2].length =
			ddata->regions[2].end - ddata->regions[2].start + 1;

		ddata->regions[3].start = pci_resource_start(pdev, 3);
		ddata->regions[3].end = pci_resource_end(pdev, 3);
		ddata->regions[3].flags = pci_resource_flags(pdev, 3);
		ddata->regions[3].length =
			ddata->regions[3].end - ddata->regions[3].start + 1;
		apci_debug("regions[2].start = %08llx\n",
			   ddata->regions[2].start);
		apci_debug("regions[2].end   = %08llx\n",
			   ddata->regions[2].end);
		apci_debug("regions[2].length= %08x\n",
			   ddata->regions[2].length);
		apci_debug("regions[2].flags = %lx\n", ddata->regions[2].flags);
		apci_debug("regions[3].start = %08llx\n",
			   ddata->regions[3].start);
		apci_debug("regions[3].end   = %08llx\n",
			   ddata->regions[3].end);
		apci_debug("regions[3].length= %08x\n",
			   ddata->regions[3].length);
		apci_debug("regions[3].flags = %lx\n", ddata->regions[3].flags);
		apci_debug("NO irq\n");
		break;
	}

	// cards where we support DMA. So far just the mPCIe_AI*_proto cards
	switch (ddata->dev_id) {
	case mPCIe_AIO16_16F_proto:
	case mPCIe_AIO16_16A_proto:
	case mPCIe_AIO16_16E_proto:
	case mPCIe_AI16_16F_proto:
	case mPCIe_AI16_16A_proto:
	case mPCIe_AI16_16E_proto:
	case mPCIe_AIO12_16A_proto:
	case mPCIe_AIO12_16_proto:
	case mPCIe_AIO12_16E_proto:
	case mPCIe_AI12_16A_proto:
	case mPCIe_AI12_16_proto:
	case mPCIe_AI12_16E_proto:

	case PCIe_ADIO16_16FDS:
	case PCIe_ADIO16_16F:
	case PCIe_ADIO16_16A:
	case PCIe_ADIO16_16E:
	case PCIe_ADI16_16F:
	case PCIe_ADI16_16A:
	case PCIe_ADI16_16E:
	case PCIe_ADIO12_16A:
	case PCIe_ADIO12_16:
	case PCIe_ADIO12_16E:
	case PCIe_ADI12_16A:
	case PCIe_ADI12_16:
	case PCIe_ADI12_16E:
	case mPCIe_AIO16_16FDS:
	case mPCIe_AIO16_16F:
	case mPCIe_AIO16_16A:
	case mPCIe_AIO16_16E:
	case mPCIe_AI16_16F:
	case mPCIe_AI16_16A:
	case mPCIe_AI16_16E:
	case mPCIe_AIO12_16A:
	case mPCIe_AIO12_16:
	case mPCIe_AIO12_16E:
	case mPCIe_AI12_16A:
	case mPCIe_AI12_16:
	case mPCIe_AI12_16E:
	case mPCIe_ADIO16_8FDS:
	case mPCIe_ADIO16_8F:
	case mPCIe_ADIO16_8A:
	case mPCIe_ADIO16_8E:
	case mPCIe_ADI16_8F:
	case mPCIe_ADI16_8A:
	case mPCIe_ADI16_8E:
	case mPCIe_ADIO12_8A:
	case mPCIe_ADIO12_8:
	case mPCIe_ADIO12_8E:
	case mPCIe_ADI12_8A:
	case mPCIe_ADI12_8:
	case mPCIe_ADI12_8E:
		apci_devel("setting up DMA in alloc\n");
		ddata->regions[0].start = pci_resource_start(pdev, 0);
		ddata->regions[0].end = pci_resource_end(pdev, 0);
		ddata->regions[0].flags = pci_resource_flags(pdev, 0);
		ddata->regions[0].length =
			ddata->regions[0].end - ddata->regions[0].start + 1;

		iounmap(ddata->plx_region.mapped_address);
		apci_debug("regions[0].start = %08llx\n",
			   ddata->regions[0].start);
		apci_debug("regions[0].end   = %08llx\n",
			   ddata->regions[0].end);
		apci_debug("regions[0].length= %08x\n",
			   ddata->regions[0].length);
		apci_debug("regions[0].flags = %lx\n", ddata->regions[0].flags);
		break;
	}

	/* request regions */
	for (count = 0; count < 6; count++) {
		if (ddata->regions[count].start == 0) {
			continue; /* invalid region */
		}
		if (ddata->regions[count].flags & IORESOURCE_IO) {
			apci_debug("requesting io region start=%08llx,len=%d\n",
				   ddata->regions[count].start,
				   ddata->regions[count].length);
			presource = request_region(ddata->regions[count].start,
						   ddata->regions[count].length,
						   "apci");
		} else {
			apci_debug(
				"requesting mem region start=%08llx,len=%d\n",
				ddata->regions[count].start,
				ddata->regions[count].length);
			presource = request_mem_region(
				ddata->regions[count].start,
				ddata->regions[count].length, "apci");
			if (presource != NULL) {
				ddata->regions[count].mapped_address =
					ioremap(ddata->regions[count].start,
						ddata->regions[count].length);
			}
		}

		if (presource == NULL) {
			/* If we failed to allocate the region. */
			count--;

			while (count >= 0) {
				if (ddata->regions[count].start != 0) {
					if (ddata->regions[count].flags &
					    IORESOURCE_IO) {
						/* if it is a valid region */
						release_region(
							ddata->regions[count]
								.start,
							ddata->regions[count]
								.length);
					} else {
						iounmap(ddata->regions[count]
								.mapped_address);

						release_region(
							ddata->regions[count]
								.start,
							ddata->regions[count]
								.length);
					}
				}
			}
			goto out_alloc_driver;
		}
	}

	// cards where we support DMA. So far just the mPCIe_AI*(_proto) cards
	switch (ddata->dev_id) {
	case mPCIe_AIO16_16F_proto:
	case mPCIe_AIO16_16A_proto:
	case mPCIe_AIO16_16E_proto:
	case mPCIe_AI16_16F_proto:
	case mPCIe_AI16_16A_proto:
	case mPCIe_AI16_16E_proto:
	case mPCIe_AIO12_16A_proto:
	case mPCIe_AIO12_16_proto:
	case mPCIe_AIO12_16E_proto:
	case mPCIe_AI12_16A_proto:
	case mPCIe_AI12_16_proto:
	case mPCIe_AI12_16E_proto:

	case PCIe_ADIO16_16FDS:
	case PCIe_ADIO16_16F:
	case PCIe_ADIO16_16A:
	case PCIe_ADIO16_16E:
	case PCIe_ADI16_16F:
	case PCIe_ADI16_16A:
	case PCIe_ADI16_16E:
	case PCIe_ADIO12_16A:
	case PCIe_ADIO12_16:
	case PCIe_ADIO12_16E:
	case PCIe_ADI12_16A:
	case PCIe_ADI12_16:
	case PCIe_ADI12_16E:
	case mPCIe_AIO16_16FDS:
	case mPCIe_AIO16_16F:
	case mPCIe_AIO16_16A:
	case mPCIe_AIO16_16E:
	case mPCIe_AI16_16F:
	case mPCIe_AI16_16A:
	case mPCIe_AI16_16E:
	case mPCIe_AIO12_16A:
	case mPCIe_AIO12_16:
	case mPCIe_AIO12_16E:
	case mPCIe_AI12_16A:
	case mPCIe_AI12_16:
	case mPCIe_AI12_16E:
	case mPCIe_ADIO16_8FDS:
	case mPCIe_ADIO16_8F:
	case mPCIe_ADIO16_8A:
	case mPCIe_ADIO16_8E:
	case mPCIe_ADI16_8F:
	case mPCIe_ADI16_8A:
	case mPCIe_ADI16_8E:
	case mPCIe_ADIO12_8A:
	case mPCIe_ADIO12_8:
	case mPCIe_ADIO12_8E:
	case mPCIe_ADI12_8A:
	case mPCIe_ADI12_8:
	case mPCIe_ADI12_8E:
		spin_lock_init(&(ddata->dma_data_lock));
		ddata->plx_region = ddata->regions[0];
		apci_debug("DMA spinlock init\n");
		break;
	}

	return ddata;

out_alloc_driver:
	kfree(ddata);
	return NULL;
}

/**
 * @desc Removes all of the drivers from this module
 *       before cleanup
 */
void apci_free_driver(struct pci_dev *pdev)
{
	struct apci_my_info *ddata = pci_get_drvdata(pdev);
	int count;

	apci_devel("Entering free driver.\n");

	if (ddata->plx_region.flags & IORESOURCE_IO) {
		apci_debug("releasing memory of %08llx , length=%d\n",
			   ddata->plx_region.start, ddata->plx_region.length);
		release_region(ddata->plx_region.start,
			       ddata->plx_region.length);
	} else {
		// iounmap(ddata->plx_region.mapped_address);
		// release_mem_region(ddata->plx_region.start,
		// ddata->plx_region.length);
	}

	for (count = 0; count < 6; count++) {
		if (ddata->regions[count].start == 0) {
			continue; /* invalid region */
		}
		if (ddata->regions[count].flags & IORESOURCE_IO) {
			release_region(ddata->regions[count].start,
				       ddata->regions[count].length);
		} else {
			iounmap(ddata->regions[count].mapped_address);
			release_mem_region(ddata->regions[count].start,
					   ddata->regions[count].length);
		}
	}

	if (NULL != ddata->dac_fifo_buffer) {
		kfree(ddata->dac_fifo_buffer);
	}
	kfree(ddata);
	apci_debug("Completed freeing driver.\n");
}

int destroy_child_devices(struct device *dev, void *data)
{
	struct apci_child_info *child_info = dev_get_drvdata(dev);
	cdev_del(&child_info->cdev);

	device_destroy(class_apci, dev->devt);

	return 0;
}

static void apci_class_dev_unregister(struct apci_my_info *ddata)
{
	struct apci_lookup_table_entry *obj =
		&apci_driver_table[APCI_LOOKUP_ENTRY((int)ddata->id->device)];
	apci_devel("entering apci_class_dev_unregister\n");
	/* ddata->dev = device_create(class_apci, &ddata->pci_dev->dev ,
	 * apci_first_dev + id, NULL, "apci/%s_%d", obj->name, obj->counter ++
	 * ); */

	apci_devel("entering apci_class_dev_unregister.\n");
	if (ddata->dev == NULL)
		return;

	device_for_each_child_reverse(ddata->dev, NULL, destroy_child_devices);

	device_destroy(class_apci, ddata->dev->devt);
	obj->counter--;
	dev_counter--;

	apci_devel("leaving apci_class_dev_unregister.\n");
}

static int __devinit apci_class_child_dev_register(
	struct apci_my_info *ddata, const char *dev_type_name,
	enum apci_child_device_type dev_type, int num_devs)
{
	int ret = 0;
	struct apci_lookup_table_entry *obj =
		&apci_driver_table[APCI_LOOKUP_ENTRY((int)ddata->id->device)];

	if (num_devs == 0)
		return 0;
	apci_debug("Creating child devices of type %s, number %d",
		   dev_type_name, num_devs);

	// Create child  devices
	for (int i = 0; i < num_devs; ++i) {
		struct apci_child_info *cdata =
			(struct apci_child_info *)kmalloc(
				sizeof(struct apci_child_info), GFP_KERNEL);
		apci_debug("Creating device %d of type %s", i, dev_type_name);

		cdata->type = dev_type;
		cdata->ddata = ddata;
		cdata->bar_offset = dev_type == APCI_CHILD_RELAY ?
					    obj->relay_offsets[i] :
					    obj->input_offsets[i];
		cdata->dev = device_create(class_apci, ddata->dev,
					   apci_first_dev + dev_counter, cdata,
					   "apci/%s_%d_%s%d", obj->name,
					   obj->counter, dev_type_name, i);
		if (IS_ERR(cdata->dev)) {
			apci_error(
				"Error creating child %s dev_t device %d for dev %d",
				dev_type_name, i, dev_counter);
			ret = PTR_ERR(cdata->dev);
			kfree(cdata);
			return ret;
		}
		cdev_init(&cdata->cdev, &apci_child_ops);
		cdata->cdev.owner = THIS_MODULE;
		cdev_set_parent(&cdata->cdev, &cdata->dev->kobj);
		ret = cdev_add(&cdata->cdev, apci_first_dev + dev_counter, 1);
		if (ret) {
			apci_error(
				"Error creating child %s character device %d for dev %d",
				dev_type_name, i, dev_counter);
			device_destroy(class_apci,
				       apci_first_dev + dev_counter);
			ret = PTR_ERR(cdata->dev);
			kfree(cdata);

			return ret;
		}

		dev_counter++;
	}
	return 0;
}
static int __devinit apci_class_root_dev_register(struct apci_my_info *ddata)
{
	int ret;
	struct apci_lookup_table_entry *obj =
		&apci_driver_table[APCI_LOOKUP_ENTRY((int)ddata->id->device)];
	apci_devel("entering apci_class_dev_register\n");

	ddata->dev = device_create(class_apci, &ddata->pci_dev->dev,
				   apci_first_dev + dev_counter, ddata,
				   "apci/%s_%d", obj->name, obj->counter);

	/* add pointer to the list of all ACCES I/O products */

	if (IS_ERR(ddata->dev)) {
		apci_error("Error creating device");
		ret = PTR_ERR(ddata->dev);
		ddata->dev = NULL;
		return ret;
	}

	dev_counter++;

	apci_class_child_dev_register(ddata, "relay", APCI_CHILD_RELAY,
				      obj->relay_bytes);

	apci_class_child_dev_register(ddata, "input", APCI_CHILD_INPUT,
				      obj->input_bytes);

	// Object counter increment happens after child devices created
	// so that they share same prefix.
	obj->counter++;

	apci_devel("leaving apci_class_dev_register\n");
	return 0;
}

irqreturn_t apci_interrupt(int irq, void *dev_id)
{
	struct apci_my_info *ddata;
	__u8 byte;
	__u32 dword;
	bool notify_user = true;
	uint32_t irq_event = 0;

	ddata = (struct apci_my_info *)dev_id;
	switch (ddata->dev_id) {
	case PCIe_ADIO16_16FDS:
	case PCIe_ADIO16_16F:
	case PCIe_ADIO16_16A:
	case PCIe_ADIO16_16E:
	case PCIe_ADI16_16F:
	case PCIe_ADI16_16A:
	case PCIe_ADI16_16E:
	case PCIe_ADIO12_16A:
	case PCIe_ADIO12_16:
	case PCIe_ADIO12_16E:
	case PCIe_ADI12_16A:
	case PCIe_ADI12_16:
	case PCIe_ADI12_16E:
	case mPCIe_AIO16_16FDS:
	case mPCIe_AIO16_16F:
	case mPCIe_AIO16_16A:
	case mPCIe_AIO16_16E:
	case mPCIe_AI16_16F:
	case mPCIe_AI16_16A:
	case mPCIe_AI16_16E:
	case mPCIe_AIO12_16A:
	case mPCIe_AIO12_16:
	case mPCIe_AIO12_16E:
	case mPCIe_AI12_16A:
	case mPCIe_AI12_16:
	case mPCIe_AI12_16E:
	case mPCIe_ADIO16_8FDS:
	case mPCIe_ADIO16_8F:
	case mPCIe_ADIO16_8A:
	case mPCIe_ADIO16_8E:
	case mPCIe_ADI16_8F:
	case mPCIe_ADI16_8A:
	case mPCIe_ADI16_8E:
	case mPCIe_ADIO12_8A:
	case mPCIe_ADIO12_8:
	case mPCIe_ADIO12_8E:
	case mPCIe_ADI12_8A:
	case mPCIe_ADI12_8:
	case mPCIe_ADI12_8E:
		break;

	default:
		/* The first thing we do is check to see if the card is causing
		 * an IRQ. If it is then we can proceed to clear the IRQ.
		 * Otherwise let Linux know that it wasn't us.
		 */
		if (!ddata->is_pcie) {
			byte = inb(ddata->plx_region.start + 0x4C);

			if ((byte & 4) == 0) {
				return IRQ_NONE; /* not me */
			}
		} else { /* PCIe */
			if (ddata->plx_region.flags & IORESOURCE_IO) {
				byte = inb(ddata->plx_region.start + 0x69);
			} else {
				byte = ioread8(
					ddata->plx_region.mapped_address +
					0x69);
			}

			if ((byte & 0x80) == 0) {
				return IRQ_NONE; /* not me */
			}
		}
	} // handle 9052 & 8311 "IAm" IRQ Flags

	apci_devel("ISR called.\n");

	/* Handle interrupt based on the device ID for the board. */
	switch (ddata->dev_id) {
	case PCIe_DIO_24:
	case PCIe_DIO_24D:
	case PCIe_DIO_24S:
	case PCIe_DIO_24DS:
	case PCIe_DIO_24DC:
	case PCIe_DIO_24DCS:
	case PCIe_DIO_48:
	case PCIe_DIO_48S:
	case PCI_DIO_24H:
	case PCI_DIO_24D:
	case PCI_DIO_24H_C:
	case PCI_DIO_24D_C:
	case PCI_DIO_24S:
	case PCI_DIO_48:
	case PCI_DIO_48S:
	case MPCIE_DIO_24:
	case MPCIE_DIO_24S:
	case MPCIE_DIO_24S_R1:
		outb(0, ddata->regions[2].start + 0x0f);
		break;

		/* These cards don't have the IRQ simply "Cleared",
		 * it must be disabled then re-enabled.
		 */
	case PCI_DIO_72:
	case PCI_DIO_96:
	case PCI_DIO_96CT:
	case PCI_DIO_96C3:
	case PCI_DIO_120:
	case PCIe_DIO_72:
	case PCIe_DIO_96:
	case PCIe_DIO_120:
		outb(0, ddata->regions[2].start + 0x1e);
		outb(0, ddata->regions[2].start + 0x1f);
		break;

	case PCI_DA12_16:
	case PCI_DA12_8:
	case PCIe_DA16_16:
	case PCIe_DA16_8:
	case PCIe_DA12_16:
	case PCIe_DA12_8:
		byte = inb(ddata->regions[2].start + 0xC);
		break;

	case PCI_WDG_CSM:
		outb(0, ddata->regions[2].start + 0x9);
		outb(0, ddata->regions[2].start + 0x4);
		break;

	case PCIe_IIRO_8:
	case PCIe_IIRO_16:
	case PCI_IIRO_8:
	case PCI_IIRO_16:
	case PCI_IDIO_16:
	case LPCI_IIRO_8:
		apci_devel("Interrupt for PCIe_IIRO_8");
		outb(0, ddata->regions[2].start + 0x1);
		break;

	case PCI_IDI_48:
		byte = inb(ddata->regions[2].start + 0x7);
		break;

	case PCI_AI12_16:
	case PCI_AI12_16_:
	case PCI_AI12_16A:
	case PCI_AIO12_16:
	case PCI_A12_16A:
		/* Clear the FIFO interrupt enable bits, but leave
		 * the counter enabled.  Otherwise the IRQ will not
		 * go away and user code will never run as the machine
		 * will hang in a never-ending IRQ loop. The userland
		 * irq routine must re-enable the interrupts if desired.
		 */
		outb(0x01, ddata->regions[2].start + 0x4);
		byte = inb(ddata->regions[2].start + 0x4);
		break;

	case LPCI_A16_16A:
		outb(0, ddata->regions[2].start + 0xc);
		outb(0x10, ddata->regions[2].start + 0xc);
		break;

	case P104_DIO_96: /* unknown at this time 6-FEB-2007 */
	case P104_DIO_48S: /* unknown at this time 6-FEB-2007 */
		break;
	case MPCIE_IDIO_8:
	case MPCIE_IIRO_8:
	case MPCIE_IDIO_4:
	case MPCIE_IIRO_4:
	case MPCIE_IDO_8:
	case MPCIE_RO_8:
	case MPCIE_II_16:
	case MPCIE_II_8:
	case MPCIE_II_4:
		outb(0xff, ddata->regions[2].start + 41);
		break;

	case MPCIE_QUAD_4:
	case PCI_QUAD_4: {
		int i;
		for (i = 0; i < 4; i++) {
			byte = inb(ddata->regions[2].start + 0x8 * i + 7);
			if (0 == (byte & 0x80))
				break;

			if (byte & 0x08)
				outb(0x08,
				     ddata->regions[2].start + 0x8 * i + 6);
		}
	} break;

	case MPCIE_QUAD_8:
	case PCI_QUAD_8: {
		int i;
		for (i = 0; i < 8; i++) {
			byte = inb(ddata->regions[2].start + 0x8 * i + 7);
			if (0 == (byte & 0x80))
				break;

			if (byte & 0x08)
				outb(0x08,
				     ddata->regions[2].start + 0x8 * i + 6);
		}
	} break;

	case PCIe_IDIO_12:
	case PCIe_IDIO_24:
		/* read 32 bits from +8 to determine which specific bits have
		 * generated a CoS IRQ then write the same value back to +8 to
		 * clear those CoS latches */
		dword = inl(ddata->regions[2].start + 0x8);
		outl(dword, ddata->regions[2].start + 0x8);
		break;

	case mPCIe_AIO16_16F_proto:
	case mPCIe_AIO16_16A_proto:
	case mPCIe_AIO16_16E_proto:
	case mPCIe_AI16_16F_proto:
	case mPCIe_AI16_16A_proto:
	case mPCIe_AI16_16E_proto:
	case mPCIe_AIO12_16A_proto:
	case mPCIe_AIO12_16_proto:
	case mPCIe_AIO12_16E_proto:
	case mPCIe_AI12_16A_proto:
	case mPCIe_AI12_16_proto:
	case mPCIe_AI12_16E_proto: // DMA capable cards
	{
		apci_devel("ISR: mPCIe-AI irq_event\n");
		// If this is a FIFO near full IRQ then tell the card
		// to write to the next buffer (and don't notify the user?)
		// else if it is a write done IRQ set last_valid_buffer and
		// notify user
		irq_event = ioread8(ddata->regions[2].mapped_address + 0x2);
		if (irq_event == 0) {
			apci_devel("ISR: not our IRQ\n");
			return IRQ_NONE;
		}

		if (irq_event & 0x1) // FIFO almost full
		{
			dma_addr_t base = ddata->dma_addr;
			spin_lock(&(ddata->dma_data_lock));
			if (ddata->dma_last_buffer == -1) {
				notify_user = false;
				apci_debug("ISR First IRQ");
			} else if (ddata->dma_first_valid == -1) {
				ddata->dma_first_valid = 0;
			}

			ddata->dma_last_buffer++;
			ddata->dma_last_buffer %= ddata->dma_num_slots;

			if (ddata->dma_last_buffer == ddata->dma_first_valid) {
				apci_error("ISR: data discarded");
				ddata->dma_last_buffer--;
				if (ddata->dma_last_buffer < 0)
					ddata->dma_last_buffer =
						ddata->dma_num_slots - 1;
				ddata->dma_data_discarded++;
			}
			spin_unlock(&(ddata->dma_data_lock));
			base += ddata->dma_slot_size * ddata->dma_last_buffer;

			iowrite32(base & 0xffffffff,
				  ddata->regions[0].mapped_address);
			iowrite32(base >> 32,
				  ddata->regions[0].mapped_address + 4);
			iowrite32(ddata->dma_slot_size,
				  ddata->regions[0].mapped_address + 8);
			iowrite32(4, ddata->regions[0].mapped_address + 12);
			udelay(5);
		}

		iowrite8(irq_event, ddata->regions[2].mapped_address + 0x2);
		apci_debug("ISR: irq_event = 0x%x, depth = 0x%x\n", irq_event,
			   ioread32(ddata->regions[2].mapped_address + 0x28));
		break;
	}

	case PCIe_ADIO16_16FDS:
	case PCIe_ADIO16_16F:
	case PCIe_ADIO16_16A:
	case PCIe_ADIO16_16E:
	case PCIe_ADI16_16F:
	case PCIe_ADI16_16A:
	case PCIe_ADI16_16E:
	case PCIe_ADIO12_16A:
	case PCIe_ADIO12_16:
	case PCIe_ADIO12_16E:
	case PCIe_ADI12_16A:
	case PCIe_ADI12_16:
	case PCIe_ADI12_16E:
	case mPCIe_AIO16_16FDS:
	case mPCIe_AIO16_16F:
	case mPCIe_AIO16_16A:
	case mPCIe_AIO16_16E:
	case mPCIe_AI16_16F:
	case mPCIe_AI16_16A:
	case mPCIe_AI16_16E:
	case mPCIe_AIO12_16A:
	case mPCIe_AIO12_16:
	case mPCIe_AIO12_16E:
	case mPCIe_AI12_16A:
	case mPCIe_AI12_16:
	case mPCIe_AI12_16E:
	case mPCIe_ADIO16_8FDS:
	case mPCIe_ADIO16_8F:
	case mPCIe_ADIO16_8A:
	case mPCIe_ADIO16_8E:
	case mPCIe_ADI16_8F:
	case mPCIe_ADI16_8A:
	case mPCIe_ADI16_8E:
	case mPCIe_ADIO12_8A:
	case mPCIe_ADIO12_8:
	case mPCIe_ADIO12_8E:
	case mPCIe_ADI12_8A:
	case mPCIe_ADI12_8:
	case mPCIe_ADI12_8E: {
		apci_devel("ISR: mPCIe-AxIO irq_event\n");
		// If this is a FIFO near full IRQ then tell the card
		// to write to the next buffer (and don't notify the user)
		// else if it is a write done IRQ set last_valid_buffer and
		// notify user
		irq_event = ioread32(
			ddata->regions[1].mapped_address +
			mPCIe_ADIO_IRQStatusAndClearOffset); // TODO: Upgrade to
							     // doRegisterAction("AmI?")

		if ((irq_event & mPCIe_ADIO_IRQEventMask) == 0) {
			apci_devel("ISR: not our IRQ\n");
			return IRQ_NONE;
		}

		if (irq_event &
		    (bmADIO_ADCTRIGGERStatus | bmADIO_DMADoneStatus)) {
			dma_addr_t base = ddata->dma_addr;
			spin_lock(&(ddata->dma_data_lock));
			if (ddata->dma_last_buffer == -1) {
				notify_user = false;
				apci_debug("ISR First IRQ");
			} else if (ddata->dma_first_valid == -1) {
				ddata->dma_first_valid = 0;
			}

			ddata->dma_last_buffer++;
			ddata->dma_last_buffer %= ddata->dma_num_slots;

			if (ddata->dma_last_buffer == ddata->dma_first_valid) {
				apci_error("ISR: data discarded");
				ddata->dma_last_buffer--;
				if (ddata->dma_last_buffer < 0)
					ddata->dma_last_buffer =
						ddata->dma_num_slots - 1;
				ddata->dma_data_discarded++;
			}
			spin_unlock(&(ddata->dma_data_lock));
			base += ddata->dma_slot_size * ddata->dma_last_buffer;

			iowrite32(base & 0xffffffff,
				  ddata->regions[0].mapped_address + 0x10);
			iowrite32(base >> 32,
				  ddata->regions[0].mapped_address + 4 + 0x10);
			iowrite32(ddata->dma_slot_size,
				  ddata->regions[0].mapped_address + 8 + 0x10);
			iowrite32(4,
				  ddata->regions[0].mapped_address + 12 + 0x10);
			udelay(5); // ?
		}

		iowrite32(
			irq_event,
			ddata->regions[1].mapped_address +
				mPCIe_ADIO_IRQStatusAndClearOffset); // clear
								     // whatever
								     // IRQ
								     // occurred
								     // and
								     // retain
								     // enabled
								     // IRQ
								     // sources
								     // // TODO:
								     // Upgrade
								     // to
								     // doRegisterAction("Clear&Enable")
		apci_debug(
			"ISR: irq_event = 0x%x, depth = 0x%x, IRQStatus = 0x%x\n",
			irq_event,
			ioread32(ddata->regions[1].mapped_address + 0x28),
			ioread32(ddata->regions[1].mapped_address + 0x40));
		break;
	}
	}; // end card-specific switch

	/* Check to see if we were actually waiting for an IRQ. If we were
	 * then we need to wake the queue associated with this device.
	 * Right now it is not possible for any other code sections that access
	 * the critical data to interrupt us so we won't disable other IRQs.
	 */
	if (notify_user) {
		spin_lock(&(ddata->irq_lock));

		if (ddata->waiting_for_irq) {
			ddata->waiting_for_irq = 0;
			spin_unlock(&(ddata->irq_lock));
			wake_up_interruptible(&(ddata->wait_queue));
		} else {
			spin_unlock(&(ddata->irq_lock));
		}
	}
	apci_devel("ISR: IRQ Handled\n");
	return IRQ_HANDLED;
}

void remove(struct pci_dev *pdev)
{
	struct apci_my_info *ddata = pci_get_drvdata(pdev);
	struct apci_my_info *child;
	struct apci_my_info *_temp;
	apci_devel("entering remove\n");

	// Don't spinlock around free irq as it will already wait for irqs to
	// finish, and disabling the irq will ensure nothing new happens
	if (ddata->irq_capable) {
		disable_irq(pdev->irq);

		free_irq(pdev->irq, ddata);
	}
	if (ddata->dma_virt_addr != NULL) {
		dma_free_coherent(&(ddata->pci_dev->dev),
				  ddata->dma_num_slots * ddata->dma_slot_size,
				  ddata->dma_virt_addr, ddata->dma_addr);
	}

	apci_class_dev_unregister(ddata);

	cdev_del(&ddata->cdev);

	spin_lock(&head.driver_list_lock);
	list_for_each_entry_safe(child, _temp, &head.driver_list, driver_list) {
		if (child == ddata) {
			apci_debug("Removing node with address %p\n", child);
			list_del(&child->driver_list);
		}
	}
	spin_unlock(&head.driver_list_lock);

	apci_free_driver(pdev);

	apci_devel("leaving remove\n");
}

/*
 * @note Adds the driver to the list of all ACCES I/O Drivers
 * that are supported by this driver
 */
int probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct apci_my_info *ddata;
	struct apci_my_info *_temp;
	struct apci_my_info *child;
	int ret;
	apci_devel("entering probe\n");

	if (pci_enable_device(pdev)) {
		return -ENODEV;
	}

	ddata = (struct apci_my_info *)apci_alloc_driver(pdev, id);
	if (ddata == NULL) {
		return -ENOMEM;
	}
	/* Setup actual device driver items */
	ddata->nchannels = APCI_NCHANNELS;
	ddata->pci_dev = pdev;
	ddata->id = id;
	ddata->is_pcie = (ddata->plx_region.length >= 0x100 ? 1 : 0);
	apci_debug("Is device PCIE : %d\n", ddata->is_pcie);

	/* Spin lock init stuff */

	/* Request Irq */
	if (ddata->irq_capable) {
		apci_debug("Requesting Interrupt, %u\n",
			   (unsigned int)ddata->irq);
		ret = request_irq((unsigned int)ddata->irq, apci_interrupt,
				  IRQF_SHARED, "apci", ddata);
		if (ret) {
			apci_error("error requesting IRQ %u\n", ddata->irq);
			ret = -ENOMEM;
			goto exit_free;
		}

		// TODO: Fix this when HW is available to test MEM version
		if (ddata->is_pcie) {
			if (ddata->dev_id != 0xC0E8) {
				if (ddata->plx_region.flags & IORESOURCE_IO) {
					outb(0x9,
					     ddata->plx_region.start + 0x69);
				} else {
					apci_debug(
						"Enabling IRQ MEM/IO plx region\n");
					iowrite8(
						0x9,
						ddata->plx_region.mapped_address +
							0x69);
				}
			}
		}

		/* switch( id->device ) {  */
		/* case PCIe_IIRO_8: */
		/*   apci_debug("Found PCIe_IIRO_8"); */
		/*   outb( 0x9, ddata->plx_region.start + 0x69 );  */
		/* default: */
		/*   apci_debug("Found no device"); */
		/* } */
		/* if (ret) { */
		/*      apci_error("Could not allocate irq."); */
		/*      goto exit_free; */
		/* } else { */
		init_waitqueue_head(&(ddata->wait_queue));
		/* } */
	}

	/* add to sysfs */

	cdev_init(&ddata->cdev, &apci_root_fops);
	ddata->cdev.owner = THIS_MODULE;

	ret = cdev_add(&ddata->cdev, apci_first_dev + dev_counter, 1);
	if (ret) {
		apci_error("error registering Driver %d", dev_counter);
		goto exit_irq;
	}

	/* add to the list of devices supported */
	spin_lock(&head.driver_list_lock);
	list_add(&ddata->driver_list, &head.driver_list);
	spin_unlock(&head.driver_list_lock);

	pci_set_drvdata(pdev, ddata);
	ret = apci_class_root_dev_register(ddata);

	if (ret)
		goto exit_pci_setdrv;

	apci_debug("Added driver %d\n", dev_counter - 1);
	apci_debug("Value of irq is %d\n", pdev->irq);
	return 0;

exit_pci_setdrv:
	/* Routine to remove the driver from the list */
	spin_lock(&head.driver_list_lock);
	list_for_each_entry_safe(child, _temp, &head.driver_list, driver_list) {
		if (child == ddata) {
			apci_debug("Would delete address %p\n", child);
		}
	}
	spin_unlock(&head.driver_list_lock);

	pci_set_drvdata(pdev, NULL);
	cdev_del(&ddata->cdev);
exit_irq:
	if (ddata->irq_capable)
		free_irq(pdev->irq, ddata);
exit_free:
	apci_free_driver(pdev);
	return ret;
}

/* Configure the default /dev/{devicename} permissions */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 2, 0)
static char *apci_devnode(struct device *dev, umode_t *mode)
#else
static char *apci_devnode(const struct device *dev, umode_t *mode)
#endif
{
	if (!mode)
		return NULL;
	*mode = APCI_DEFAULT_DEVFILE_MODE;
	return NULL;
}

int __init apci_init(void)
{
	void *ptr_err;
	int result;
	int ret;
	dev_t dev = MKDEV(APCI_MAJOR, 0);
	apci_debug("performing init duties\n");
	spin_lock_init(&head.driver_list_lock);
	INIT_LIST_HEAD(&head.driver_list);
	sort(apci_driver_table, APCI_TABLE_SIZE, APCI_TABLE_ENTRY_SIZE, te_sort,
	     NULL);

	ret = alloc_chrdev_region(&apci_first_dev, 0, MAX_APCI_DEVICES, APCI);
	if (ret) {
		apci_error("Unable to allocate device numbers");
		return ret;
	}

	/* Create the sysfs entry for this */
	class_apci = class_create(THIS_MODULE, APCI_CLASS_NAME);
	if (IS_ERR(ptr_err = class_apci))
		goto err;
	class_apci->devnode = apci_devnode; // set device file permissions

	cdev_init(&apci_cdev, &apci_root_fops);
	apci_cdev.owner = THIS_MODULE;

	result = cdev_add(&apci_cdev, dev, 1);

#ifdef TEST_CDEV_ADD_FAIL
	result = -1;
	if (result == -1) {
		apci_error("Going to delete device.\n");
		cdev_del(&apci_cdev);
		apci_error("Deleted device.\n");
	}
#endif

	if (result < 0) {
		apci_error("cdev_add failed in apci_init");
		goto err;
	}

	/* needed to get the probe and remove to be called */
	result = pci_register_driver(&pci_driver);

	return 0;
err:

	apci_error("Unregistering chrdev_region.\n");

	unregister_chrdev_region(MKDEV(APCI_MAJOR, 0), MAX_APCI_DEVICES);

	class_destroy(class_apci);
	return PTR_ERR(ptr_err);
}

static void __exit apci_exit(void)
{
	apci_debug("performing exit duties\n");
	pci_unregister_driver(&pci_driver);
	cdev_del(&apci_cdev);
	unregister_chrdev_region(MKDEV(APCI_MAJOR, 0), MAX_APCI_DEVICES);
	class_destroy(class_apci);
}

module_init(apci_init);
module_exit(apci_exit);

MODULE_AUTHOR("John Hentges <JHentges@accesio.com>");
MODULE_LICENSE("GPL");
