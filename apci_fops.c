#include "apci_fops.h"
#include "apci_dev.h"


address_type is_valid_addr(struct apci_my_info *driver_data, int bar, int addr)
{
    /* if it is a valid bar */
    if (driver_data->regions[bar].start != 0) {
      /* if it is within the valid range */
      if (driver_data->regions[bar].length >= addr) {
        /* if it is an I/O region */
        if (driver_data->regions[bar].flags & IORESOURCE_IO) {
          apci_debug("Valid I/O range.\n");
          return IO;
        } else {
          apci_debug("apci: Valid Mem range.\n");
          return MEM;
        }
      }
    }
    apci_error("apci: Invalid addr.\n");

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
    unsigned long device_index, flags;

    if( !ddata ) {
         apci_error("no ddata.\n");
         return 0;

    }
    apci_devel("apci_wait_for_irq_ioctl value is %u\n", (int)apci_wait_for_irq_ioctl );
    apci_devel("apci_get_base_address value is %x\n", (int)apci_get_base_address );
    apci_devel("inside ioctl.\n");

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
          status = access_ok(VERIFY_WRITE, arg,
                             sizeof(info_struct));

          if (status == 0) return -EACCES;

          status = copy_from_user(&info, (info_struct *) arg, sizeof(info_struct));

          info.dev_id = ddata->dev_id;

          for (count = 0; count < 6; count++) {
               info.base_addresses[count] =
                    ddata->regions[count].start;
          }

          status = copy_to_user((info_struct *) arg, &info, sizeof(info_struct));
          break;

        case apci_write_ioctl:
          status = access_ok (VERIFY_WRITE, arg, sizeof(iopack));

          if (status == 0) {
               apci_error("access_ok failed\n");
               return -EACCES; /* TODO: FIND appropriate return value */
          }

          status = copy_from_user(&io_pack, (iopack *) arg,
                                  sizeof(iopack));
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
              apci_info("performing I/O write to %X\n",
                        ddata->regions[io_pack.bar].start + io_pack.offset);

              switch (io_pack.size) {
                case BYTE:
                  apci_devel("writing byte to %X\n", ddata->regions[io_pack.bar].start + io_pack.offset);
                  outb(io_pack.data, ddata->regions[io_pack.bar].start + io_pack.offset);
                  break;

                case WORD:
                  apci_devel("writing word to %X\n", ddata->regions[io_pack.bar].start + io_pack.offset);
                  outw(io_pack.data, ddata->regions[io_pack.bar].start + io_pack.offset);
                  break;

                case DWORD:
                  apci_devel("writing dword to %X\n", ddata->regions[io_pack.bar].start + io_pack.offset);
                  outl(io_pack.data, ddata->regions[io_pack.bar].start + io_pack.offset);
                  break;
              };
              break;

            case MEM:
              switch(io_pack.size) {
                case BYTE:
                  iowrite8(io_pack.data, ddata->regions[io_pack.bar].mapped_address + io_pack.offset);
                  break;
                
                case WORD:
                  iowrite16(io_pack.data, ddata->regions[io_pack.bar].mapped_address + io_pack.offset);
                  break;
                
                case DWORD:
                  iowrite32(io_pack.data, ddata->regions[io_pack.bar].mapped_address + io_pack.offset);
                  break;
              };
              break;

            case INVALID:
              return -EFAULT;
              break;
          };
          break;

        case apci_read_ioctl:
             status = access_ok(VERIFY_WRITE, arg, sizeof(iopack));

             if (status == 0) return -EACCES; /* TODO: Find a better return code */

             status = copy_from_user(&io_pack, (iopack *) arg, sizeof(iopack));
             count = 0;

             if (ddata == NULL) return -ENXIO; /* invalid device index */

             switch (is_valid_addr(ddata, io_pack.bar, io_pack.offset)) {
             case IO:
                  switch (io_pack.size) {
                  case BYTE:
                       io_pack.data = inb(ddata->regions[io_pack.bar].start + io_pack.offset);
                       break;

                  case WORD:
                       io_pack.data = inw(ddata->regions[io_pack.bar].start + io_pack.offset);
                       break;

                  case DWORD:
                       io_pack.data = inl(ddata->regions[io_pack.bar].start + io_pack.offset);
                       break;
                  };
                  break;

             case MEM:
                  switch (io_pack.size) {
                  case BYTE:
                       io_pack.data = ioread8(ddata->regions[io_pack.bar].mapped_address + io_pack.offset);
                       break;

                  case WORD:
                       io_pack.data = ioread16(ddata->regions[io_pack.bar].mapped_address + io_pack.offset);
                       break;

                  case DWORD:
                       io_pack.data = ioread32(ddata->regions[io_pack.bar].mapped_address + io_pack.offset);
                       break;
                  };
                  break;

             case INVALID:
                  return -EFAULT;
                  break;
             };
      
             apci_info("performed read from %X\n",
                       ddata->regions[io_pack.bar].start + io_pack.offset);

             status = copy_to_user((iopack *)arg, &io_pack,
                                   sizeof(iopack));
             break;

    case apci_wait_for_irq_ioctl:
         apci_info("enter wait_for_IRQ.\n");

         device_index = arg;

         apci_info("Acquiring spin lock\n");

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

         apci_info("Released spin lock\n");

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

         status = copy_to_user((info_struct *) arg, &ddata->plx_region.start, sizeof( io_region ));      
         break;
    };

    return 0;
}
