#include "apci_fops.h"
#include "apci_dev.h"


address_type is_valid_addr(struct apci_my_info *driver_data, int bar, int addr)
{
    /* if it is a valid bar */
    if (driver_data->regions[bar].start != 0) {
      /* if it is within the valid range */
      if (driver_data->regions[bar].length >= addr) 
      {
        /* if it is an I/O region */
        if (driver_data->regions[bar].flags & IORESOURCE_IO) {
          apci_info("Valid I/O range.\n");
          return IO;
        } else {
          apci_info("Valid Mem range.\n");
          return MEM;
        }
      } else
      {
           apci_error("register address to large for region[%d]\n", bar);
      }     
    }
    apci_error("Invalid addr: bar[%d]+0x%04x.\n", bar, addr);

    return INVALID;
}

int open_apci( pInode inode, pFile filp )
{
  struct apci_my_info *ddata;
  apci_debug("Opening device\n");
  ddata = container_of( inode->i_cdev, struct apci_my_info, cdev );
  /* need to check to see if the device is
     Blocking  / nonblocking */

  filp->private_data = ddata;
  ddata->waiting_for_irq = 0;
  return 0;
}


ssize_t read_apci(struct file *filp, char __user *buf,
                         size_t len, loff_t *off)
{
    struct apci_my_info  *ddata = filp->private_data ;
    int status;
    unsigned int value = inb( ddata->regions[2].start + 0x1 );
    status = copy_to_user(buf, &value, 1);
    return ( status ? -EFAULT : 1 );
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39 )
int ioctl_apci(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
#else
long  ioctl_apci(struct file *filp, unsigned int cmd, unsigned long arg)
#endif
{
    int count;
    int status;
    struct apci_my_info *ddata = filp->private_data;
    info_struct info;
    iopack io_pack;
    string_iopack string_pack;

    unsigned long device_index, flags;

    if( !ddata ) {
         apci_error("no ddata.\n");
         return 0;

    }
    apci_info("inside ioctl.\n");

    switch (cmd) {
        struct apci_my_info *child;
        case apci_get_devices_ioctl:
          apci_debug("entering get_devices\n");

          if (filp->private_data == NULL) return 0;

          apci_debug("private_data was not null\n");

          count = 0;

          list_for_each_entry( child, &head.driver_list , driver_list )
            count ++;

          apci_debug("get_devices returning %d\n", count);

          return count;
          break;



        case apci_get_device_info_ioctl:
          apci_debug("entering get_device_info \n");
          #if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0))
                    status = access_ok( arg, sizeof(info_struct));
          #else
                    status = access_ok(VERIFY_WRITE, arg,
                                   sizeof(info_struct));
          #endif

          if (status == 0) return -EACCES;

          status = copy_from_user(&info, (info_struct *) arg, sizeof(info_struct));

          info.dev_id = ddata->dev_id;

          for (count = 0; count < 6; count++) {
               info.base_addresses[count] =
                    ddata->regions[count].start;
          }

          status = copy_to_user((info_struct *) arg, &info, sizeof(info_struct));
          apci_debug("exiting get_device_info_ioctl\n");
          break;




        case apci_write_ioctl:
          #if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0))
                    status = access_ok (arg, sizeof(iopack));
          #else
                    status = access_ok (VERIFY_WRITE, arg, sizeof(iopack));
          #endif

          if (status == 0) {
               apci_error("access_ok failed\n");
               return -EACCES; /* TODO: FIND appropriate return value */
          }

          status = copy_from_user(&io_pack, (iopack *) arg, sizeof(iopack));
          count = 0;
          /* while (count < io_pack.device_index) { */
          /*      if (ddata != NULL) { */
          /*           ddata = ddata->next; */
          /*      } */
          /*      count++; */
          /* } */
          /* if (ddata == NULL) return -ENXIO; /\* invalid device index *\/ */

          switch(is_valid_addr(ddata, io_pack.bar,io_pack.offset)) {
               case IO:
                    apci_info("performing I/O write to %llX\n",
                         ddata->regions[io_pack.bar].start + io_pack.offset);

                    switch (io_pack.size) {
                         case BYTE:
                         apci_devel("writing byte %02X to %llX\n", io_pack.data, ddata->regions[io_pack.bar].start + io_pack.offset);
                         outb(io_pack.data, ddata->regions[io_pack.bar].start + io_pack.offset);
                         break;

                         case WORD:
                         apci_devel("writing word %04X to %llX\n", io_pack.data, ddata->regions[io_pack.bar].start + io_pack.offset);
                         outw(io_pack.data, ddata->regions[io_pack.bar].start + io_pack.offset);
                         break;

                         case DWORD:
                         apci_devel("writing dword %08X to %llX\n", io_pack.data, ddata->regions[io_pack.bar].start + io_pack.offset);
                         outl(io_pack.data, ddata->regions[io_pack.bar].start + io_pack.offset);
                         break;
                    };
               break;

               case MEM:
                    switch(io_pack.size) {
                         case BYTE:
                         apci_devel("writing byte %02X to %llX\n", io_pack.data, ddata->regions[io_pack.bar].start + io_pack.offset);               
                         iowrite8(io_pack.data, ddata->regions[io_pack.bar].mapped_address + io_pack.offset);
                         break;
          
                         case WORD:
                         apci_devel("writing word %04X to %llX\n", io_pack.data, ddata->regions[io_pack.bar].start + io_pack.offset);
                         iowrite16(io_pack.data, ddata->regions[io_pack.bar].mapped_address + io_pack.offset);
                         break;
          
                         case DWORD:
                         iowrite32(io_pack.data, ddata->regions[io_pack.bar].mapped_address + io_pack.offset);
                         apci_devel("writing dword %08X to %llX\n", io_pack.data, ddata->regions[io_pack.bar].start + io_pack.offset);
                         break;
                    };
               break;

               case INVALID:
                    return -EFAULT;
               break;
          };
          break;





          case apci_read_ioctl:
               #if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0))
                         status = access_ok(arg, sizeof(iopack));
               #else
                         status = access_ok(VERIFY_WRITE, arg, sizeof(iopack));
               #endif

               if (status == 0) return -EACCES; /* TODO: Find a better return code */
  
               status = copy_from_user(&io_pack, (iopack *) arg, sizeof(iopack));
               count = 0;
  
               if (ddata == NULL) 
                    return -ENXIO; /* invalid device index */
  
               switch (is_valid_addr(ddata, io_pack.bar, io_pack.offset)) {
                    case IO:
                         switch (io_pack.size) {
                         case BYTE:
                              io_pack.data = inb(ddata->regions[io_pack.bar].start + io_pack.offset);
                              apci_devel("performed byte read from %llX, got %02X\n", ddata->regions[io_pack.bar].start + io_pack.offset, io_pack.data);
                              break;

                         case WORD:
                              io_pack.data = inw(ddata->regions[io_pack.bar].start + io_pack.offset);
                              apci_devel("performed word read from %llX, got %04X\n", ddata->regions[io_pack.bar].start + io_pack.offset, io_pack.data);
                              break;

                         case DWORD:
                              io_pack.data = inl(ddata->regions[io_pack.bar].start + io_pack.offset);
                              apci_devel("performed dword read from %llX, got %08X\n", ddata->regions[io_pack.bar].start + io_pack.offset, io_pack.data);
                              break;
                         };
                         break;

                    case MEM:
                         switch (io_pack.size) {
                         case BYTE:
                              io_pack.data = ioread8(ddata->regions[io_pack.bar].mapped_address + io_pack.offset);
                              apci_devel("performed read from %llX, got %02X\n", ddata->regions[io_pack.bar].start + io_pack.offset, io_pack.data);
                              break;
     
                         case WORD:
                              io_pack.data = ioread16(ddata->regions[io_pack.bar].mapped_address + io_pack.offset);
                              apci_devel("performed read from %llX, got %04X\n", ddata->regions[io_pack.bar].start + io_pack.offset, io_pack.data);
                              break;
     
                         case DWORD:
                              io_pack.data = ioread32(ddata->regions[io_pack.bar].mapped_address + io_pack.offset);
                              apci_devel("performed read from %llX, got %08X\n", ddata->regions[io_pack.bar].start + io_pack.offset, io_pack.data);
                              break;
                         };
                    break;
     
                    case INVALID:
                         return -EFAULT;
                         break;
               };
                            

               status = copy_to_user((iopack *)arg, &io_pack, sizeof(iopack));
               break;




     case apci_read_string_ioctl:
          
          return -EFAULT;
          break;

    case apci_wait_for_irq_ioctl:
         apci_info("enter wait_for_IRQ.\n");

         device_index = arg;

         apci_debug("Acquiring spin lock\n");

         spin_lock_irqsave(&(ddata->irq_lock), flags);

         if (ddata->waiting_for_irq == 1) {
              /* if we are already waiting for an IRQ on
               * this device.
               */
              spin_unlock_irqrestore (&(ddata->irq_lock), flags);
              return -EALREADY; /* TODO: find a better return code */
         } else {
              ddata->waiting_for_irq = 1;
              ddata->irq_cancelled = 0;
         }

         spin_unlock_irqrestore (&(ddata->irq_lock),
                                 flags);

         apci_debug("Released spin lock\n");

         wait_event_interruptible(ddata->wait_queue, ddata->waiting_for_irq == 0);

         if (ddata->irq_cancelled == 1) return -ECANCELED;

         break;

    case apci_cancel_wait_ioctl:
         apci_info("Cancel wait_for_irq.\n");
         device_index = arg;

         spin_lock_irqsave(&(ddata->irq_lock), flags);

         if (ddata->waiting_for_irq == 0) {
              spin_unlock_irqrestore(&(ddata->irq_lock), flags);
              return -EALREADY;
         }

         ddata->irq_cancelled = 1;
         ddata->waiting_for_irq = 0;
         spin_unlock_irqrestore(&(ddata->irq_lock), flags);
         wake_up_interruptible(&(ddata->wait_queue));
         break;

    case apci_get_base_address:
         apci_debug("Getting base address");

          /* status = copy_from_user(&info, (info_struct *) arg, sizeof(info_struct)); */
         info.dev_id = ddata->dev_id;

         break;

     case apci_set_dma_transfer_size:
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0))
          status = access_ok(arg, sizeof(dma_buffer_settings_t));
#else
          status = access_ok(VERIFY_WRITE, arg, sizeof(dma_buffer_settings_t));
#endif
          if (status == 0) return -EACCES;
          {
               dma_buffer_settings_t settings = {0};
               status = copy_from_user(&settings,
                         (dma_buffer_settings_t *) arg,
                         sizeof(dma_buffer_settings_t));

               if (ddata->dma_virt_addr != NULL)
               {
                    dma_free_coherent(&(ddata->pci_dev->dev),
                         ddata->dma_num_slots * ddata->dma_slot_size,
                         ddata->dma_virt_addr,
                         ddata->dma_addr);

                    ddata->dma_num_slots = 0;
                    ddata->dma_virt_addr = NULL;
                    ddata->dma_addr = 0;
                    ddata->dma_slot_size = 0;
               }

               ddata->dma_num_slots = settings.num_slots;
               ddata->dma_slot_size = settings.slot_size;

               ddata->dma_virt_addr = dma_alloc_coherent(&(ddata->pci_dev->dev),
                                        ddata->dma_num_slots * ddata->dma_slot_size,
                                        &(ddata->dma_addr),
                                        GFP_KERNEL);
               if (ddata->dma_virt_addr == NULL) return -ENOMEM;
               ddata->dma_last_buffer = -1;
               ddata->dma_first_valid = -1;
               ddata->dma_data_discarded = 0;
          }

          break;
          
     case apci_data_ready:
         apci_info("Getting data ready\n");

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0))
          status = access_ok(arg, sizeof(dma_buffer_settings_t));
