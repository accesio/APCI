#ifndef APCI_IOCTL_H

#define APCI_IOCTL_H
#define ACCES_MAGIC_NUM 0xE0
enum SIZE { BYTE = 0, WORD, DWORD};
#define DAC_BUFF_LEN 65536 * 4

typedef struct {
        unsigned long device_index;
        int bar;
        unsigned int offset;
        enum SIZE size;
        __u32 data;
} iopack;

typedef struct {
        unsigned long device_index;
        int bar;
        unsigned int bar_offset;
        unsigned int mmap_offset; //offset in bytes into mmaped dac fifo buffer
        enum SIZE size;
        __u32 length;
} buff_iopack;


typedef struct
{
        unsigned long device_index;
        int dev_id;
        /* Most likely only base_addresses[2] will be useful.
         * Consult your product manual for more information.
         */
        unsigned long base_addresses[6];
} info_struct;

#define APCI_EVENT_DATA_READY 0x1
#define APCI_EVENT_DATA_DISCARDED 0x2

typedef struct {
        int num_slots;
        size_t slot_size;
} dma_buffer_settings_t;
typedef struct {
        int start_index; //first index with valid data
        int slots; //number of slots  that have valid data. May wrap
        int data_discarded; //number of slots worth of data discarded due to
                            //full buffer since last call to data_ready
} data_ready_t;




#define apci_get_devices_ioctl      _IO(ACCES_MAGIC_NUM , 1)
#define apci_get_device_info_ioctl  _IOR(ACCES_MAGIC_NUM, 2, info_struct *)
#define apci_write_ioctl            _IOW(ACCES_MAGIC_NUM, 3, iopack *)
#define apci_read_ioctl             _IOR(ACCES_MAGIC_NUM, 4, iopack *)
#define apci_wait_for_irq_ioctl     _IOR(ACCES_MAGIC_NUM, 5, unsigned long)
#define apci_cancel_wait_ioctl      _IOW(ACCES_MAGIC_NUM, 6, unsigned long)
#define apci_get_base_address       _IOW(ACCES_MAGIC_NUM, 7, unsigned long)
//#define apci_force_dma              _IO(ACCES_MAGIC_NUM, 8)
#define apci_set_dma_transfer_size  _IOW(ACCES_MAGIC_NUM, 9, dma_buffer_settings_t *)
#define apci_data_ready             _IOR(ACCES_MAGIC_NUM, 10, data_ready_t *)
#define apci_data_done              _IOW(ACCES_MAGIC_NUM, 11, unsigned long)
#define apci_write_buff_ioctl       _IOW(ACCES_MAGIC_NUM, 12, buff_iopack *)
#define apci_set_dac_buff_size     _IOW(ACCES_MAGIC_NUM, 13, unsigned long)



#endif
