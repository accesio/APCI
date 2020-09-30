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

/* PCI table construction */
static struct pci_device_id ids[] = {
        { PCI_DEVICE(A_VENDOR_ID, PCIe_IIRO_8), },
        { PCI_DEVICE(A_VENDOR_ID, PCI_DIO_24D), },
	{ PCI_DEVICE(A_VENDOR_ID, PCI_DA12_4) , },
	{ PCI_DEVICE(A_VENDOR_ID, PCI_DA12_6) , },
	{ PCI_DEVICE(A_VENDOR_ID, PCI_DA12_2) , },
	{ PCI_DEVICE(A_VENDOR_ID, PCI_DA12_2) , },
        { PCI_DEVICE(A_VENDOR_ID, P104_DIO_48S    ), },
        { PCI_DEVICE(A_VENDOR_ID, P104_DIO_96     ), },
        { PCI_DEVICE(A_VENDOR_ID, PCI_DIO_120     ), },
        { PCI_DEVICE(A_VENDOR_ID, PCI_DIO_24D     ), },
        { PCI_DEVICE(A_VENDOR_ID, PCI_DIO_24D_C   ), },
        { PCI_DEVICE(A_VENDOR_ID, PCI_DIO_24H     ), },
        { PCI_DEVICE(A_VENDOR_ID, PCI_DIO_24H_C   ), },
        { PCI_DEVICE(A_VENDOR_ID, PCI_DIO_24S     ), },
        { PCI_DEVICE(A_VENDOR_ID, PCI_DIO_48      ), },
        { PCI_DEVICE(A_VENDOR_ID, PCI_DIO_48H     ), },
        { PCI_DEVICE(A_VENDOR_ID, PCI_DIO_48HS    ), },
        { PCI_DEVICE(A_VENDOR_ID, PCI_DIO_48S     ), },
        { PCI_DEVICE(A_VENDOR_ID, PCI_DIO_72      ), },
        { PCI_DEVICE(A_VENDOR_ID, PCI_DIO_96      ), },
        { PCI_DEVICE(A_VENDOR_ID, PCI_DIO_96C3    ), },
        { PCI_DEVICE(A_VENDOR_ID, PCI_DIO_96CT    ), },
        { PCI_DEVICE(A_VENDOR_ID, PCIe_DIO_24     ), },
        { PCI_DEVICE(A_VENDOR_ID, PCIe_DIO_24D    ), },
        { PCI_DEVICE(A_VENDOR_ID, PCIe_DIO_24DC   ), },
        { PCI_DEVICE(A_VENDOR_ID, PCIe_DIO_24DCS  ), },
        { PCI_DEVICE(A_VENDOR_ID, PCIe_DIO_24DS   ), },
        { PCI_DEVICE(A_VENDOR_ID, PCIe_DIO_24S    ), },
        { PCI_DEVICE(A_VENDOR_ID, PCIe_DIO_48     ), },
        { PCI_DEVICE(A_VENDOR_ID, PCIe_DIO_48S    ), },
        { PCI_DEVICE(A_VENDOR_ID, PCIe_IIRO_16    ), },
        { PCI_DEVICE(A_VENDOR_ID, PCI_IIRO_16     ), },
        { PCI_DEVICE(A_VENDOR_ID, PCI_IIRO_8      ), },
        { PCI_DEVICE(A_VENDOR_ID, PCIe_IIRO_8     ), },
        { PCI_DEVICE(A_VENDOR_ID, MPCIE_DIO_24S   ), },
        { PCI_DEVICE(A_VENDOR_ID, MPCIE_DIO_24S_R1   ), },
        { PCI_DEVICE(A_VENDOR_ID, MPCIE_IDIO_8), },
        { PCI_DEVICE(A_VENDOR_ID, MPCIE_IIRO_8), },
        { PCI_DEVICE(A_VENDOR_ID, MPCIE_IDIO_4), },
        { PCI_DEVICE(A_VENDOR_ID, MPCIE_IIRO_4), },
        { PCI_DEVICE(A_VENDOR_ID, MPCIE_IDO_8 ), },
        { PCI_DEVICE(A_VENDOR_ID, MPCIE_RO_8  ), },
        { PCI_DEVICE(A_VENDOR_ID, MPCIE_II_16 ), },
        { PCI_DEVICE(A_VENDOR_ID, MPCIE_II_8  ), },
        { PCI_DEVICE(A_VENDOR_ID, MPCIE_II_4  ), },
        { PCI_DEVICE(A_VENDOR_ID, MPCIE_QUAD_4  ), },
        { PCI_DEVICE(A_VENDOR_ID, MPCIE_QUAD_8  ), },
        { PCI_DEVICE(A_VENDOR_ID, MPCIE_DIO_24  ), },
        { PCI_DEVICE(A_VENDOR_ID, PCIe_IDIO_12   ), },
        { PCI_DEVICE(A_VENDOR_ID, PCIe_IDIO_24   ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO16_16F ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO16_16A ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO16_16E ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AI16_16F ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AI16_16A ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AI16_16E ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO12_16A ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO12_16 ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO12_16E ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AI12_16A ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AI12_16 ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AI12_16E ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO16_16F_proto ) },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO16_16A_proto ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO16_16E_proto ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AI16_16F_proto ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AI16_16A_proto ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AI16_16E_proto ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO12_16A_proto ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO12_16_proto ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AIO12_16E_proto ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AI12_16A_proto ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AI12_16_proto ), },
        { PCI_DEVICE(A_VENDOR_ID, mPCIe_AI12_16E_proto ), },
        {0,}
};
MODULE_DEVICE_TABLE(pci, ids);