#else
          status = access_ok(VERIFY_WRITE, arg, sizeof(dma_buffer_settings_t));
#endif
          if (status == 0) return -EACCES;
          {
               unsigned long flags;
               int last_valid;
               data_ready_t data_ready = {0};

               spin_lock_irqsave(&(ddata->dma_data_lock), flags);
               if (( ddata->dma_last_buffer < 0 ) || (ddata->dma_first_valid == -1))
               {
                    data_ready.slots = 0;
               }
               else if (ddata->dma_first_valid == ddata->dma_last_buffer)
               {
                    data_ready.slots = 0;
               }
               else
               {
                    data_ready.start_index = ddata->dma_first_valid;
                    last_valid = ddata->dma_last_buffer - 1;

                    if (last_valid == -1) last_valid = ddata->dma_num_slots - 1;

                    apci_debug("last_valid = %d, ddata_dma_last_buffer = %d\n", last_valid, ddata->dma_last_buffer);

                    if (last_valid >= data_ready.start_index)
                    {
                         data_ready.slots = last_valid - data_ready.start_index +1;
                    }
                    else
                    {
                         data_ready.slots = ddata->dma_num_slots - data_ready.start_index + last_valid + 1;
                    }
               }

               apci_debug("data_ready.start_index = %d, ddata->dma_last_buffer = %d, data_ready.slots = %d\n", data_ready.start_index, ddata->dma_last_buffer, data_ready.slots);

               data_ready.data_discarded = ddata->dma_data_discarded;
               ddata->dma_data_discarded = 0;
               spin_unlock_irqrestore(&(ddata->dma_data_lock), flags);

               apci_debug("start_index = %d, first_valid = %d, num_slots = %d, discarded = %d\n", data_ready.start_index, ddata->dma_first_valid, data_ready.slots, data_ready.data_discarded);

               status = copy_to_user((data_ready_t *)arg, &data_ready,
                                   sizeof(data_ready_t));
          }
          break;
          
     case apci_data_done:
          {
               unsigned long flags;
               {
                    spin_lock_irqsave(&(ddata->dma_data_lock), flags);
                    apci_debug("Adding %lu to first_valid", arg);
                    ddata->dma_first_valid += arg;
                    ddata->dma_first_valid %= ddata->dma_num_slots;
                    spin_unlock_irqrestore(&(ddata->dma_data_lock), flags);
               }
          }
          break;

     case apci_write_string_ioctl:
          #if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0))
               status = access_ok (arg, sizeof(string_iopack));
          #else
               status = access_ok (VERIFY_WRITE, arg, sizeof(string_iopack));
          #endif
          if (status == 0) {
               apci_error("access_ok failed\n");
               return -EACCES; /* TODO: FIND appropriate return value */
          }

          status = copy_from_user(&string_pack, (string_iopack *) arg, sizeof(string_iopack));
          switch(is_valid_addr(ddata, string_pack.bar,string_pack.offset)) {
               case IO:
                    apci_info("performing buffered I/O write %d times to %llX\n",
                         string_pack.length,
                         ddata->regions[string_pack.bar].start + string_pack.offset
                    );

                    switch (string_pack.size) {
                         case BYTE:
                              for (count = 0; count < string_pack.length; count++) {
                                   __u8 *pdata = (__u8 *) string_pack.data;
                                   apci_devel("writing byte %02X to %llX\n", pdata[count], ddata->regions[string_pack.bar].start + string_pack.offset);
                                   outb(pdata[count], ddata->regions[string_pack.bar].start + string_pack.offset);
                              }
                         break;

                         case WORD:
                              for (count = 0; count < string_pack.length; count++) {
                                   __u16 *pdata = (__u16 *) string_pack.data;
                                   apci_devel("writing word %04X to %llX\n", pdata[count], ddata->regions[string_pack.bar].start + string_pack.offset);
                                   outw(pdata[count], ddata->regions[string_pack.bar].start + string_pack.offset);
                              }
                         break;

                         case DWORD:
                              for (count = 0; count < string_pack.length; count++) {
                                   __u32 *pdata = (__u32 *) string_pack.data;
                                   apci_devel("writing dword %08X to %llX\n", pdata[count], ddata->regions[string_pack.bar].start + string_pack.offset);
                                   outl(pdata[count], ddata->regions[string_pack.bar].start + string_pack.offset);
                              }
                         break;
                    };
               break;

               case MEM:
                    switch(string_pack.size) {
                         case BYTE:
                              for (count = 0; count < string_pack.length; count++) {
                                   __u8 *pdata = (__u8 *) string_pack.data;
                                   apci_devel("writing byte %02X to %llX\n", pdata[count], ddata->regions[string_pack.bar].start + string_pack.offset);
                                   iowrite8(pdata[count], ddata->regions[string_pack.bar].mapped_address + string_pack.offset);
                              }
                         break;
          
                         case WORD:
                              for(count = 0; count < string_pack.length; count++) {
                                   __u16 *pdata = (__u16 *) string_pack.data;
                                   apci_devel("writing word %04X to %llX\n", pdata[count], ddata->regions[string_pack.bar].start + string_pack.offset);
                                   iowrite16(pdata[count], ddata->regions[string_pack.bar].mapped_address + string_pack.offset);
                              }
                         break;
          
                         case DWORD:
                              for(count = 0; count < string_pack.length; count++) {
                                   __u32 *pdata = (__u32 *) string_pack.data;
                                   apci_devel("writing dword %08X to %llX\n", pdata[count], ddata->regions[string_pack.bar].start + string_pack.offset);
                                   iowrite32(pdata[count], ddata->regions[string_pack.bar].mapped_address + string_pack.offset);
                              }
                         break;
                    };
               break;

               case INVALID:
                    return -EFAULT;
               break;
          };
          return -EFAULT;
          break;



    };

    return 0;
}


int mmap_apci (struct file *filp, struct vm_area_struct *vma)
{
     struct apci_my_info *ddata = filp->private_data;
     int status;

     status = dma_mmap_coherent(&(ddata->pci_dev->dev),
		         vma,
			 ddata->dma_virt_addr,
			 ddata->dma_addr,
			 vma->vm_end - vma->vm_start);

     return 0;
}
