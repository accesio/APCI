#ifndef APCI_DEV_H
#define APCI_DEV_H


#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <asm/uaccess.h>
#include <linux/version.h>

#include "apci_common.h"





/* The device IDs for all the PCI cards this driver will support. */
#define PCIe_DIO_24	0x0C52
#define PCIe_DIO_24D	0x0C53
#define PCIe_DIO_24S	0x0E53
#define PCIe_DIO_24DS	0x0E54
#define PCIe_DIO_24DC	0x0E55
#define PCIe_DIO_24DCS	0x0E56
#define PCIe_DIO_48	0x0C61
#define PCIe_DIO_48S	0x0E61
#define PCIe_DIO_72     0x0C6A 
#define PCIe_DIO_96     0x0C71
#define PCIe_DIO_120    0x0C79
#define PCIe_IIRO_8	0x0F02
#define PCIe_IIRO_16	0x0F09
#define PCI_DIO_24H	0x0C50
#define PCI_DIO_24D	0x0C51
#define PCI_DIO_24H_C	0x0E51
#define PCI_DIO_24D_C	0x0E52
#define PCI_DIO_24S	0x0E50
#define PCI_DIO_48	0x0C60
#define PCI_DIO_48S	0x0E60
#define P104_DIO_48S	0x0E62
#define PCI_DIO_48H	0x0C60
#define PCI_DIO_48HS	0x0E60
#define PCI_DIO_72	0x0C68
#define P104_DIO_96	0x0C69
#define PCI_DIO_96	0x0C70
#define PCI_DIO_96CT	0x2C50
#define PCI_DIO_96C3	0x2C58
#define PCI_DIO_120	0x0C78
#define PCI_AI12_16	0xACA8
#define PCI_AI12_16A	0xACA9
#define PCI_AIO12_16	0xECA8
#define PCI_A12_16A	0xECAA
#define LPCI_A16_16A	0xECE8
#define PCI_DA12_16	0x6CB0
#define PCI_DA12_8	0x6CA8
#define PCI_DA12_6	0x6CA0
#define PCI_DA12_4	0x6C98
#define PCI_DA12_2	0x6C90
#define PCI_DA12_16V	0x6CB1
#define PCI_DA12_8V	0x6CA9
#define LPCI_IIRO_8	0x0F01
#define PCI_IIRO_8	0x0F00
#define PCI_IIRO_16	0x0F08
#define PCI_IDI_48	0x0920
#define PCI_IDO_48	0x0520
#define PCI_IDIO_16	0x0DC8
#define PCI_WDG_2S	0x1250 //TODO: Find out if the Watch Dog Cards should really be here
#define PCI_WDG_CSM	0x22C0
#define PCI_WDG_IMPAC	0x12D0
#define MPCIE_DIO_24S   0x0100

/* names for the drivers we create */
#define NAME_PCIe_DIO_24                "pcie_dio_24"
#define NAME_PCIe_DIO_24D               "pcie_dio_24d"
#define NAME_PCIe_DIO_24S               "pcie_dio_24s"
#define NAME_PCIe_DIO_24DS              "pcie_dio_24ds"
#define NAME_PCIe_DIO_24DC              "pcie_dio_24dc"
#define NAME_PCIe_DIO_24DCS             "pcie_dio_24dcs"
#define NAME_PCIe_DIO_48                "pcie_dio_48"
#define NAME_PCIe_DIO_48S               "pcie_dio_48s"
#define NAME_PCIe_DIO_72                "pcie_dio_72"
#define NAME_PCIe_DIO_96                "pcie_dio_96"  
#define NAME_PCIe_DIO_120               "pcie_dio_120"  

#define NAME_PCIe_IIRO_8                "pcie_iiro_8"
#define NAME_PCIe_IIRO_16               "pcie_iiro_16"
#define NAME_PCI_DIO_24H                "pci_dio_24h"
#define NAME_PCI_DIO_24D                "pci_dio_24d"
#define NAME_PCI_DIO_24H_C              "PCI_dio_24h_c"
#define NAME_PCI_DIO_24D_C              "PCI_dio_24d_c"
#define NAME_PCI_DIO_24S                "pci_dio_24s"
#define NAME_PCI_DIO_48                 "pci_dio_48"
#define NAME_PCI_DIO_48S                "pci_dio_48s"
#define NAME_P104_DIO_48S               "p104_dio_48s"
#define NAME_PCI_DIO_48H                "pci_dio_48h"
#define NAME_PCI_DIO_48HS               "pci_dio_48hs"
#define NAME_PCI_DIO_72                 "pci_dio_72"
#define NAME_P104_DIO_96                "p104_dio_96"
#define NAME_PCI_DIO_96                 "pci_dio_96"
#define NAME_PCI_DIO_96CT               "pci_dio_96ct"
#define NAME_PCI_DIO_96C3               "pci_dio_96c3"
#define NAME_PCI_DIO_120                "pci_dio_120"
#define NAME_PCI_AI12_16                "pci_ai12_16"
#define NAME_PCI_AI12_16A               "pci_ai12_16a"
#define NAME_PCI_AIO12_16               "pci_aio12_16"
#define NAME_PCI_A12_16A                "pci_a12_16a"
#define NAME_LPCI_A16_16A               "lpci_a16_16a"
#define NAME_PCI_DA12_16                "pci_da12_16"
#define NAME_PCI_DA12_8                 "pci_da12_8"
#define NAME_PCI_DA12_6                 "pci_da12_6"
#define NAME_PCI_DA12_4                 "pci_da12_4"
#define NAME_PCI_DA12_2                 "pci_da12_2"
#define NAME_PCI_DA12_16V               "pci_da12_16v"
#define NAME_PCI_DA12_8V                "pci_da12_8v"
#define NAME_LPCI_IIRO_8                "lpci_iiro_8"
#define NAME_PCI_IIRO_8                 "pci_iiro_8"
#define NAME_PCI_IIRO_16                "pci_iiro_16"
#define NAME_PCI_IDI_48                 "pci_idi_48"
#define NAME_PCI_IDO_48                 "pci_ido_48"
#define NAME_PCI_IDIO_16                "pci_idio_16"
#define NAME_PCI_WDG_2S                 "pci_wdg_2s"
#define NAME_PCI_WDG_CSM                "pci_wdg_csm"
#define NAME_PCI_WDG_IMPAC              "pci_wdg_impac"
#define NAME_MPCIE_DIO_24S               "mpcie_dio_24s"

enum ADDRESS_TYPE {INVALID = 0, IO, MEM};
typedef enum ADDRESS_TYPE address_type;



typedef struct {
    __u32 start;
    __u32 end;
    __u32 length;
    unsigned long flags;
    void *mapped_address;
} io_region;

struct apci_board {
    struct device *dev;
};


struct apci_my_info {
     __u32 dev_id;
     io_region regions[6], plx_region;
     const struct pci_device_id *id;
     int is_pcie;
     int irq;
     int irq_capable; /* is the card even able to generate irqs? */
     int waiting_for_irq; /* boolean for if the user has requested an IRQ */
     int irq_cancelled; /* boolean for if the user has cancelled the wait */

     /* List of drivers */
     struct list_head driver_list;
     spinlock_t driver_list_lock;

     wait_queue_head_t wait_queue;
     spinlock_t irq_lock;

  
     struct cdev cdev;

     struct pci_dev *pci_dev;

     int nchannels;
     
     struct apci_board boards[APCI_NCHANNELS];

     struct device *dev;
};

int probe(struct pci_dev *dev, const struct pci_device_id *id);
void remove(struct pci_dev *dev);
void delete_driver(struct pci_dev *dev);



#endif
