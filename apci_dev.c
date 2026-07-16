/*
 * ACCES I/O APCI Linux driver
 *
 * Copyright 1998-2024 John Hentges <jhentges@accesio.com>  All rights granted.
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

#ifndef PCI_IRQ_INTX
#ifdef PCI_IRQ_LEGACY
#define PCI_IRQ_INTX PCI_IRQ_LEGACY
#endif
#endif
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
#define bmADIO_LDACStatus (1 << 23)
#define bmADIO_FAFIRQStatus (1 << 20)
#define bmADIO_DMADoneStatus (1 << 18)
#define bmADIO_LDACEnable (1 << 7)
#define bmADIO_DMADoneEnable (1 << 2)
#define bmADIO_ADCTRIGGERStatus (1 << 16)
#define bmADIO_ADCTRIGGEREnable (1 << 0)

int irq_disabled = 0;
module_param(irq_disabled, int, 0444);
MODULE_PARM_DESC(irq_disabled, "Don't register an IRQ/ISR. IRSRC register must be polled from userspace");

static int use_msi = 0;
module_param(use_msi, int, 0444);
MODULE_PARM_DESC(use_msi, "Prefer MSI interrupts when supported; falls back to shared legacy INTx");

/* PCI table construction */
static struct pci_device_id ids[] = {
    {
        PCI_DEVICE(A_VENDOR_ID, ROB_MATRIX_8X16),
    },
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
        PCI_DEVICE(A_VENDOR_ID, MPCIE_DIO_24A),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, MPCIE_DIO_24X),
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
        PCI_DEVICE(A_VENDOR_ID, MPCIE_ISODIO_16),
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
        PCI_DEVICE(A_VENDOR_ID, MPCIE_IDIO_8HL),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, MPCIE_IDIO_8H),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, MPCIE_IDIO_8L),
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
        PCI_DEVICE(A_VENDOR_ID, mPCIe_DA16_8  ),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, mPCIe_DA16_4  ),
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
        PCI_DEVICE(A_VENDOR_ID, mPCIe_ADIODF16_8FDS),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, mPCIe_ADIODF16_8F),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, mPCIe_ADIODF16_8A),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, mPCIe_ADIODF16_8E),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, mPCIe_ADIODF12_8A),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, mPCIe_ADIODF12_8),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, mPCIe_ADIODF12_8E),
    },

    {
        PCI_DEVICE(A_VENDOR_ID, mPCIe_DAAI16_8F),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, mPCIe_DAAI16_8A),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, mPCIe_DAAI16_8E),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, mPCIe_DAAI12_8),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, mPCIe_DAAI12_8E),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, mPCIe_DAAI12_8A),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, mPCIe_DAAI16_4F),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, mPCIe_DAAI16_4A),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, mPCIe_DAAI16_4E),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, mPCIe_DAAI12_4A),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, mPCIe_DAAI12_4),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, mPCIe_DAAI12_4E),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, mPCIe_DA16_8),
    },
    {
        PCI_DEVICE(A_VENDOR_ID, mPCIe_DA16_4),
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
    }};
MODULE_DEVICE_TABLE(pci, ids);

struct apci_lookup_table_entry
{
  int board_id;
  int counter;
  char *name;
};

