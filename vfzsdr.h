//
//  vfzsdr.h
//  hello
//
//  Created by Albin Stigö on 07/10/2017.
//  Copyright © 2017 Albin Stigo. All rights reserved.
//

#ifndef vfzsdr_h
#define vfzsdr_h

#include <linux/init.h>           // Macros used to mark up functions e.g. __init __exit
#include <linux/module.h>         // Core header for loading LKMs into the kernel
#include <linux/device.h>         // Header to support the kernel Driver Model
#include <linux/kernel.h>         // Contains types, macros, functions for the kernel
#include <linux/fs.h>             // Header for the Linux file system support
#include <linux/uaccess.h>         // Required for the copy to user function
#include <linux/i2c.h>
#include <asm/byteorder.h>
#include <linux/semaphore.h>

#define  DEVICE_NAME "vfzsdr"
#define  CLASS_NAME  "sdr"

#define CRIT_BEG(sem, error) if(down_interruptible(sem)) return -error
#define CRIT_END(sem) up(sem)

enum vfzsdr_reg {
    REG_MODE = 1,
    REG_GAIN = 2,
    REG_FREQ = 3,
};

const char *vfzsdr_mode_str[] = {"AM", "LSB", "USB"};
enum vfzsdr_mode {
    MODE_AM  = 0,
    MODE_SSB = 1,
};

enum vfzsdr_sideband {
    SIDEBAND_LOWER = 0,
    SIDEBAND_UPPER = 1,
};

const char *vfzsdr_filter_str[] = {"narrow", "wide"};
enum vfzsdr_filter {
    FILTER_NARROW = 0,
    FILTER_WIDE   = 1,
};

const char *vfzsdr_att_str[] = {"0dB", "6dB", "12dB", "18dB"};
enum vfzsdr_att {
    ATT_0DB  = 0,
    ATT_6DB  = 1,
    ATT_12DB = 2,
    ATT_18DB = 3,
};

const char *vfzsdr_iffreq_str[] = {"" /* placeholder */, "45MHz", "21MHz"};
enum vfzsdr_iffreq {
    IF_45MHZ = 1,
    IF_21MHZ = 2,
};

const char *vfzsdr_fconf_str[] = {"direct", "indirect"};
enum vfzsdr_fconf {
    FCONF_DIRECT   = 0,
    FCONF_INDIRECT = 1,
};

struct vfzsdr_radio {
    // Reg 1
    enum vfzsdr_mode     mode;
    enum vfzsdr_filter   filter;
    enum vfzsdr_sideband sideband;
    bool                 forced_tx;
    bool                 forced_key;
    enum vfzsdr_att      tx_att;
    enum vfzsdr_att      rx_att;
    enum vfzsdr_iffreq   if_freq;
    bool                 cw_tx_nomod;
    enum vfzsdr_fconf    fconf;
    u8                   gain;
    s8                   clarifier;
    u32                  frequency;
};


#endif /* vfzsdr_h */