struct apci_lookup_table_entry {
  int board_id;
  int counter;
  char *name;
};


/* Driver naming section */
#define APCI_MAKE_ENTRY(X) { X, 0, NAME_ ## X }
#define APCI_MAKE_DRIVER_TABLE(...) { __VA_ARGS__ }
/* acpi_lookup_table */

static int
te_sort(const void *m1, const void *m2)
{
    struct apci_lookup_table_entry *mi1 = (struct apci_lookup_table_entry *) m1;
    struct apci_lookup_table_entry *mi2 = (struct apci_lookup_table_entry *) m2;
    return ( mi1->board_id < mi2->board_id ? -1 : mi1->board_id != mi2->board_id );
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

#define APCI_TABLE_SIZE  sizeof(apci_driver_table)/sizeof(struct apci_lookup_table_entry)
#define APCI_TABLE_ENTRY_SIZE sizeof( struct apci_lookup_table_entry )


#else
static struct apci_lookup_table_entry apci_driver_table[] = \
  APCI_MAKE_DRIVER_TABLE(
                         APCI_MAKE_ENTRY( PCIe_DIO_24 ),
                         APCI_MAKE_ENTRY( PCIe_DIO_24D ),
                         APCI_MAKE_ENTRY( PCIe_DIO_24S ),
                         APCI_MAKE_ENTRY( PCIe_DIO_24DS ),
                         APCI_MAKE_ENTRY( PCIe_DIO_24DC ),
                         APCI_MAKE_ENTRY( PCIe_DIO_24DCS ),
                         APCI_MAKE_ENTRY( PCIe_DIO_48 ),
                         APCI_MAKE_ENTRY( PCIe_DIO_48S ),
                         APCI_MAKE_ENTRY( PCIe_IIRO_8 ),
                         APCI_MAKE_ENTRY( PCIe_IIRO_16 ),
                         APCI_MAKE_ENTRY( PCI_DIO_24H ),
                         APCI_MAKE_ENTRY( PCI_DIO_24D ),
                         APCI_MAKE_ENTRY( PCI_DIO_24H_C ),
                         APCI_MAKE_ENTRY( PCI_DIO_24D_C ),
                         APCI_MAKE_ENTRY( PCI_DIO_24S ),
                         APCI_MAKE_ENTRY( PCI_DIO_48 ),
                         APCI_MAKE_ENTRY( PCI_DIO_48S ),
                         APCI_MAKE_ENTRY( P104_DIO_48S ),
                         APCI_MAKE_ENTRY( PCI_DIO_48H ),
                         APCI_MAKE_ENTRY( PCI_DIO_48HS ),
                         APCI_MAKE_ENTRY( PCI_DIO_72 ),
                         APCI_MAKE_ENTRY( P104_DIO_96 ),
                         APCI_MAKE_ENTRY( PCI_DIO_96 ),
                         APCI_MAKE_ENTRY( PCI_DIO_96CT ),
                         APCI_MAKE_ENTRY( PCI_DIO_96C3 ),
                         APCI_MAKE_ENTRY( PCI_DIO_120 ),
                         APCI_MAKE_ENTRY( PCI_AI12_16 ),
                         APCI_MAKE_ENTRY( PCI_AI12_16A ),
                         APCI_MAKE_ENTRY( PCI_AIO12_16 ),
                         APCI_MAKE_ENTRY( PCI_A12_16A ),
                         APCI_MAKE_ENTRY( LPCI_A16_16A ),
                         APCI_MAKE_ENTRY( PCI_DA12_16 ),
                         APCI_MAKE_ENTRY( PCI_DA12_8 ),
                         APCI_MAKE_ENTRY( PCI_DA12_6 ),
                         APCI_MAKE_ENTRY( PCI_DA12_4 ),
                         APCI_MAKE_ENTRY( PCI_DA12_2 ),
                         APCI_MAKE_ENTRY( PCI_DA12_16V ),
                         APCI_MAKE_ENTRY( PCI_DA12_8V ),
                         APCI_MAKE_ENTRY( LPCI_IIRO_8 ),
                         APCI_MAKE_ENTRY( PCI_IIRO_8 ),
                         APCI_MAKE_ENTRY( PCI_IIRO_16 ),
                         APCI_MAKE_ENTRY( PCI_IDI_48 ),
                         APCI_MAKE_ENTRY( PCI_IDO_48 ),
                         APCI_MAKE_ENTRY( PCI_IDIO_16 ),
                         APCI_MAKE_ENTRY( PCI_WDG_2S ),
                         APCI_MAKE_ENTRY( PCI_WDG_CSM ),
                         APCI_MAKE_ENTRY( PCI_WDG_IMPAC ),
                         APCI_MAKE_ENTRY( MPCIE_DIO_24S ),
                         APCI_MAKE_ENTRY( MPCIE_DIO_24S_R1 ),
                         APCI_MAKE_ENTRY( MPCIE_IDIO_8 ),
                         APCI_MAKE_ENTRY( MPCIE_IIRO_8 ),
                         APCI_MAKE_ENTRY( MPCIE_IDIO_4 ),
                         APCI_MAKE_ENTRY( MPCIE_IIRO_4 ),
                         APCI_MAKE_ENTRY( MPCIE_IDO_8 ),
                         APCI_MAKE_ENTRY( MPCIE_RO_8 ),
                         APCI_MAKE_ENTRY( MPCIE_II_16 ),
                         APCI_MAKE_ENTRY( MPCIE_II_8 ),
                         APCI_MAKE_ENTRY( MPCIE_II_4 ),
                         APCI_MAKE_ENTRY( MPCIE_QUAD_4 ),
                         APCI_MAKE_ENTRY( MPCIE_QUAD_8 ),
                         APCI_MAKE_ENTRY( MPCIE_DIO_24 ),
                         APCI_MAKE_ENTRY( PCI_WDG_IMPAC ),
                         APCI_MAKE_ENTRY( MPCIE_DIO_24S ),
                         APCI_MAKE_ENTRY( PCIe_IDIO_12 ),
                         APCI_MAKE_ENTRY( PCIe_IDIO_24 ),
                         APCI_MAKE_ENTRY( mPCIe_AIO16_16F ),
                         APCI_MAKE_ENTRY( mPCIe_AIO16_16A ),
                         APCI_MAKE_ENTRY( mPCIe_AIO16_16E ),
                         APCI_MAKE_ENTRY( mPCIe_AI16_16F ),
                         APCI_MAKE_ENTRY( mPCIe_AI16_16A ),
                         APCI_MAKE_ENTRY( mPCIe_AI16_16E ),
                         APCI_MAKE_ENTRY( mPCIe_AIO12_16A ),
                         APCI_MAKE_ENTRY( mPCIe_AIO12_16 ),
                         APCI_MAKE_ENTRY( mPCIe_AIO12_16E ),
                         APCI_MAKE_ENTRY( mPCIe_AI12_16A ),
                         APCI_MAKE_ENTRY( mPCIe_AI12_16 ),
                         APCI_MAKE_ENTRY( mPCIe_AI12_16E ),
                         APCI_MAKE_ENTRY( mPCIe_AIO16_16F_proto),
                         APCI_MAKE_ENTRY( mPCIe_AIO16_16A_proto ),
                         APCI_MAKE_ENTRY( mPCIe_AIO16_16E_proto ),
                         APCI_MAKE_ENTRY( mPCIe_AI16_16F_proto ),
                         APCI_MAKE_ENTRY( mPCIe_AI16_16A_proto ),
                         APCI_MAKE_ENTRY( mPCIe_AI16_16E_proto ),
                         APCI_MAKE_ENTRY( mPCIe_AIO12_16A_proto ),
                         APCI_MAKE_ENTRY( mPCIe_AIO12_16_proto ),
                         APCI_MAKE_ENTRY( mPCIe_AIO12_16E_proto ),
                         APCI_MAKE_ENTRY( mPCIe_AI12_16A_proto ),
                         APCI_MAKE_ENTRY( mPCIe_AI12_16_proto ),
                         APCI_MAKE_ENTRY( mPCIe_AI12_16E_proto ),
                          );

#define APCI_TABLE_SIZE  sizeof(apci_driver_table)/sizeof(struct apci_lookup_table_entry)
#define APCI_TABLE_ENTRY_SIZE sizeof( struct apci_lookup_table_entry )


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




int APCI_LOOKUP_ENTRY(int fx ) {
     struct apci_lookup_table_entry driver_entry;
     struct apci_lookup_table_entry *driver_num;
     driver_entry.board_id = fx;
     driver_num = (struct apci_lookup_table_entry *)bsearch( &driver_entry,
                                                             apci_driver_table,
                                                             APCI_TABLE_SIZE,
                                                             APCI_TABLE_ENTRY_SIZE,
                                                             te_sort
          );

  if( !driver_num ) {
    return -1;
  } else {
    return (int)(driver_num - apci_driver_table );
  }
}
#endif


static struct class *class_apci;

/* PCI Driver setup */
static struct pci_driver pci_driver = {
        .name      = "acpi",
        .id_table  = ids,
        .probe     = probe,
        .remove    = remove
};


/* File Operations */
static struct file_operations apci_fops = {
  .read = read_apci,
  .open = open_apci,
#if  LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39 )
  .ioctl = ioctl_apci,
#else
  .unlocked_ioctl = ioctl_apci,
#endif
  .mmap = mmap_apci,
};

static const int NUM_DEVICES         = 4;
static const char APCI_CLASS_NAME[]  = "apci";
struct cdev apci_cdev;
static int major_num;


static struct class *class_apci;
/* struct apci_my_info *head = NULL; */
struct apci_my_info head;


static int dev_counter = 0;

static dev_t apci_first_dev = MKDEV(0,0);


void *
apci_alloc_driver(struct pci_dev *pdev, const struct pci_device_id *id )
{

    struct apci_my_info *ddata = kmalloc( sizeof( struct apci_my_info ) , GFP_KERNEL );
    int count, plx_bar;
    struct resource *presource;

    if( !ddata )
      return NULL;

    /* Initialize with defaults, fill in specifics later */
    ddata->irq = 0;
    ddata->irq_capable = 0;

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

    if (pci_resource_flags(pdev, 0) & IORESOURCE_IO) {
        plx_bar = 0;
    } else {
        plx_bar = 1;
    }

    apci_error("plx_bar = %d\n", plx_bar);

    ddata->plx_region.start	 = pci_resource_start(pdev, plx_bar);
    if( ! ddata->plx_region.start ) {
      apci_error("Invalid bar %d on start ", plx_bar );
      goto out_alloc_driver;
    }
    ddata->plx_region.end	 = pci_resource_end(pdev, plx_bar);
    if( ! ddata->plx_region.start ) {
      apci_error("Invalid bar %d on end", plx_bar );
      goto out_alloc_driver;
    }
    ddata->plx_region.flags = pci_resource_flags(pdev, plx_bar);


    ddata->plx_region.length = ddata->plx_region.end - ddata->plx_region.start + 1;

    apci_debug("plx_region.start = %08x\n", ddata->plx_region.start );
    apci_debug("plx_region.end   = %08x\n", ddata->plx_region.end );
    apci_debug("plx_region.length= %08x\n", ddata->plx_region.length );

    if (ddata->plx_region.flags & IORESOURCE_IO)
    {
      presource = request_region(ddata->plx_region.start, ddata->plx_region.length, "apci");
      if (presource == NULL) {
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
    }

    /* TODO: request and remap the region for plx */

    switch(ddata->dev_id) {
         case PCIe_DIO_24:/* group1 */
         case PCIe_DIO_24D:
         case PCIe_DIO_24S:
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
         case PCI_AI12_16A:
         case PCI_A12_16A:
         case LPCI_IIRO_8:
         case PCI_IIRO_8:
         case PCI_IIRO_16:
         case PCI_IDI_48:
         case PCI_IDIO_16:
         case PCIe_IDIO_12:
         case PCIe_IDIO_24:
              ddata->regions[2].start   = pci_resource_start(pdev, 2);
              ddata->regions[2].end     = pci_resource_end(pdev, 2);
              ddata->regions[2].flags   = pci_resource_flags(pdev, 2);
              ddata->regions[2].length  = ddata->regions[2].end - ddata->regions[2].start + 1;
              ddata->irq                = pdev->irq;
              ddata->irq_capable        = 1;
              apci_debug("regions[2].start = %08x\n", ddata->regions[2].start );
              apci_debug("regions[2].end   = %08x\n", ddata->regions[2].end );
              apci_debug("regions[2].length= %08x\n", ddata->regions[2].length );
              apci_debug("regions[2].flags = %lx\n", ddata->regions[2].flags );
              apci_debug("irq = %d\n", ddata->irq );
              break;

         case PCI_IDO_48: /* group2 */
              ddata->regions[2].start   = pci_resource_start(pdev, 2);
              ddata->regions[2].end     = pci_resource_end(pdev, 2);
              ddata->regions[2].flags   = pci_resource_flags(pdev, 2);
              ddata->regions[2].length  = ddata->regions[2].end - ddata->regions[2].start + 1;
              break;

         case LPCI_A16_16A:
         case PCI_DA12_16:
         case PCI_DA12_8:
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
         case mPCIe_AI12_16E_proto:/* group3 */
              ddata->regions[2].start   = pci_resource_start(pdev, 2);
              ddata->regions[2].end     = pci_resource_end(pdev, 2);
              ddata->regions[2].flags   = pci_resource_flags(pdev, 2);
              ddata->regions[2].length  = ddata->regions[2].end - ddata->regions[2].start + 1;

              ddata->regions[3].start   = pci_resource_start(pdev, 3);
              ddata->regions[3].end     = pci_resource_end(pdev, 3);
              ddata->regions[3].flags   = pci_resource_flags(pdev, 3);
              ddata->regions[3].length  = ddata->regions[3].end - ddata->regions[3].start + 1;

              ddata->irq = pdev->irq;
              ddata->irq_capable = 1;
              break;

         case PCI_DA12_6: /* group4 */
         case PCI_DA12_4:
         case PCI_DA12_2:
              ddata->regions[2].start   = pci_resource_start(pdev, 2);
              ddata->regions[2].end     = pci_resource_end(pdev, 2);
              ddata->regions[2].flags   = pci_resource_flags(pdev, 2);
              ddata->regions[2].length  = ddata->regions[2].end - ddata->regions[2].start + 1;

              ddata->regions[3].start   = pci_resource_start(pdev, 3);
              ddata->regions[3].end     = pci_resource_end(pdev, 3);
              ddata->regions[3].flags   = pci_resource_flags(pdev, 3);
              ddata->regions[3].length  = ddata->regions[3].end - ddata->regions[3].start + 1;
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
        apci_devel("setting up DMA in alloc\n");
        ddata->regions[0].start   = pci_resource_start(pdev, 0);
        ddata->regions[0].end     = pci_resource_end(pdev, 0);
        ddata->regions[0].flags   = pci_resource_flags(pdev, 0);
        ddata->regions[0].length  = ddata->regions[0].end - ddata->regions[0].start + 1;

        iounmap(ddata->plx_region.mapped_address);
    }



    /* request regions */
    for (count = 0; count < 6; count++) {
      if (ddata->regions[count].start == 0) {
        continue; /* invalid region */
      }
      if (ddata->regions[count].flags & IORESOURCE_IO) {

        apci_debug("requesting io region start=%08x,len=%d\n", ddata->regions[count].start, ddata->regions[count].length );
        presource = request_region(ddata->regions[count].start, ddata->regions[count].length, "apci");
      } else {
        apci_debug("requesting mem region start=%08x,len=%d\n", ddata->regions[count].start, ddata->regions[count].length );
        presource = request_mem_region(ddata->regions[count].start, ddata->regions[count].length, "apci");
        if (presource != NULL) {
          ddata->regions[count].mapped_address = ioremap(ddata->regions[count].start, ddata->regions[count].length);
        }
      }

      if (presource == NULL) {
        /* If we failed to allocate the region. */
        count--;

        while (count >= 0) {
          if (ddata->regions[count].start != 0) {
            if (ddata->regions[count].flags & IORESOURCE_IO) {
              /* if it is a valid region */
              release_region(ddata->regions[count].start, ddata->regions[count].length);
            } else {
              iounmap(ddata->regions[count].mapped_address);

              release_region(ddata->regions[count].start, ddata->regions[count].length);
            }
          }
        }
        goto out_alloc_driver;
      }
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
        spin_lock_init(&(ddata->dma_data_lock));
        ddata->plx_region = ddata->regions[0];
    }

    return ddata;

out_alloc_driver:
    kfree( ddata );
    return NULL;
}

/**
 * @desc Removes all of the drivers from this module
 *       before cleanup
 */
void
apci_free_driver( struct pci_dev *pdev )
{
     struct apci_my_info *ddata = pci_get_drvdata( pdev );
     int count;

     apci_devel("Entering free driver.\n");

     if (ddata->plx_region.flags & IORESOURCE_IO)
     {
        apci_debug("releasing memory of %08x , length=%d\n", ddata->plx_region.start, ddata->plx_region.length );
        release_region( ddata->plx_region.start, ddata->plx_region.length );
     }
     else
     {
        //iounmap(ddata->plx_region.mapped_address);
        //release_mem_region(ddata->plx_region.start, ddata->plx_region.length);
     }

     for (count = 0; count < 6; count ++) {
          if (ddata->regions[count].start == 0) {
               continue; /* invalid region */
          }
          if (ddata->regions[count].flags & IORESOURCE_IO) {
               release_region(ddata->regions[count].start, ddata->regions[count].length);
          } else {
               iounmap(ddata->regions[count].mapped_address);
               release_mem_region(ddata->regions[count].start, ddata->regions[count].length);
          }
     }

     kfree(ddata );
     apci_debug("Completed freeing driver.\n");
}

static void apci_class_dev_unregister(struct apci_my_info *ddata )
{
     struct apci_lookup_table_entry *obj = &apci_driver_table[ APCI_LOOKUP_ENTRY( (int)ddata->id->device ) ];
     apci_devel("entering apci_class_dev_unregister\n");
/* ddata->dev = device_create(class_apci, &ddata->pci_dev->dev , apci_first_dev + id, NULL, "apci/%s_%d", obj->name, obj->counter ++ ); */


     apci_devel("entering apci_class_dev_unregister.\n");
     if (ddata->dev == NULL)
          return;

     device_unregister( ddata->dev );
     obj->counter --;
     dev_counter --;

     apci_devel("leaving apci_class_dev_unregister.\n");
}

static int __devinit
apci_class_dev_register( struct apci_my_info *ddata )
{
    int ret;
    struct apci_lookup_table_entry *obj = &apci_driver_table[ APCI_LOOKUP_ENTRY( (int)ddata->id->device ) ];
    apci_devel("entering apci_class_dev_register\n");

    ddata->dev = device_create(class_apci, &ddata->pci_dev->dev , apci_first_dev + dev_counter, NULL, "apci/%s_%d", obj->name, obj->counter ++ );

    /* add pointer to the list of all ACCES I/O products */

    if( IS_ERR( ddata->dev )) {
      apci_error("Error creating device");
      ret = PTR_ERR( ddata->dev );
      ddata->dev = NULL;
      return ret;
    }
    dev_counter ++;
    apci_devel("leaving apci_class_dev_register\n");
    return 0;
}

irqreturn_t apci_interrupt(int irq, void *dev_id)
{
    struct apci_my_info  *ddata;
    __u8  byte;
    __u32 dword;
    bool notify_user = true;


    ddata = (struct apci_my_info *) dev_id;

    /* The first thing we do is check to see if the card is causing an IRQ.
     * If it is then we can proceed to clear the IRQ. Otherwise let
     * Linux know that it wasn't us.
     */
    if( !ddata->is_pcie ) {

      byte = inb(ddata->plx_region.start + 0x4C);

      if ((byte & 4) == 0) {
        return IRQ_NONE; /* not me */
      }
    }
    else
    {                    /* PCIe */
      if (ddata->plx_region.flags & IORESOURCE_IO)
      {
        byte = inb(ddata->plx_region.start + 0x69);
      }
      else
      {
        byte = ioread8(ddata->plx_region.mapped_address + 0x69);
      }

      if ((byte & 0x80 ) == 0) {
        return IRQ_NONE; /* not me */
      }
    }

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
          outb(0, ddata->regions[2].start + 0x1e);
          outb(0, ddata->regions[2].start + 0x1f);
          break;

        case PCI_DA12_16:
        case PCI_DA12_8:
        case PCI_DA12_6:
        case PCI_DA12_4:
        case PCI_DA12_2:
        case PCI_DA12_16V:
        case PCI_DA12_8V:
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
        case PCI_AI12_16A:
        case PCI_AIO12_16:
        case PCI_A12_16A:
          /* Clear the FIFO interrupt enable bits, but leave
           * the counter enabled.  Otherwise the IRQ will not
           * go away and user code will never run as the machine
           * will hang in a never-ending IRQ loop. The userland
           * irq routine must re-enable the interrupts if desired.
           */
          outb( 0x01, ddata->regions[2].start + 0x4);
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
        case MPCIE_RO_8:
        case MPCIE_II_16:
        case MPCIE_II_8:
        case MPCIE_II_4:
        case MPCIE_QUAD_4:
        case MPCIE_QUAD_8:
          outb(0xff, ddata->regions[2].start + 41);
          break;

        case PCIe_IDIO_12:
        case PCIe_IDIO_24:
          /* read 32 bits from +8 to determine which specific bits have generated a CoS IRQ then write the same value back to +8 to clear those CoS latches */
          dword = inl(ddata->regions[2].start + 0x8);
          outl(dword, ddata->regions[2].start + 0x8);
          break;
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
        case mPCIe_AI12_16E_proto: //DMA capable cards
        {
          //If this is a FIFO near full IRQ then tell the card
          //to write to the next buffer (and don't notify the user?)
          //else if it is a write done IRQ set last_valid_buffer and notify user
          uint8_t irq_event = ioread8(ddata->regions[2].mapped_address + 0x2);
          if (irq_event & 0x1) //FIFO almost full
          {
            dma_addr_t base = ddata->dma_addr;
            spin_lock(&(ddata->dma_data_lock));
            if (ddata->dma_last_buffer == -1 )
            {
              notify_user = false;
            }
            else if (ddata->dma_first_valid == -1)
            {
              ddata->dma_first_valid = 0;
            }

            ddata->dma_last_buffer++;
            ddata->dma_last_buffer %= ddata->dma_num_slots;

            if (ddata->dma_last_buffer == ddata->dma_first_valid)
            {
              apci_error("ISR: data discarded");
              ddata->dma_last_buffer--;
              if ( ddata->dma_last_buffer < 0 ) ddata->dma_last_buffer = ddata->dma_num_slots-1;
              ddata->dma_data_discarded++;
            }
            spin_unlock(&(ddata->dma_data_lock));
            base += ddata->dma_slot_size * ddata->dma_last_buffer;

            iowrite32(base & 0xffffffff, ddata->regions[0].mapped_address);
            iowrite32(base >> 32, ddata->regions[0].mapped_address + 4);
            iowrite32(ddata->dma_slot_size, ddata->regions[0].mapped_address + 8);
            iowrite32(4, ddata->regions[0].mapped_address + 12);
          }
          iowrite8(irq_event, ddata->regions[2].mapped_address + 0x2);
          apci_debug("ISR: irq_event = 0x%x, depth = 0x%x\n", irq_event, ioread32(ddata->regions[2].mapped_address + 0x28));
        }
    };

    /* Check to see if we were actually waiting for an IRQ. If we were
     * then we need to wake the queue associated with this device.
     * Right now it is not possible for any other code sections that access
     * the critical data to interrupt us so we won't disable other IRQs.
     */
    if (notify_user)
    {
      spin_lock(&(ddata->irq_lock));

      if (ddata->waiting_for_irq) {
        ddata->waiting_for_irq = 0;
        spin_unlock(&(ddata->irq_lock));
        wake_up_interruptible(&(ddata->wait_queue));
      } else {
        spin_unlock(&(ddata->irq_lock));
      }
    }

    return IRQ_HANDLED;
}



void remove(struct pci_dev *pdev)
{
     struct apci_my_info *ddata = pci_get_drvdata( pdev );
     struct apci_my_info *child;
     struct apci_my_info *_temp;
     apci_devel("entering remove\n");

     spin_lock(&(ddata->irq_lock));

     if( ddata->irq_capable )
       free_irq(pdev->irq, ddata );

     spin_unlock(&(ddata->irq_lock));

     if (ddata->dma_virt_addr != NULL)
     {
        dma_free_coherent(&(ddata->pci_dev->dev),
            ddata->dma_num_slots * ddata->dma_slot_size,
            ddata->dma_virt_addr,
            ddata->dma_addr);
     }

     apci_class_dev_unregister( ddata );

     cdev_del( &ddata->cdev );

     spin_lock( &head.driver_list_lock );
     list_for_each_entry_safe( child , _temp,  &head.driver_list , driver_list ) {
       if( child == ddata ) {
         apci_debug("Removing node with address %p\n", child );
         list_del( &child->driver_list );
       }
     }
     spin_unlock( &head.driver_list_lock );

     apci_free_driver( pdev );

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

    if( pci_enable_device(pdev) ) {
      return -ENODEV;
    }

    ddata = (struct apci_my_info*) apci_alloc_driver( pdev, id  );
    if( ddata == NULL ) {
      return -ENOMEM;
    }
    /* Setup actual device driver items */
    ddata->nchannels = APCI_NCHANNELS;
    ddata->pci_dev   = pdev;
    ddata->id        = id;
    ddata->is_pcie   = ( ddata->plx_region.length >= 0x100 ? 1 : 0 );
    apci_debug("Is device PCIE : %d\n", ddata->is_pcie );

    /* Spin lock init stuff */


    /* Request Irq */
    if ( ddata->irq_capable ) {
         apci_debug("Requesting Interrupt, %u\n", (unsigned int)ddata->irq );
         ret = request_irq((unsigned int) ddata->irq,
			   apci_interrupt,
			   IRQF_SHARED ,
			   "apci",
			   ddata);
	 if ( ret ) {
	   apci_error("error requesting IRQ %u\n", ddata->irq);
	   ret = -ENOMEM;
	   goto exit_free;
	 }
   //TODO: Fix this when HW is available to test MEM version
   if (ddata->is_pcie && ddata->plx_region.flags & IORESOURCE_IO)
   {
      outb(0x9, ddata->plx_region.start + 0x69);
   }
   else
   {
     apci_debug("Enabling IRQ MEM/IO plx region\n");
     iowrite8(0x9, ddata->plx_region.mapped_address + 0x69);
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
	 init_waitqueue_head( &(ddata->wait_queue) );
         /* } */
    }


    /* add to sysfs */

    cdev_init( &ddata->cdev, &apci_fops );
    ddata->cdev.owner = THIS_MODULE;

    ret = cdev_add( &ddata->cdev, apci_first_dev + dev_counter, 1 );
    if ( ret ) {
      apci_error("error registering Driver %d", dev_counter );
      goto exit_irq;
    }

    /* add to the list of devices supported */
    spin_lock( &head.driver_list_lock );
    list_add( &ddata->driver_list , &head.driver_list );
    spin_unlock( &head.driver_list_lock );

    pci_set_drvdata(pdev, ddata );
    ret = apci_class_dev_register( ddata );

    if ( ret )
        goto exit_pci_setdrv;

    apci_debug("Added driver %d\n", dev_counter - 1);
    apci_debug("Value of irq is %d\n", pdev->irq );
    return 0;

exit_pci_setdrv:
    /* Routine to remove the driver from the list */
    spin_lock( &head.driver_list_lock );
    list_for_each_entry_safe( child , _temp,  &head.driver_list , driver_list ) {
      if( child == ddata ) {
        apci_debug("Would delete address %p\n", child );
      }
    }
    spin_unlock( &head.driver_list_lock );

    pci_set_drvdata(pdev,NULL);
    cdev_del( &ddata->cdev );
exit_irq:
    if( ddata->irq_capable )
      free_irq(pdev->irq, ddata );
exit_free:
    apci_free_driver( pdev );
    return ret;
}


int __init
apci_init(void)
{
	void *ptr_err;
        int result;
        int ret;
        dev_t dev = MKDEV(0,0);
        apci_debug("performing init duties\n");
        spin_lock_init( &head.driver_list_lock);
        INIT_LIST_HEAD( &head.driver_list );
        sort( apci_driver_table, APCI_TABLE_SIZE , APCI_TABLE_ENTRY_SIZE ,te_sort, NULL );


        ret = alloc_chrdev_region( &apci_first_dev, 0, MAX_APCI_CARDS , APCI );
        if( ret ) {
          apci_error("Unable to allocate device numbers");
          return ret;
        }

        /* Create the sysfs entry for this */
	class_apci = class_create(THIS_MODULE, APCI_CLASS_NAME  );
	if (IS_ERR(ptr_err = class_apci))
		goto err;

        cdev_init( &apci_cdev, &apci_fops );
        apci_cdev.owner = THIS_MODULE;
        apci_cdev.ops   = &apci_fops;
        result = cdev_add(&apci_cdev, dev, 1 );

#ifdef TEST_CDEV_ADD_FAIL
        result = -1;
        if( result == -1 ) {
                apci_error("Going to delete device.\n");
                cdev_del(&apci_cdev);
                apci_error("Deleted device.\n");
        }
#endif

        if( result < 0 ) {
                apci_error("cdev_add failed in apci_init");
                goto err;
        }

        /* needed to get the probe and remove to be called */
        result = pci_register_driver(&pci_driver);

	return 0;
err:

        apci_error("Unregistering chrdev_region.\n");

        unregister_chrdev_region(MKDEV(major_num,0),1 );

	class_destroy(class_apci);
	return PTR_ERR(ptr_err);
}

static void __exit apci_exit(void)
{
    apci_debug("performing exit duties\n");
    pci_unregister_driver(&pci_driver);
    cdev_del(&apci_cdev);
    unregister_chrdev_region(MKDEV(major_num,0),1 );
    class_destroy(class_apci );
}

module_init( apci_init  );
module_exit( apci_exit  );

MODULE_AUTHOR("Jimi Damon <jdamon@accesio.com>");
MODULE_LICENSE("GPL");