/* Driver naming section */
#define APCI_MAKE_ENTRY(X) \
  {                        \
      X, 0, NAME_##X}
#define APCI_MAKE_DRIVER_TABLE(...) \
  {                                 \
      __VA_ARGS__}
/* acpi_lookup_table */

static int
te_sort(const void *m1, const void *m2)
{
  struct apci_lookup_table_entry *mi1 = (struct apci_lookup_table_entry *)m1;
  struct apci_lookup_table_entry *mi2 = (struct apci_lookup_table_entry *)m2;
  return (mi1->board_id < mi2->board_id ? -1 : mi1->board_id != mi2->board_id);
}

#if 0
static struct apci_lookup_table_entry apci_driver_table[] = {
  { PCIe_IIRO_8, 0 , "iiro8" } ,
  { PCI_DIO_24D, 0 , "pci24d" },
  {0}
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

#define APCI_TABLE_SIZE sizeof(apci_driver_table) / sizeof(struct apci_lookup_table_entry)
#define APCI_TABLE_ENTRY_SIZE sizeof(struct apci_lookup_table_entry)

#else
static struct apci_lookup_table_entry apci_driver_table[] =
    APCI_MAKE_DRIVER_TABLE(
        APCI_MAKE_ENTRY(ROB_MATRIX_8X16),
        APCI_MAKE_ENTRY(PCIe_DIO_24),
        APCI_MAKE_ENTRY(PCIe_DIO_24D),
        APCI_MAKE_ENTRY(PCIe_DIO_24S),
        APCI_MAKE_ENTRY(PCIe_DIO_24DS),
        APCI_MAKE_ENTRY(PCIe_DIO_24DC),
        APCI_MAKE_ENTRY(PCIe_DIO_24DCS),
        APCI_MAKE_ENTRY(PCIe_DIO_48),
        APCI_MAKE_ENTRY(PCIe_DIO_48S),
        APCI_MAKE_ENTRY(PCIe_IIRO_8),
        APCI_MAKE_ENTRY(PCIe_IIRO_16),
        APCI_MAKE_ENTRY(PCI_DIO_24H),
        APCI_MAKE_ENTRY(PCI_DIO_24D),
        APCI_MAKE_ENTRY(PCI_DIO_24H_C),
        APCI_MAKE_ENTRY(PCI_DIO_24D_C),
        APCI_MAKE_ENTRY(PCI_DIO_24S),
        APCI_MAKE_ENTRY(PCI_DIO_48),
        APCI_MAKE_ENTRY(PCI_DIO_48S),
        APCI_MAKE_ENTRY(P104_DIO_48S),
        APCI_MAKE_ENTRY(PCI_DIO_48H),
        APCI_MAKE_ENTRY(PCI_DIO_48HS),
        APCI_MAKE_ENTRY(PCI_DIO_72),
        APCI_MAKE_ENTRY(P104_DIO_96),
        APCI_MAKE_ENTRY(PCI_DIO_96),
        APCI_MAKE_ENTRY(PCI_DIO_96CT),
        APCI_MAKE_ENTRY(PCI_DIO_96C3),
        APCI_MAKE_ENTRY(PCI_DIO_120),
        APCI_MAKE_ENTRY(PCI_AI12_16),
        APCI_MAKE_ENTRY(PCI_AI12_16_),
        APCI_MAKE_ENTRY(PCI_AI12_16A),
        APCI_MAKE_ENTRY(PCI_AIO12_16),
        APCI_MAKE_ENTRY(PCI_A12_16A),
        APCI_MAKE_ENTRY(LPCI_A16_16A),
        APCI_MAKE_ENTRY(PCIe_DA16_16),
        APCI_MAKE_ENTRY(PCIe_DA16_8),
        APCI_MAKE_ENTRY(PCIe_DA16_6),
        APCI_MAKE_ENTRY(PCIe_DA16_4),
        APCI_MAKE_ENTRY(PCIe_DA16_2),
        APCI_MAKE_ENTRY(PCIe_DA12_16),
        APCI_MAKE_ENTRY(PCIe_DA12_8),
        APCI_MAKE_ENTRY(PCIe_DA12_6),
        APCI_MAKE_ENTRY(PCIe_DA12_4),
        APCI_MAKE_ENTRY(PCIe_DA12_2),
        APCI_MAKE_ENTRY(PCI_DA12_16),
        APCI_MAKE_ENTRY(PCI_DA12_8),
        APCI_MAKE_ENTRY(PCI_DA12_6),
        APCI_MAKE_ENTRY(PCI_DA12_4),
        APCI_MAKE_ENTRY(PCI_DA12_2),
        APCI_MAKE_ENTRY(PCI_DA12_16V),
        APCI_MAKE_ENTRY(PCI_DA12_8V),
        APCI_MAKE_ENTRY(LPCI_IIRO_8),
        APCI_MAKE_ENTRY(PCI_IIRO_8),
        APCI_MAKE_ENTRY(PCI_IIRO_16),
        APCI_MAKE_ENTRY(PCI_IDI_48),
        APCI_MAKE_ENTRY(PCI_IDO_48),
        APCI_MAKE_ENTRY(PCI_IDIO_16),
        APCI_MAKE_ENTRY(PCI_WDG_2S),
        APCI_MAKE_ENTRY(PCI_WDG_CSM),
        APCI_MAKE_ENTRY(PCI_WDG_IMPAC),
        APCI_MAKE_ENTRY(MPCIE_DIO_24S),
        APCI_MAKE_ENTRY(MPCIE_DIO_24A),
        APCI_MAKE_ENTRY(MPCIE_DIO_24X),
        APCI_MAKE_ENTRY(MPCIE_DIO_24S_R1),
        APCI_MAKE_ENTRY(MPCIE_IDIO_8),
        APCI_MAKE_ENTRY(MPCIE_IIRO_8),
        APCI_MAKE_ENTRY(MPCIE_IDIO_4),
        APCI_MAKE_ENTRY(MPCIE_IIRO_4),
        APCI_MAKE_ENTRY(MPCIE_IDO_8),
        APCI_MAKE_ENTRY(MPCIE_ISODIO_16),
        APCI_MAKE_ENTRY(MPCIE_RO_8),
        APCI_MAKE_ENTRY(MPCIE_II_16),
        APCI_MAKE_ENTRY(MPCIE_II_8),
        APCI_MAKE_ENTRY(MPCIE_II_4),
        APCI_MAKE_ENTRY(MPCIE_QUAD_4),
        APCI_MAKE_ENTRY(MPCIE_QUAD_8),
        APCI_MAKE_ENTRY(MPCIE_IDIO_8HL),
        APCI_MAKE_ENTRY(MPCIE_IDIO_8H),
        APCI_MAKE_ENTRY(MPCIE_IDIO_8L),

        APCI_MAKE_ENTRY(PCI_QUAD_8),
        APCI_MAKE_ENTRY(PCI_QUAD_4),

        APCI_MAKE_ENTRY(MPCIE_DIO_24),
        APCI_MAKE_ENTRY(PCI_WDG_IMPAC),
        APCI_MAKE_ENTRY(PCIe_IDIO_12),
        APCI_MAKE_ENTRY(PCIe_IDIO_24),

        APCI_MAKE_ENTRY(PCIe_ADIO16_16FDS),
        APCI_MAKE_ENTRY(PCIe_ADIO16_16F),
        APCI_MAKE_ENTRY(PCIe_ADIO16_16A),
        APCI_MAKE_ENTRY(PCIe_ADIO16_16E),
        APCI_MAKE_ENTRY(PCIe_ADI16_16F),
        APCI_MAKE_ENTRY(PCIe_ADI16_16A),
        APCI_MAKE_ENTRY(PCIe_ADI16_16E),
        APCI_MAKE_ENTRY(PCIe_ADIO12_16A),
        APCI_MAKE_ENTRY(PCIe_ADIO12_16),
        APCI_MAKE_ENTRY(PCIe_ADIO12_16E),
        APCI_MAKE_ENTRY(PCIe_ADI12_16A),
        APCI_MAKE_ENTRY(PCIe_ADI12_16),
        APCI_MAKE_ENTRY(PCIe_ADI12_16E),

        APCI_MAKE_ENTRY(mPCIe_AIO16_16FDS),
        APCI_MAKE_ENTRY(mPCIe_AIO16_16F),
        APCI_MAKE_ENTRY(mPCIe_AIO16_16A),
        APCI_MAKE_ENTRY(mPCIe_AIO16_16E),
        APCI_MAKE_ENTRY(mPCIe_AI16_16F),
        APCI_MAKE_ENTRY(mPCIe_AI16_16A),
        APCI_MAKE_ENTRY(mPCIe_AI16_16E),
        APCI_MAKE_ENTRY(mPCIe_AIO12_16A),
        APCI_MAKE_ENTRY(mPCIe_AIO12_16),
        APCI_MAKE_ENTRY(mPCIe_AIO12_16E),
        APCI_MAKE_ENTRY(mPCIe_AI12_16A),
        APCI_MAKE_ENTRY(mPCIe_AI12_16),
        APCI_MAKE_ENTRY(mPCIe_AI12_16E),
        APCI_MAKE_ENTRY(mPCIe_AIO16_16F_proto),
        APCI_MAKE_ENTRY(mPCIe_AIO16_16A_proto),
        APCI_MAKE_ENTRY(mPCIe_AIO16_16E_proto),
        APCI_MAKE_ENTRY(mPCIe_AI16_16F_proto),
        APCI_MAKE_ENTRY(mPCIe_AI16_16A_proto),
        APCI_MAKE_ENTRY(mPCIe_AI16_16E_proto),
        APCI_MAKE_ENTRY(mPCIe_AIO12_16A_proto),
        APCI_MAKE_ENTRY(mPCIe_AIO12_16_proto),
        APCI_MAKE_ENTRY(mPCIe_AIO12_16E_proto),
        APCI_MAKE_ENTRY(mPCIe_AI12_16A_proto),
        APCI_MAKE_ENTRY(mPCIe_AI12_16_proto),
        APCI_MAKE_ENTRY(mPCIe_AI12_16E_proto),
        APCI_MAKE_ENTRY(mPCIe_ADIO16_8FDS),
        APCI_MAKE_ENTRY(mPCIe_ADIO16_8F),
        APCI_MAKE_ENTRY(mPCIe_ADIO16_8A),
        APCI_MAKE_ENTRY(mPCIe_ADIO16_8E),
        APCI_MAKE_ENTRY(mPCIe_ADI16_8F),
        APCI_MAKE_ENTRY(mPCIe_ADI16_8A),
        APCI_MAKE_ENTRY(mPCIe_ADI16_8E),
        APCI_MAKE_ENTRY(mPCIe_ADIO12_8A),
        APCI_MAKE_ENTRY(mPCIe_ADIO12_8),
        APCI_MAKE_ENTRY(mPCIe_ADIO12_8E),
        APCI_MAKE_ENTRY(mPCIe_ADI12_8A),
        APCI_MAKE_ENTRY(mPCIe_ADI12_8),
        APCI_MAKE_ENTRY(mPCIe_ADI12_8E),
        APCI_MAKE_ENTRY(mPCIe_ADIODF16_8FDS),
        APCI_MAKE_ENTRY(mPCIe_ADIODF16_8F),
        APCI_MAKE_ENTRY(mPCIe_ADIODF16_8A),
        APCI_MAKE_ENTRY(mPCIe_ADIODF16_8E),
        APCI_MAKE_ENTRY(mPCIe_ADIODF12_8A),
        APCI_MAKE_ENTRY(mPCIe_ADIODF12_8),
        APCI_MAKE_ENTRY(mPCIe_ADIODF12_8E),

        APCI_MAKE_ENTRY(mPCIe_ADIOHC16_8FDS),
        APCI_MAKE_ENTRY(mPCIe_ADIOHC16_8F),
        APCI_MAKE_ENTRY(mPCIe_ADIOHC16_8A),
        APCI_MAKE_ENTRY(mPCIe_ADIOHC16_8E),
        APCI_MAKE_ENTRY(mPCIe_ADIOHC12_8A),
        APCI_MAKE_ENTRY(mPCIe_ADIOHC12_8),
        APCI_MAKE_ENTRY(mPCIe_ADIOHC12_8E),
        APCI_MAKE_ENTRY(mPCIe_DAAI16_8F),
        APCI_MAKE_ENTRY(mPCIe_DAAI16_8A),
        APCI_MAKE_ENTRY(mPCIe_DAAI16_8E),
        APCI_MAKE_ENTRY(mPCIe_DAAI12_8),
        APCI_MAKE_ENTRY(mPCIe_DAAI12_8E),
        APCI_MAKE_ENTRY(mPCIe_DAAI12_8A),
        APCI_MAKE_ENTRY(mPCIe_DAAI16_4F),
        APCI_MAKE_ENTRY(mPCIe_DAAI16_4A),
        APCI_MAKE_ENTRY(mPCIe_DAAI16_4E),
        APCI_MAKE_ENTRY(mPCIe_DAAI12_4A),
        APCI_MAKE_ENTRY(mPCIe_DAAI12_4),
        APCI_MAKE_ENTRY(mPCIe_DAAI12_4E),
        APCI_MAKE_ENTRY(mPCIe_DA16_8),
        APCI_MAKE_ENTRY(mPCIe_DA16_4),
        APCI_MAKE_ENTRY(PCIe_DIO_24HC),
        APCI_MAKE_ENTRY(PCIe_DIO_72),
        APCI_MAKE_ENTRY(PCIe_DIO_96),
        APCI_MAKE_ENTRY(PCIe_DIO_120), );

#define APCI_TABLE_SIZE sizeof(apci_driver_table) / sizeof(struct apci_lookup_table_entry)
#define APCI_TABLE_ENTRY_SIZE sizeof(struct apci_lookup_table_entry)

void *bsearch(const void *key, const void *base, size_t num, size_t size,
              int (*cmp)(const void *key, const void *elt))
{
  int start = 0, end = num - 1, mid, result;
  if (num == 0)
    return NULL;

  while (start <= end)
  {
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
  driver_num = (struct apci_lookup_table_entry *)bsearch(&driver_entry,
                                                         apci_driver_table,
                                                         APCI_TABLE_SIZE,
                                                         APCI_TABLE_ENTRY_SIZE,
                                                         te_sort);

  if (!driver_num)
  {
    apci_debug("did not find driver in table for %04x", fx);
    return -1;
  }
  else
  {
    return (int)(driver_num - apci_driver_table);
  }
}
#endif

static struct class *class_apci;

/* PCI Driver setup */
static struct pci_driver pci_driver = {
    .name = "apci",
    .id_table = ids,
    .probe = probe,
    .remove = remove,
};

/* File Operations */
static struct file_operations apci_fops = {
    .read = read_apci,
    .open = open_apci,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39)
    .ioctl = ioctl_apci,
#else
    .unlocked_ioctl = ioctl_apci,
#endif
    .mmap = mmap_apci,
};

static const int NUM_DEVICES = 4;
static const char APCI_CLASS_NAME[] = "apci";
struct cdev apci_cdev;

static struct class *class_apci;
/* struct apci_my_info *head = NULL; */
struct apci_my_info head;

static int dev_counter = 0;

#define APCI_MAJOR 247

static dev_t apci_first_dev = MKDEV(APCI_MAJOR, 0);

irqreturn_t apci_interrupt(int irq, void *dev_id);

static void apci_release_io_region(io_region *region)
{
  if (region == NULL || region->start == 0 || region->length == 0)
    return;

  if (region->flags & IORESOURCE_IO)
  {
    release_region(region->start, region->length);
  }
  else
  {
    if (region->mapped_address != NULL)
      iounmap(region->mapped_address);
    release_mem_region(region->start, region->length);
  }

  region->mapped_address = NULL;
  region->start = 0;
  region->end = 0;
  region->length = 0;
  region->flags = 0;
}

static int apci_request_device_irq(struct apci_my_info *ddata)
{
  struct pci_dev *pdev = ddata->pci_dev;
  unsigned long irq_flags = IRQF_SHARED;
  int ret;

  ddata->irq_requested = 0;
  ddata->irq_msi_enabled = 0;
  ddata->irq_vectors_allocated = 0;
  ddata->irq = pdev->irq;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
  if (use_msi)
  {
    ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
    if (ret == 1)
    {
      ddata->irq = pci_irq_vector(pdev, 0);
      ddata->irq_msi_enabled = 1;
      ddata->irq_vectors_allocated = 1;
      irq_flags = 0;
    }
    else
    {
      dev_info(&pdev->dev,
               "MSI unavailable for bdf=%s device=%04x, falling back to legacy INTx: %d\n",
               pci_name(pdev), ddata->dev_id, ret);
    }
  }

  if (!ddata->irq_vectors_allocated)
  {
    ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_INTX);
    if (ret != 1)
      return ret < 0 ? ret : -ENODEV;

    ddata->irq = pci_irq_vector(pdev, 0);
    ddata->irq_msi_enabled = 0;
    ddata->irq_vectors_allocated = 1;
    irq_flags = IRQF_SHARED;
  }
#else
  if (use_msi)
  {
    ret = pci_enable_msi(pdev);
    if (ret == 0)
    {
      ddata->irq = pdev->irq;
      ddata->irq_msi_enabled = 1;
      irq_flags = 0;
    }
    else
    {
      dev_info(&pdev->dev,
               "MSI unavailable for bdf=%s device=%04x, falling back to legacy INTx: %d\n",
               pci_name(pdev), ddata->dev_id, ret);
    }
  }
#endif

  snprintf(ddata->irq_name, sizeof(ddata->irq_name), "apci:%s", pci_name(pdev));

  dev_info(&pdev->dev,
           "requesting IRQ %u using %s for bdf=%s device=%04x name=%s\n",
           (unsigned int)ddata->irq,
           ddata->irq_msi_enabled ? "MSI" : "legacy INTx",
           pci_name(pdev), ddata->dev_id, ddata->irq_name);

  ret = request_irq((unsigned int)ddata->irq,
                    apci_interrupt,
                    irq_flags,
                    ddata->irq_name,
                    ddata);
  if (ret)
  {
    dev_err(&pdev->dev,
            "error requesting IRQ %u for bdf=%s device=%04x: %d\n",
            ddata->irq, pci_name(pdev), ddata->dev_id, ret);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
    if (ddata->irq_vectors_allocated)
    {
      pci_free_irq_vectors(pdev);
      ddata->irq_vectors_allocated = 0;
    }
#else
    if (ddata->irq_msi_enabled)
    {
      pci_disable_msi(pdev);
      ddata->irq_msi_enabled = 0;
    }
#endif
    return ret;
  }

  ddata->irq_requested = 1;
  return 0;
}

static void apci_free_device_irq(struct apci_my_info *ddata)
{
  if (ddata == NULL || ddata->pci_dev == NULL)
    return;

  if (ddata->irq_requested)
  {
    free_irq(ddata->irq, ddata);
    ddata->irq_requested = 0;
  }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
  if (ddata->irq_vectors_allocated)
  {
    pci_free_irq_vectors(ddata->pci_dev);
    ddata->irq_vectors_allocated = 0;
  }
#else
  if (ddata->irq_msi_enabled)
  {
    pci_disable_msi(ddata->pci_dev);
  }
#endif
  ddata->irq_msi_enabled = 0;
}

void *
apci_alloc_driver(struct pci_dev *pdev, const struct pci_device_id *id)
{

  struct apci_my_info *ddata = kzalloc(sizeof(struct apci_my_info), GFP_KERNEL);
  int count, i, plx_bar;
  struct resource *presource;
  int status = -1;

  if (!ddata)
    return NULL;

  ddata->dac_fifo_buffer = NULL;

  /* Initialize with defaults, fill in specifics later */
  ddata->irq = 0;
  ddata->irq_capable = 0;
  ddata->irq_requested = 0;
  ddata->irq_msi_enabled = 0;
  ddata->irq_vectors_allocated = 0;
  ddata->irq_name[0] = '\0';
  ddata->waiting_for_irq = 0;
  ddata->irq_cancelled = 0;
  ddata->irq_event_pending = 0;
  ddata->irq_notify_count = 0;

  ddata->dma_virt_addr = NULL;
  ddata->dma_data_discarded = 0;
  ddata->dma_data_discarded_total = 0;

  for (count = 0; count < 6; count++)
  {
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

  switch (ddata->dev_id)
  {
  case ROB_MATRIX_8X16:
  case MPCIE_DIO_24A:
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
  case mPCIe_ADIODF16_8FDS:
  case mPCIe_ADIODF16_8F:
  case mPCIe_ADIODF16_8A:
  case mPCIe_ADIODF16_8E:
  case mPCIe_ADIODF12_8A:
  case mPCIe_ADIODF12_8:
  case mPCIe_ADIODF12_8E:
  case mPCIe_ADIOHC16_8FDS:
  case mPCIe_ADIOHC16_8F:
  case mPCIe_ADIOHC16_8A:
  case mPCIe_ADIOHC16_8E:
  case mPCIe_ADIOHC12_8A:
  case mPCIe_ADIOHC12_8:
  case mPCIe_ADIOHC12_8E:

  case mPCIe_DAAI16_8F:
  case mPCIe_DAAI16_8A:
  case mPCIe_DAAI16_8E:
  case mPCIe_DAAI12_8:
  case mPCIe_DAAI12_8E:
  case mPCIe_DAAI12_8A:
  case mPCIe_DAAI16_4F:
  case mPCIe_DAAI16_4A:
  case mPCIe_DAAI16_4E:
  case mPCIe_DAAI12_4A:
  case mPCIe_DAAI12_4:
  case mPCIe_DAAI12_4E:
  case mPCIe_DA16_8:
  case mPCIe_DA16_4:
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
  case MPCIE_DIO_24X:
    break;

  default:
        if (pci_resource_flags(pdev, 0) & IORESOURCE_IO)
        {
          plx_bar = 0;
        }
        else
        {
          plx_bar = 1;
        }

        apci_debug("dev_id = %04x. plx_bar = %d\n", ddata->dev_id, plx_bar);

        ddata->plx_region.start = pci_resource_start(pdev, plx_bar);
        if (!ddata->plx_region.start)
        {
          apci_error("Invalid PLX bar %d on start ", plx_bar);
        }

        ddata->plx_region.end = pci_resource_end(pdev, plx_bar);
        if (!ddata->plx_region.end)
        {
          apci_error("Invalid PLX bar %d on end", plx_bar);
        }
        ddata->plx_region.flags = pci_resource_flags(pdev, plx_bar);

        ddata->plx_region.length = ddata->plx_region.end - ddata->plx_region.start + 1;
        ddata->regions[plx_bar].start = ddata->plx_region.start;
        ddata->regions[plx_bar].end = ddata->plx_region.end;
        ddata->regions[plx_bar].flags = ddata->plx_region.flags;
        ddata->regions[plx_bar].length = ddata->plx_region.length;

        apci_debug("plx_region.start = %08llx\n", ddata->plx_region.start);
        apci_devel("plx_region.end   = %08llx\n", ddata->plx_region.end);
        apci_devel("plx_region.length= %08x\n", ddata->plx_region.length);
        apci_info("plx_region.flags = %08lx\n", ddata->plx_region.flags);

        if (ddata->plx_region.flags & IORESOURCE_IO)
        {
          presource = request_region(ddata->plx_region.start, ddata->plx_region.length, "apci");
          if (presource == NULL)
          {
            /* We couldn't get the region.  We have only allocated
            * ddata so release it and return an error.
            */
            apci_error("Unable to request region.\n");
            goto out_alloc_driver;
          }
        }
        else
        {
          ddata->plx_region.mapped_address = ioremap(ddata->plx_region.start, ddata->plx_region.length);
          if (ddata->plx_region.mapped_address == NULL)
          {
            apci_error("Unable to map PLX BAR %d.\n", plx_bar);
            release_mem_region(ddata->plx_region.start, ddata->plx_region.length);
            goto out_alloc_driver;
          }
          ddata->regions[plx_bar].mapped_address = ddata->plx_region.mapped_address;
        }
        break;
      }
      /* TODO: request and remap the region for plx */

  switch (ddata->dev_id)
  {
  case PCIe_DIO_24: /* group1 */
  case PCIe_DIO_24S:
  case MPCIE_DIO_24S_R1:
  case MPCIE_IDIO_8:
  case MPCIE_IIRO_8:
  case MPCIE_IDIO_4:
  case MPCIE_IIRO_4:
  case MPCIE_IDO_8:
  case MPCIE_ISODIO_16:
  case MPCIE_RO_8:
  case MPCIE_II_16:
  case MPCIE_II_8:
  case MPCIE_II_4:
  case MPCIE_QUAD_4:
  case MPCIE_QUAD_8:
  case MPCIE_IDIO_8HL:
  case MPCIE_IDIO_8H:
  case MPCIE_IDIO_8L:
  case PCI_QUAD_8:
  case PCI_QUAD_4:
  case MPCIE_DIO_24:
  case MPCIE_DIO_24S:
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
    ddata->regions[1].start = pci_resource_start(pdev, 1);
    if (ddata->regions[1].start != 0)
    {
      ddata->regions[1].end = pci_resource_end(pdev, 1);
      ddata->regions[1].flags = pci_resource_flags(pdev, 1);
      ddata->regions[1].length = ddata->regions[1].end - ddata->regions[1].start + 1;
      apci_debug("regions[1].start = %08llx\n", ddata->regions[1].start);
      apci_info("regions[1].end   = %08llx\n", ddata->regions[1].end);
      apci_devel("regions[1].length= %08x\n", ddata->regions[1].length);
      apci_info("regions[1].flags = %08lx\n", ddata->regions[1].flags);
    }

    ddata->regions[2].start = pci_resource_start(pdev, 2);
    ddata->regions[2].end = pci_resource_end(pdev, 2);
    ddata->regions[2].flags = pci_resource_flags(pdev, 2);
    ddata->regions[2].length = ddata->regions[2].end - ddata->regions[2].start + 1;
    ddata->irq = pdev->irq;
    ddata->irq_capable = 1;
    apci_debug("regions[2].start = %08llx\n", ddata->regions[2].start);
    apci_info("regions[2].end   = %08llx\n", ddata->regions[2].end);
    apci_devel("regions[2].length= %08x\n", ddata->regions[2].length);
    apci_debug("regions[2].flags = %08lx\n", ddata->regions[2].flags);
    apci_devel("irq = %d\n", ddata->irq);
    break;

  case PCI_IDO_48: /* group2 */
    ddata->regions[2].start = pci_resource_start(pdev, 2);
    ddata->regions[2].end = pci_resource_end(pdev, 2);
    ddata->regions[2].flags = pci_resource_flags(pdev, 2);
    ddata->regions[2].length = ddata->regions[2].end - ddata->regions[2].start + 1;
    apci_debug("regions[2].start = %08llx\n", ddata->regions[2].start);
    apci_info("regions[2].end   = %08llx\n", ddata->regions[2].end);
    apci_devel("regions[2].length= %08x\n", ddata->regions[2].length);
    apci_info("regions[2].flags = %08lx\n", ddata->regions[2].flags);
    apci_devel("NO irq\n");
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
  case mPCIe_ADIODF16_8FDS:
  case mPCIe_ADIODF16_8F:
  case mPCIe_ADIODF16_8A:
  case mPCIe_ADIODF16_8E:
  case mPCIe_ADIODF12_8A:
  case mPCIe_ADIODF12_8:
  case mPCIe_ADIODF12_8E:
  case mPCIe_ADIOHC16_8FDS:
  case mPCIe_ADIOHC16_8F:
  case mPCIe_ADIOHC16_8A:
  case mPCIe_ADIOHC16_8E:
  case mPCIe_ADIOHC12_8A:
  case mPCIe_ADIOHC12_8:
  case mPCIe_ADIOHC12_8E:
  case mPCIe_DAAI16_8F:
  case mPCIe_DAAI16_8A:
  case mPCIe_DAAI16_8E:
  case mPCIe_DAAI12_8:
  case mPCIe_DAAI12_8E:
  case mPCIe_DAAI12_8A:
  case mPCIe_DAAI16_4F:
  case mPCIe_DAAI16_4A:
  case mPCIe_DAAI16_4E:
  case mPCIe_DAAI12_4A:
  case mPCIe_DAAI12_4:
  case mPCIe_DAAI12_4E:
  case mPCIe_DA16_8:
  case mPCIe_DA16_4:
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
    ddata->regions[0].length = ddata->regions[0].end - ddata->regions[0].start + 1;

    ddata->regions[1].start = pci_resource_start(pdev, 2);
    ddata->regions[1].end = pci_resource_end(pdev, 2);
    ddata->regions[1].flags = pci_resource_flags(pdev, 2);
    ddata->regions[1].length = ddata->regions[1].end - ddata->regions[1].start + 1;

    ddata->irq = pdev->irq;
    ddata->irq_capable = 1;
    apci_debug("[%04x]: regions[0].start = %08llx\n", ddata->dev_id, ddata->regions[0].start);
    apci_info ("        regions[0].end   = %08llx\n", ddata->regions[0].end);
    apci_devel("        regions[0].length= %08x\n", ddata->regions[0].length);
    apci_info ("        regions[0].flags = %08lx\n", ddata->regions[0].flags);
    apci_debug("        regions[1].start = %08llx\n", ddata->regions[1].start);
    apci_info ("        regions[1].end   = %08llx\n", ddata->regions[1].end);
    apci_devel("        regions[1].length= %08x\n", ddata->regions[1].length);
    apci_info ("        regions[1].flags = %08lx\n", ddata->regions[1].flags);
    apci_debug("        irq = %d\n", ddata->irq);
    break;

  case MPCIE_DIO_24X:
    ddata->regions[0].start = pci_resource_start(pdev, 0);
    ddata->regions[0].end = pci_resource_end(pdev, 0);
    ddata->regions[0].flags = pci_resource_flags(pdev, 0);
    ddata->regions[0].length = ddata->regions[0].end - ddata->regions[0].start + 1;

    ddata->regions[1].start = pci_resource_start(pdev, 1);
    ddata->regions[1].end = pci_resource_end(pdev, 1);
    ddata->regions[1].flags = pci_resource_flags(pdev, 1);
    ddata->regions[1].length = ddata->regions[1].end - ddata->regions[1].start + 1;

    ddata->irq = pdev->irq;
    ddata->irq_capable = 1;
    apci_debug("[%04x]: regions[0].start = %08llx\n", ddata->dev_id, ddata->regions[0].start);
    apci_info ("        regions[0].end   = %08llx\n", ddata->regions[0].end);
    apci_devel("        regions[0].length= %08x\n", ddata->regions[0].length);
    apci_info ("        regions[0].flags = %08lx\n", ddata->regions[0].flags);
    apci_debug("        regions[1].start = %08llx\n", ddata->regions[1].start);
    apci_info ("        regions[1].end   = %08llx\n", ddata->regions[1].end);
    apci_devel("        regions[1].length= %08x\n", ddata->regions[1].length);
    apci_info ("        regions[1].flags = %08lx\n", ddata->regions[1].flags);
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
    ddata->regions[2].length = ddata->regions[2].end - ddata->regions[2].start + 1;

    ddata->regions[3].start = pci_resource_start(pdev, 3);
    ddata->regions[3].end = pci_resource_end(pdev, 3);
    ddata->regions[3].flags = pci_resource_flags(pdev, 3);
    ddata->regions[3].length = ddata->regions[3].end - ddata->regions[3].start + 1;

    ddata->irq = pdev->irq;
    ddata->irq_capable = 1;
    apci_debug("regions[2].start = %08llx\n", ddata->regions[2].start);
    apci_info ("regions[2].end   = %08llx\n", ddata->regions[2].end);
    apci_devel("regions[2].length= %08x\n", ddata->regions[2].length);
    apci_info ("regions[2].flags = %08lx\n", ddata->regions[2].flags);
    apci_debug("regions[3].start = %08llx\n", ddata->regions[3].start);
    apci_info ("regions[3].end   = %08llx\n", ddata->regions[3].end);
    apci_devel("regions[3].length= %08x\n", ddata->regions[3].length);
    apci_info ("regions[3].flags = %08lx\n", ddata->regions[3].flags);
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
    ddata->regions[2].length = ddata->regions[2].end - ddata->regions[2].start + 1;

    ddata->regions[3].start = pci_resource_start(pdev, 3);
    ddata->regions[3].end = pci_resource_end(pdev, 3);
    ddata->regions[3].flags = pci_resource_flags(pdev, 3);
    ddata->regions[3].length = ddata->regions[3].end - ddata->regions[3].start + 1;
    apci_debug("regions[2].start = %08llx\n", ddata->regions[2].start);
    apci_info ("regions[2].end   = %08llx\n", ddata->regions[2].end);
    apci_devel("regions[2].length= %08x\n", ddata->regions[2].length);
    apci_info ("regions[2].flags = %08lx\n", ddata->regions[2].flags);
    apci_debug("regions[3].start = %08llx\n", ddata->regions[3].start);
    apci_info ("regions[3].end   = %08llx\n", ddata->regions[3].end);
    apci_devel("regions[3].length= %08x\n", ddata->regions[3].length);
    apci_info ("regions[3].flags = %08lx\n", ddata->regions[3].flags);
    apci_debug("NO irq\n");
    break;
    case MPCIE_DIO_24A:

      ddata->regions[0].start = pci_resource_start(pdev, 0);
      ddata->regions[0].end = pci_resource_end(pdev, 0);
      ddata->regions[0].flags = pci_resource_flags(pdev, 0);
      ddata->regions[0].length = ddata->regions[0].end - ddata->regions[0].start + 1;

      ddata->regions[1].start = pci_resource_start(pdev, 1);
      ddata->regions[1].end = pci_resource_end(pdev, 1);
      ddata->regions[1].flags = pci_resource_flags(pdev, 1);
      ddata->regions[1].length = ddata->regions[1].end - ddata->regions[1].start + 1;

      ddata->irq = pdev->irq;
      ddata->irq_capable = 1;
      apci_debug("[%04x]: regions[0].start = %08llx\n", ddata->dev_id, ddata->regions[0].start);
      apci_debug("        regions[0].end   = %08llx\n", ddata->regions[0].end);
      apci_debug("        regions[0].length= %08x\n", ddata->regions[0].length);
      apci_debug("        regions[0].flags = %lx\n", ddata->regions[0].flags);
      apci_debug("        regions[1].start = %08llx\n", ddata->regions[1].start);
      apci_debug("        regions[1].end   = %08llx\n", ddata->regions[1].end);
      apci_debug("        regions[1].length= %08x\n", ddata->regions[1].length);
      apci_debug("        regions[1].flags = %lx\n", ddata->regions[1].flags);
      apci_debug("        irq = %d\n", ddata->irq);
      break;
    default:
      ddata->regions[0].start = pci_resource_start(pdev, 0);
      ddata->regions[0].end = pci_resource_end(pdev, 0);
      ddata->regions[0].flags = pci_resource_flags(pdev, 0);
      ddata->regions[0].length = ddata->regions[0].end - ddata->regions[0].start + 1;

      ddata->regions[1].start = pci_resource_start(pdev, 1);
      ddata->regions[1].end = pci_resource_end(pdev, 1);
      ddata->regions[1].flags = pci_resource_flags(pdev, 1);
      ddata->regions[1].length = ddata->regions[1].end - ddata->regions[1].start + 1;

      ddata->regions[2].start = pci_resource_start(pdev, 2);
      ddata->regions[2].end = pci_resource_end(pdev, 2);
      ddata->regions[2].flags = pci_resource_flags(pdev, 2);
      ddata->regions[2].length = ddata->regions[2].end - ddata->regions[2].start + 1;

      ddata->regions[3].start = pci_resource_start(pdev, 3);
      ddata->regions[3].end = pci_resource_end(pdev, 3);
      ddata->regions[3].flags = pci_resource_flags(pdev, 3);
      ddata->regions[3].length = ddata->regions[3].end - ddata->regions[3].start + 1;
      ddata->irq_capable = 0;
      apci_debug("[%04x]: regions[0].start = %08llx\n", ddata->dev_id, ddata->regions[0].start);
      apci_debug("        regions[0].end   = %08llx\n", ddata->regions[0].end);
      apci_debug("        regions[0].length= %08x\n", ddata->regions[0].length);
      apci_debug("        regions[0].flags = %lx\n", ddata->regions[0].flags);
      apci_debug("        regions[1].start = %08llx\n", ddata->regions[1].start);
      apci_debug("        regions[1].end   = %08llx\n", ddata->regions[1].end);
      apci_debug("        regions[1].length= %08x\n", ddata->regions[1].length);
      apci_debug("        regions[1].flags = %lx\n", ddata->regions[1].flags);
      apci_debug("        regions[2].start = %08llx\n", ddata->regions[2].start);
      apci_debug("        regions[2].end   = %08llx\n", ddata->regions[2].end);
      apci_debug("        regions[2].length= %08x\n", ddata->regions[2].length);
      apci_debug("        regions[2].flags = %lx\n", ddata->regions[2].flags);
      apci_debug("        regions[3].start = %08llx\n", ddata->regions[3].start);
      apci_debug("        regions[3].end   = %08llx\n", ddata->regions[3].end);
      apci_debug("        regions[3].length= %08x\n", ddata->regions[3].length);
      apci_debug("        regions[3].flags = %lx\n", ddata->regions[3].flags);
      apci_debug("        NO irq\n");

      break;
    }

  // cards where we support DMA. So far just the mPCIe_AI*_proto cards
  switch (ddata->dev_id)
  {
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

  case mPCIe_DA16_8  :
  case mPCIe_DA16_4  :
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
  case mPCIe_ADIODF16_8FDS:
  case mPCIe_ADIODF16_8F:
  case mPCIe_ADIODF16_8A:
  case mPCIe_ADIODF16_8E:
  case mPCIe_ADIODF12_8A:
  case mPCIe_ADIODF12_8:
  case mPCIe_ADIODF12_8E:

  case mPCIe_ADIOHC16_8FDS:
  case mPCIe_ADIOHC16_8F:
  case mPCIe_ADIOHC16_8A:
  case mPCIe_ADIOHC16_8E:
  case mPCIe_ADIOHC12_8A:
  case mPCIe_ADIOHC12_8:
  case mPCIe_ADIOHC12_8E:

  case mPCIe_DAAI16_8F:
  case mPCIe_DAAI16_8A:
  case mPCIe_DAAI16_8E:
  case mPCIe_DAAI12_8A:
  case mPCIe_DAAI12_8 :
  case mPCIe_DAAI12_8E:
  case mPCIe_DAAI16_4F:
  case mPCIe_DAAI16_4A:
  case mPCIe_DAAI16_4E:
  case mPCIe_DAAI12_4A:
  case mPCIe_DAAI12_4 :
  case mPCIe_DAAI12_4E:

    apci_devel("setting up DMA in alloc\n");
    ddata->regions[0].start = pci_resource_start(pdev, 0);
    ddata->regions[0].end = pci_resource_end(pdev, 0);
    ddata->regions[0].flags = pci_resource_flags(pdev, 0);
    ddata->regions[0].length = ddata->regions[0].end - ddata->regions[0].start + 1;

    /* Native PCIe DMA devices do not have a separately mapped PLX BAR here. */
    apci_debug("regions[0].start = %08llx\n", ddata->regions[0].start);
    apci_info ("regions[0].end   = %08llx\n", ddata->regions[0].end);
    apci_devel("regions[0].length= %08x\n", ddata->regions[0].length);
    apci_info ("regions[0].flags = %lx\n", ddata->regions[0].flags);
    break;
  }

  /* request regions */
  for (count = 0; count < 6; count++)
  {
    if (ddata->regions[count].start == 0)
    {
      continue; /* invalid region */
    }
    /* Skip regions that overlap with the already-requested PLX region */
    if (ddata->plx_region.start != 0 && ddata->regions[count].start == ddata->plx_region.start)
    {
      continue;
    }
    if (ddata->regions[count].flags & IORESOURCE_IO)
    {

      apci_debug("requesting io region start=%08llx,len=%d\n", ddata->regions[count].start, ddata->regions[count].length);
      presource = request_region(ddata->regions[count].start, ddata->regions[count].length, "apci");
    }
    else
    {
      apci_debug("requesting mem region start=%08llx,len=%d\n", ddata->regions[count].start, ddata->regions[count].length);
      presource = request_mem_region(ddata->regions[count].start, ddata->regions[count].length, "apci");
      if (presource != NULL)
      {
        ddata->regions[count].mapped_address = ioremap(ddata->regions[count].start, ddata->regions[count].length);
        if (ddata->regions[count].mapped_address == NULL)
        {
          apci_error("Unable to map mem region %d.\n", count);
          release_mem_region(ddata->regions[count].start, ddata->regions[count].length);
          presource = NULL;
        }
      }
    }

    if (presource == NULL)
    {
      /* If we failed to allocate the region. */
      count--;

      while (count >= 0)
      {
        if (ddata->regions[count].start != 0)
        {
          apci_release_io_region(&ddata->regions[count]);
        }
        count--;
      }
      goto out_alloc_driver;
    }
  }


  // cards where we support DMA. So far just the mPCIe_AI*(_proto) cards
  switch (ddata->dev_id)
  {
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
  case mPCIe_DA16_8:
  case mPCIe_DA16_4:
  case mPCIe_ADIODF16_8FDS:
  case mPCIe_ADIODF16_8F:
  case mPCIe_ADIODF16_8A:
  case mPCIe_ADIODF16_8E:
  case mPCIe_ADIODF12_8A:
  case mPCIe_ADIODF12_8:
  case mPCIe_ADIODF12_8E:
  case mPCIe_ADIOHC16_8FDS:
  case mPCIe_ADIOHC16_8F:
  case mPCIe_ADIOHC16_8A:
  case mPCIe_ADIOHC16_8E:
  case mPCIe_ADIOHC12_8A:
  case mPCIe_ADIOHC12_8:
  case mPCIe_ADIOHC12_8E:

  case mPCIe_DAAI16_8F:
  case mPCIe_DAAI16_8A:
  case mPCIe_DAAI16_8E:
  case mPCIe_DAAI12_8A:
  case mPCIe_DAAI12_8:
  case mPCIe_DAAI12_8E:
  case mPCIe_DAAI16_4F:
  case mPCIe_DAAI16_4A:
  case mPCIe_DAAI16_4E:
  case mPCIe_DAAI12_4A:
  case mPCIe_DAAI12_4:
  case mPCIe_DAAI12_4E:

    spin_lock_init(&(ddata->dma_data_lock));
    ddata->plx_region = ddata->regions[0];
    apci_devel("DMA spinlock init\n");
    break;
  }

  return ddata;

out_alloc_driver:
  apci_info("exit via out_alloc_driver, about to kfree ddata\n");
  kfree(ddata);
  apci_devel("exit via out_alloc_driver, just kfreed\n");
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

  if (ddata == NULL)
    return;

  if (ddata->regions[0].mapped_address != NULL && ddata->regions[0].length > 0x1C)
    iowrite8(0x08, ddata->regions[0].mapped_address + 0x1C); // abort and clear DMA operation

  for (count = 0; count < 6; count++)
    apci_release_io_region(&ddata->regions[count]);

  if (NULL != ddata->dac_fifo_buffer)
  {
    kfree(ddata->dac_fifo_buffer);
  }
  pci_set_drvdata(pdev, NULL);
  kfree(ddata);
  apci_debug("Completed freeing driver.\n");
}

static void apci_class_dev_unregister(struct apci_my_info *ddata)
{
  struct apci_lookup_table_entry *obj = &apci_driver_table[APCI_LOOKUP_ENTRY((int)ddata->id->device)];
  apci_devel("entering apci_class_dev_unregister\n");
  /* ddata->dev = device_create(class_apci, &ddata->pci_dev->dev , apci_first_dev + id, NULL, "apci/%s_%d", obj->name, obj->counter ++ ); */

  apci_devel("entering apci_class_dev_unregister.\n");
  if (ddata->dev == NULL)
    return;

  device_unregister(ddata->dev);
  obj->counter--;
  dev_counter--;

  apci_devel("leaving apci_class_dev_unregister.\n");
}

static int __devinit
apci_class_dev_register(struct apci_my_info *ddata)
{
  int ret;
  struct apci_lookup_table_entry *obj = &apci_driver_table[APCI_LOOKUP_ENTRY((int)ddata->id->device)];
  apci_devel("entering apci_class_dev_register\n");

  ddata->dev = device_create(class_apci, &ddata->pci_dev->dev, apci_first_dev + dev_counter, NULL, "apci/%s_%d", obj->name, obj->counter);

  /* add pointer to the list of all ACCES I/O products */

  if (IS_ERR(ddata->dev))
  {
    apci_error("Error creating device");
    ret = PTR_ERR(ddata->dev);
    ddata->dev = NULL;
    return ret;
  }
  obj->counter++;
  dev_counter++;
  apci_devel("leaving apci_class_dev_register\n");
  return 0;
}

static void apci_log_dma_discard(struct apci_my_info *ddata, u32 irq_event,
                                 int last_buffer, int first_valid,
                                 u64 discarded_total)
{
  if (ddata == NULL || ddata->pci_dev == NULL)
    return;

  dev_warn_ratelimited(&ddata->pci_dev->dev,
                       "ISR: data discarded: bdf=%s vendor=%04x device=%04x irq=%d irq_type=%s irq_event=0x%08x last_buffer=%d first_valid=%d dma_slots=%d slot_size=%zu discarded_since_last_query=%d discarded_total=%llu\n",
                       pci_name(ddata->pci_dev),
                       ddata->pci_dev->vendor,
                       ddata->pci_dev->device,
                       ddata->irq,
                       ddata->irq_msi_enabled ? "MSI" : "legacy INTx",
                       irq_event,
                       last_buffer,
                       first_valid,
                       ddata->dma_num_slots,
                       ddata->dma_slot_size,
                       ddata->dma_data_discarded,
                       (unsigned long long)discarded_total);
}

irqreturn_t apci_interrupt(int irq, void *dev_id)
{
  struct apci_my_info *ddata;
  __u8 byte;
  __u32 dword;
  bool notify_user = true;
  uint32_t irq_event = 0;

  ddata = (struct apci_my_info *)dev_id;
  switch (ddata->dev_id)
  {
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
  case mPCIe_DAAI16_8F:
  case mPCIe_DAAI16_8A:
  case mPCIe_DAAI16_8E:
  case mPCIe_DAAI12_8A:
  case mPCIe_DAAI12_8:
  case mPCIe_DAAI12_8E:
  case mPCIe_DAAI16_4F:
  case mPCIe_DAAI16_4A:
  case mPCIe_DAAI16_4E:
  case mPCIe_DAAI12_4A:
  case mPCIe_DAAI12_4:
  case mPCIe_DAAI12_4E:
  case mPCIe_DA16_8:
  case mPCIe_DA16_4:
  case mPCIe_ADIODF16_8FDS:
  case mPCIe_ADIODF16_8F:
  case mPCIe_ADIODF16_8A:
  case mPCIe_ADIODF16_8E:
  case mPCIe_ADIODF12_8A:
  case mPCIe_ADIODF12_8:
  case mPCIe_ADIODF12_8E:
  case mPCIe_ADIOHC16_8FDS:
  case mPCIe_ADIOHC16_8F:
  case mPCIe_ADIOHC16_8A:
  case mPCIe_ADIOHC16_8E:
  case mPCIe_ADIOHC12_8A:
  case mPCIe_ADIOHC12_8:
  case mPCIe_ADIOHC12_8E:

    break;

  default:
    /* The first thing we do is check to see if the card is causing an IRQ.
     * If it is then we can proceed to clear the IRQ. Otherwise let
     * Linux know that it wasn't us.
     */
    if (!ddata->is_pcie)
    {
      byte = inb(ddata->plx_region.start + 0x4C);

      if ((byte & 4) == 0)
      {
        return IRQ_NONE; /* not me */
      }
    }
    else
    { /* PCIe */
      if (ddata->plx_region.flags & IORESOURCE_IO)
      {
        byte = inb(ddata->plx_region.start + 0x69);
      }
      else
      {
        byte = ioread8(ddata->plx_region.mapped_address + 0x69);
      }

      if ((byte & 0x80) == 0)
      {
        return IRQ_NONE; /* not me */
      }
    }
  } // handle 9052 & 8311 "IAm" IRQ Flags

  apci_devel("ISR called.\n");

  /* Handle interrupt based on the device ID for the board. */
  switch (ddata->dev_id)
  {
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
  case MPCIE_DIO_24A:
  case MPCIE_DIO_24X:
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

  case P104_DIO_96:  /* unknown at this time 6-FEB-2007 */
  case P104_DIO_48S: /* unknown at this time 6-FEB-2007 */
    break;
  case MPCIE_IDIO_8:
  case MPCIE_IIRO_8:
  case MPCIE_IDIO_4:
  case MPCIE_IIRO_4:
  case MPCIE_IDO_8:
  case MPCIE_ISODIO_16:
  case MPCIE_RO_8:
  case MPCIE_II_16:
  case MPCIE_II_8:
  case MPCIE_II_4:
  case MPCIE_IDIO_8HL:
  case MPCIE_IDIO_8H:
  case MPCIE_IDIO_8L:

    outb(0xff, ddata->regions[2].start + 41);
    break;

  case MPCIE_QUAD_4:
  case PCI_QUAD_4:
  {
    int i;
    for (i = 0; i < 4; i++)
    {
      byte = inb(ddata->regions[2].start + 0x8 * i + 7);
      if (0 == (byte & 0x80))
        break;

      if (byte & 0x08)
        outb(0x08, ddata->regions[2].start + 0x8 * i + 6);
    }
  }
  break;

  case MPCIE_QUAD_8:
  case PCI_QUAD_8:
  {
    int i;
    for (i = 0; i < 8; i++)
    {
      byte = inb(ddata->regions[2].start + 0x8 * i + 7);
      if (0 == (byte & 0x80))
        break;

      if (byte & 0x08)
        outb(0x08, ddata->regions[2].start + 0x8 * i + 6);
    }
  }
  break;

  case PCIe_IDIO_12:
  case PCIe_IDIO_24:
    /* read 32 bits from +8 to determine which specific bits have generated a CoS IRQ then write the same value back to +8 to clear those CoS latches */
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
    //All of the _proto cards should not be in production. If you are wondering
    //which DMA section to look at it is probably the one below
    apci_devel("ISR: mPCIe-AI irq_event !!PROTOTYPE!!\n");
    // If this is a FIFO near full IRQ then tell the card
    // to write to the next buffer (and don't notify the user?)
    // else if it is a write done IRQ set last_valid_buffer and notify user
    irq_event = ioread8(ddata->regions[2].mapped_address + 0x2);
    if (irq_event == 0)
    {
      apci_devel("ISR: not our IRQ\n");
      return IRQ_NONE;
    }

    if (irq_event & 0x1) // FIFO almost full
    {
      dma_addr_t base = ddata->dma_addr;
      spin_lock(&(ddata->dma_data_lock));
      if (ddata->dma_first_valid == -1)
      {
        ddata->dma_first_valid = 0;
      }

      ddata->dma_last_buffer++;
      ddata->dma_last_buffer %= ddata->dma_num_slots;

      if (ddata->dma_last_buffer == ddata->dma_first_valid)
      {
        int discard_last_buffer;
        int discard_first_valid;
        u64 discarded_total;

        ddata->dma_last_buffer--;
        if (ddata->dma_last_buffer < 0)
          ddata->dma_last_buffer = ddata->dma_num_slots - 1;
        ddata->dma_data_discarded++;
        ddata->dma_data_discarded_total++;
        discard_last_buffer = ddata->dma_last_buffer;
        discard_first_valid = ddata->dma_first_valid;
        discarded_total = ddata->dma_data_discarded_total;
        spin_unlock(&(ddata->dma_data_lock));
        apci_log_dma_discard(ddata, irq_event, discard_last_buffer,
                             discard_first_valid, discarded_total);
      }
      else
      {
        spin_unlock(&(ddata->dma_data_lock));
      }
      base += ddata->dma_slot_size * ddata->dma_last_buffer;

      iowrite32(base & 0xffffffff, ddata->regions[0].mapped_address);
      iowrite32(base >> 32, ddata->regions[0].mapped_address + 4);
      iowrite32(ddata->dma_slot_size, ddata->regions[0].mapped_address + 8);
      iowrite32(4, ddata->regions[0].mapped_address + 12);
    }

    iowrite8(irq_event, ddata->regions[2].mapped_address + 0x2);
    apci_debug("ISR: irq_event = 0x%x, depth = 0x%x\n", irq_event, ioread32(ddata->regions[2].mapped_address + 0x28));
    irq_event = ioread8(ddata->regions[2].mapped_address + 0x2);
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
  case mPCIe_ADI12_8E:
  case mPCIe_ADIODF16_8FDS:
  case mPCIe_ADIODF16_8F:
  case mPCIe_ADIODF16_8A:
  case mPCIe_ADIODF16_8E:
  case mPCIe_ADIODF12_8A:
  case mPCIe_ADIODF12_8:
  case mPCIe_ADIODF12_8E:
  case mPCIe_ADIOHC16_8FDS:
  case mPCIe_ADIOHC16_8F:
  case mPCIe_ADIOHC16_8A:
  case mPCIe_ADIOHC16_8E:
  case mPCIe_ADIOHC12_8A:
  case mPCIe_ADIOHC12_8:
  case mPCIe_ADIOHC12_8E:
  case mPCIe_DAAI16_8F:
  case mPCIe_DAAI16_8A:
  case mPCIe_DAAI16_8E:
  case mPCIe_DAAI12_8:
  case mPCIe_DAAI12_8E:
  case mPCIe_DAAI12_8A:
  case mPCIe_DAAI16_4F:
  case mPCIe_DAAI16_4A:
  case mPCIe_DAAI16_4E:
  case mPCIe_DAAI12_4A:
  case mPCIe_DAAI12_4:
  case mPCIe_DAAI12_4E:
  case mPCIe_DA16_8:
  case mPCIe_DA16_4:

  {
    apci_devel("ISR: mPCIe-AxIO irq_event\n");
    // If this is a FIFO near full IRQ then tell the card
    // to write to the next buffer (and don't notify the user)
    // else if it is a write done IRQ set last_valid_buffer and notify user
    irq_event = ioread32(ddata->regions[1].mapped_address + mPCIe_ADIO_IRQStatusAndClearOffset); // TODO: Upgrade to doRegisterAction("AmI?")

    if ((irq_event & mPCIe_ADIO_IRQEventMask) == 0)
    {
      apci_devel("ISR: not our IRQ\n");
      return IRQ_NONE;
    }

    if (irq_event & (bmADIO_ADCTRIGGERStatus | bmADIO_DMADoneStatus))
    {
      dma_addr_t base = ddata->dma_addr;
      spin_lock(&(ddata->dma_data_lock));
      if (ddata->dma_last_buffer == -1)
      {
        notify_user = false;
        apci_debug("ISR First IRQ");
      }
      else if (ddata->dma_first_valid == -1)
      {
        ddata->dma_first_valid = 0;
      }

      ddata->dma_last_buffer++;
      ddata->dma_last_buffer %= ddata->dma_num_slots;

      if (ddata->dma_last_buffer == ddata->dma_first_valid)
      {
        int discard_last_buffer;
        int discard_first_valid;
        u64 discarded_total;

        ddata->dma_last_buffer--;
        if (ddata->dma_last_buffer < 0)
          ddata->dma_last_buffer = ddata->dma_num_slots - 1;
        ddata->dma_data_discarded++;
        ddata->dma_data_discarded_total++;
        discard_last_buffer = ddata->dma_last_buffer;
        discard_first_valid = ddata->dma_first_valid;
        discarded_total = ddata->dma_data_discarded_total;
        spin_unlock(&(ddata->dma_data_lock));
        apci_log_dma_discard(ddata, irq_event, discard_last_buffer,
                             discard_first_valid, discarded_total);
      }
      else
      {
        spin_unlock(&(ddata->dma_data_lock));
      }
      base += ddata->dma_slot_size * ddata->dma_last_buffer;

      iowrite32(base & 0xffffffff, ddata->regions[0].mapped_address + 0x10);
      iowrite32(base >> 32, ddata->regions[0].mapped_address + 4 + 0x10);
      iowrite32(ddata->dma_slot_size, ddata->regions[0].mapped_address + 8 + 0x10);
      iowrite32(4, ddata->regions[0].mapped_address + 12 + 0x10);
      ioread32(ddata->regions[0].mapped_address + 12 + 0x10); // flush posted PCI writes
    }

    {
      u32 clear_event = irq_event;

      /*
       * On PCIe-ADIO16-16FDS firmware that implements the undocumented
       * DIO12 FDS-start secondary function, status bit 23 is also the FDS
       * trigger latch.  It is not an APCI-owned interrupt event unless the
       * corresponding LDAC IRQ enable bit is set.  Clearing every latched
       * status bit here can erase the FDS start request before the waveform
       * clock domain consumes it.
       *
       * Preserve the historical read/write-back behavior for every other
       * device and status source.  For C2EF only, leave bit 23 asserted when
       * its IRQ source is disabled; if enLDAC is set, clear it normally as an
       * actual driver-visible IRQ event.
       */
      if (ddata->dev_id == PCIe_ADIO16_16FDS &&
          !(irq_event & bmADIO_LDACEnable))
        clear_event &= ~bmADIO_LDACStatus;

      iowrite32(clear_event,
                ddata->regions[1].mapped_address +
                mPCIe_ADIO_IRQStatusAndClearOffset);
    }
    apci_debug("ISR: irq_event = 0x%x, depth = 0x%x, IRQStatus = 0x%x\n", irq_event, ioread32(ddata->regions[1].mapped_address + 0x28), ioread32(ddata->regions[1].mapped_address + 0x40));
    irq_event = ioread32(ddata->regions[1].mapped_address + mPCIe_ADIO_IRQStatusAndClearOffset);
    break;
  }
  }; // end card-specific switch

  /* Check to see if we were actually waiting for an IRQ. If we were
   * then we need to wake the queue associated with this device.
   * Right now it is not possible for any other code sections that access
   * the critical data to interrupt us so we won't disable other IRQs.
   */
  if (notify_user)
  {
    /*
     * Latch the user-visible IRQ event even when no thread is currently
     * blocked in apci_wait_for_irq_ioctl.  This avoids a lost-wakeup race
     * between userspace polling apci_data_ready and entering wait_for_irq.
     * Coalesce multiple unconsumed IRQs into one pending token; userspace
     * drains all ready DMA slots before waiting again, so one token is enough
     * to prevent sleeping past already-arrived data.
     */
    spin_lock(&(ddata->irq_lock));
    ddata->irq_event_pending = 1;
    ddata->irq_notify_count++;
    spin_unlock(&(ddata->irq_lock));
    wake_up_interruptible(&(ddata->wait_queue));
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

  if (ddata == NULL)
    return;

  apci_free_device_irq(ddata);

  if (ddata->dma_virt_addr != NULL)
  {
    dma_free_coherent(&(ddata->pci_dev->dev),
                      ddata->dma_num_slots * ddata->dma_slot_size,
                      ddata->dma_virt_addr,
                      ddata->dma_addr);
    ddata->dma_virt_addr = NULL;
    ddata->dma_addr = 0;
  }

  apci_class_dev_unregister(ddata);

  cdev_del(&ddata->cdev);

  spin_lock(&head.driver_list_lock);
  list_for_each_entry_safe(child, _temp, &head.driver_list, driver_list)
  {
    if (child == ddata)
    {
      apci_debug("Removing node with address %p\n", child);
      list_del(&child->driver_list);
      break;
    }
  }
  spin_unlock(&head.driver_list_lock);

  apci_free_driver(pdev);
  pci_clear_master(pdev);
  pci_disable_device(pdev);

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

  ret = pci_enable_device(pdev);
  if (ret)
  {
    apci_debug("pci_enable_device returned fail\n");
    return ret;
  }

  pci_set_master(pdev);

  ddata = (struct apci_my_info *)apci_alloc_driver(pdev, id);
  if (ddata == NULL)
  {
    apci_debug("apci_alloc_driver returned null\n");
    ret = -ENOMEM;
    goto exit_disable_device;
  }

  /* Setup actual device driver items */
  ddata->nchannels = APCI_NCHANNELS;
  ddata->pci_dev = pdev;
  ddata->id = id;
  ddata->is_pcie = (ddata->plx_region.length >= 0x100 ? 1 : 0);
  apci_debug("Is device PCIE : %d\n", ddata->is_pcie);
  ddata->irq_disabled = irq_disabled;
  init_waitqueue_head(&(ddata->wait_queue));

  /* apci_free_driver() uses pci_get_drvdata(), including probe failure paths. */
  pci_set_drvdata(pdev, ddata);

  /* Request Irq */
  if (ddata->irq_capable)
  {
    if (!irq_disabled)
    {
      ret = apci_request_device_irq(ddata);
      if (ret)
        goto exit_free;
    }
    else
    {
      dev_info(&pdev->dev,
               "IRQ is disabled via module param for bdf=%s device=%04x\n",
               pci_name(pdev), ddata->dev_id);
    }

    // TODO: Fix this when HW is available to test MEM version
    if (ddata->is_pcie)
    {
      if (ddata->dev_id != 0xC0E8)
      {
        if (ddata->plx_region.flags & IORESOURCE_IO)
        {
          outb(0x9, ddata->plx_region.start + 0x69);
        }
        else if (ddata->plx_region.mapped_address)
        {
          apci_debug("Enabling IRQ MEM/IO plx region\n");
          iowrite8(0x9, ddata->plx_region.mapped_address + 0x69);
        }
      }
    }
  }

  /* add to sysfs */

  cdev_init(&ddata->cdev, &apci_fops);
  ddata->cdev.owner = THIS_MODULE;

  ret = cdev_add(&ddata->cdev, apci_first_dev + dev_counter, 1);
  if (ret)
  {
    apci_error("error registering Driver %d", dev_counter);
    goto exit_irq;
  }

  /* add to the list of devices supported */
  spin_lock(&head.driver_list_lock);
  list_add(&ddata->driver_list, &head.driver_list);
  spin_unlock(&head.driver_list_lock);

  ret = apci_class_dev_register(ddata);

  if (ret)
    goto exit_list;

  apci_debug("Added driver %d\n", dev_counter - 1);
  apci_debug("Value of irq is %d\n", ddata->irq);
  return 0;

exit_list:
  /* Routine to remove the driver from the list */
  spin_lock(&head.driver_list_lock);
  list_for_each_entry_safe(child, _temp, &head.driver_list, driver_list)
  {
    if (child == ddata)
    {
      apci_debug("Deleting address %p\n", child);
      list_del(&child->driver_list);
      break;
    }
  }
  spin_unlock(&head.driver_list_lock);

  cdev_del(&ddata->cdev);
exit_irq:
  apci_free_device_irq(ddata);
exit_free:
  apci_free_driver(pdev);
exit_disable_device:
  pci_clear_master(pdev);
  pci_disable_device(pdev);
  return ret;
}

// The name of this variable is exposed to userspace via /etc/modprobe.d
static int dev_mode = 0;
module_param(dev_mode, int, 0);

/* Configure the default /dev/{devicename} permissions */
#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 2, 0) || RULES_DONT_APPLY_TO_RH
static char *apci_devnode(const struct device *dev, umode_t *mode)
#else
static char *apci_devnode(struct device *dev, umode_t *mode)
#endif
{
  if (!mode)
    return NULL;
  // Has been changed  build time.
  // This is not recommended, but since it
  if (0 != APCI_DEFAULT_DEVFILE_MODE) // was released this we continue to support it
  {
    *mode = APCI_DEFAULT_DEVFILE_MODE;
  }
  else if (0 != dev_mode) // Has been changed via module parameter.
  {                       // This is the recommended method
    *mode = dev_mode;
  }

  return NULL;
}

int __init
apci_init(void)
{
  void *ptr_err;
  int result;
  int ret;
  dev_t dev = MKDEV(APCI_MAJOR, 0);
  apci_debug("performing init duties\n");
  spin_lock_init(&head.driver_list_lock);
  INIT_LIST_HEAD(&head.driver_list);
  sort(apci_driver_table, APCI_TABLE_SIZE, APCI_TABLE_ENTRY_SIZE, te_sort, NULL);

  ret = alloc_chrdev_region(&apci_first_dev, 0, MAX_APCI_CARDS, APCI);
  if (ret)
  {
    apci_error("Unable to allocate device numbers");
    return ret;
  }

  /* Create the sysfs entry for this */
#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 4, 0) || RULES_DONT_APPLY_TO_RH
  class_apci = class_create(APCI_CLASS_NAME);
#else
  class_apci = class_create(THIS_MODULE, APCI_CLASS_NAME);
#endif
  if (IS_ERR(ptr_err = class_apci))
    goto err;
  class_apci->devnode = apci_devnode; // set device file permissions

  cdev_init(&apci_cdev, &apci_fops);
  apci_cdev.owner = THIS_MODULE;
  apci_cdev.ops = &apci_fops;

  result = cdev_add(&apci_cdev, dev, 1);

#ifdef TEST_CDEV_ADD_FAIL
  result = -1;
  if (result == -1)
  {
    apci_error("Going to delete device.\n");
    cdev_del(&apci_cdev);
    apci_error("Deleted device.\n");
  }
#endif

  if (result < 0)
  {
    apci_error("cdev_add failed in apci_init");
    goto err;
  }

  /* needed to get the probe and remove to be called */
  result = pci_register_driver(&pci_driver);
  apci_error("register driver returned: %d\n", result);
  return 0;
err:

  apci_error("Unregistering chrdev_region.\n");

  unregister_chrdev_region(MKDEV(APCI_MAJOR, 0), 1);

  class_destroy(class_apci);
  return PTR_ERR(ptr_err);
}

static void __exit apci_exit(void)
{
  apci_debug("performing exit duties\n");
  pci_unregister_driver(&pci_driver);
  cdev_del(&apci_cdev);
  unregister_chrdev_region(MKDEV(APCI_MAJOR, 0), 1);
  class_destroy(class_apci);
}

module_init(apci_init);
module_exit(apci_exit);

MODULE_AUTHOR("John Hentges <JHentges@accesio.com>");
MODULE_LICENSE("Dual MIT/GPL");
