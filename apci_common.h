
#ifndef APCI_COMMON_H
#define APCI_COMMON_H

#define A_PCI_DEBUG 0 // feel free to change this value

/* ACCES does not recommend changing anything else in this file. */

/* The vendor ID for all the PCI cards this driver will support. */
#define A_VENDOR_ID 0x494F
#define ACCES_MAJOR  98
#define ACCES_MAGIC_NUM 0xE0

#define APCI_DEFAULT_DEVFILE_MODE 0000 // /dev/{cardname} will get these permissions on creation

#define APCI_PREFIX "apci: "
#define APCI "apci"

#define APCI_NCHANNELS 8
#define MAX_APCI_CARDS 8
#define MAX_APCI_DEVICES (MAX_APCI_CARDS * APCI_NCHANNELS)


/**
 * Debugging , simplified
 */
extern int apci_debug_level;

#ifdef A_PCI_DEBUG
#define apci_debug(fmt,...) if( A_PCI_DEBUG > 0 ) { pr_err(APCI_PREFIX fmt, ##__VA_ARGS__ ); }
#define apci_devel(fmt,...) if( A_PCI_DEBUG > 1 ) { pr_err(APCI_PREFIX fmt, ##__VA_ARGS__ ); }
#define apci_info(fmt, ...) if( A_PCI_DEBUG > 2 ) { pr_info( APCI_PREFIX fmt, ##__VA_ARGS__ ); }
#else
#define apci_debug(fmt,...) if( 0 ) { pr_debug(APCI_PREFIX fmt, ##__VA_ARGS__ ); }
#define apci_devel(fmt,...) if( 0 ) { pr_debug(APCI_PREFIX fmt, ##__VA_ARGS__ ); }
#define apci_info(fmt, ...) if( 0 ) { pr_info( APCI_PREFIX fmt, ##__VA_ARGS__ ); }
#endif
#define apci_error(fmt,...) pr_err( APCI_PREFIX fmt, ##__VA_ARGS__ )


#endif
