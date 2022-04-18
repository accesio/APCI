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


#define PCIe_ADIO16_16FDS 0xC2EF
#define PCIe_ADIO16_16F 0xC2EC

#define NAME_PCIe_ADIO16_16FDS          "pcie_adio16_16fds"
#define NAME_PCIe_ADIO16_16F            "pcie_adio16_16f"

enum ADDRESS_TYPE {INVALID = 0, IO, MEM};
typedef enum ADDRESS_TYPE address_type;

typedef struct {
    __u64 start;
    __u64 end;
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

     dma_addr_t dma_addr;
     void *dma_virt_addr;
     int dma_last_buffer; //last dma started to fill
     int dma_first_valid; //first buffer containing valid data
     int dma_num_slots;
     size_t dma_slot_size;
     int dma_data_discarded;
     spinlock_t dma_data_lock;

     void *dac_fifo_buffer;
};

int probe(struct pci_dev *dev, const struct pci_device_id *id);
void remove(struct pci_dev *dev);
void delete_driver(struct pci_dev *dev);



#endif
