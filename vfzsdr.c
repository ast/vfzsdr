#define DEBUG 1

#include "vfzsdr.h"


static uint frequency   = 14000000;
static uint gain        = 0x0a;
static uint busno       = 1;
static uint address     = 0x23;

module_param(frequency, uint, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
module_param(gain, uint, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
module_param(busno, uint, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
module_param(address, uint, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

MODULE_PARM_DESC(frequency, " Frequency, default 14000000");
MODULE_PARM_DESC(gain, " Audio gain, default 10");
MODULE_PARM_DESC(busno, " I2C Bus number, default 1");
MODULE_PARM_DESC(address, " LCD I2C Address, default 0x23");


static struct i2c_client  *client;
static struct i2c_adapter *adapter;

static int major_number;
static struct class*  vfzsdr_class  = NULL;
static struct device* vfzsdr_device = NULL;

static struct vfzsdr_radio *radio;

static DEFINE_SEMAPHORE(sem);

/* prototypes */
static void _write_register(enum vfzsdr_reg);
static inline u32 _normalize_frequency(u32);
static inline u32 _unnormalize_frequency(u32);


static ssize_t vfzsdr_status_show(struct device *dev,
                                  struct device_attribute *attr, char *buf)
{
    int count = 0;
    
    CRIT_BEG(&sem, ERESTARTSYS);
    if(buf) {
        count = snprintf(buf, PAGE_SIZE, "%dHz %s %s\n",
                         _unnormalize_frequency(radio->frequency),
                         vfzsdr_mode_str[radio->mode],
                         vfzsdr_filter_str[radio->filter]);
    }
    CRIT_END(&sem);
    return count;
}


static ssize_t vfzsdr_filter_store(struct device* dev,
                                 struct device_attribute* attr,
                                 const char* buf, size_t count)
{
    //int err;
    CRIT_BEG(&sem, ERESTARTSYS);

    if (count > 0) {
        switch (buf[0]) {
            case 'n':
                radio->filter = FILTER_NARROW;
                break;
            case 'w':
                radio->filter = FILTER_WIDE;
                break;
            default:
                CRIT_END(&sem);
                return -EINVAL;
        }
    }
    _write_register(REG_MODE);
    CRIT_END(&sem);
    
    return count;
}

static ssize_t vfzsdr_filter_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
    int count = 0;

    CRIT_BEG(&sem, ERESTARTSYS);
    if(buf) {
        count = snprintf(buf, PAGE_SIZE, "%s\n", vfzsdr_filter_str[radio->filter]);
    }
    CRIT_END(&sem);
    return count;
}

static ssize_t vfzsdr_mode_store(struct device* dev,
                                 struct device_attribute* attr,
                                 const char* buf, size_t count)
{
    //int err;
    CRIT_BEG(&sem, ERESTARTSYS);
    /* TODO: update to sysfs_match_string */
    switch (match_string(vfzsdr_mode_str, 3, strim((char*)buf))) {
        case 0:
            radio->mode     = MODE_AM;
            break;
        case 1:
            radio->mode     = MODE_SSB;
            radio->sideband = SIDEBAND_LOWER;
            break;
        case 2:
            radio->mode     = MODE_SSB;
            radio->sideband = SIDEBAND_UPPER;
            break;
        default:
            CRIT_END(&sem);
            return -EINVAL;
    }
    _write_register(REG_MODE);
    CRIT_END(&sem);

    return count;
}

static ssize_t vfzsdr_mode_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
    int count = 0;
    int i_mode_str;

    CRIT_BEG(&sem, ERESTARTSYS);
    i_mode_str = !radio->mode ? 0 : 1 + radio->sideband;
    if(buf) {
        count = snprintf(buf, PAGE_SIZE, "%s\n", vfzsdr_mode_str[i_mode_str]);
    }
    CRIT_END(&sem);
    return count;
}

static ssize_t vfzsdr_gain_store(struct device* dev,
                                 struct device_attribute* attr,
                                 const char* buf, size_t count)
{
    u8 res;
    int err;

    CRIT_BEG(&sem, ERESTARTSYS);
    err = kstrtou8(buf, 10, &res);
    if (!err && res < 64) {
        radio->gain = res;
        _write_register(REG_GAIN);
        err = count;
    } else if (!err) {
        dev_err(dev, "gain parameter out of range 0-63");
        err = -ERANGE;
    }
    CRIT_END(&sem);
    
    return err;
}

static ssize_t vfzsdr_gain_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
    int count = 0;
    CRIT_BEG(&sem, ERESTARTSYS);
    if(buf) {
        count = snprintf(buf, PAGE_SIZE, "%u\n", radio->gain);
    }
    CRIT_END(&sem);
    return count;
}

static ssize_t vfzsdr_frequency_store(struct device* dev,
                                 struct device_attribute* attr,
                                 const char* buf, size_t count)
{
    u32 res;
    int err;
    
    CRIT_BEG(&sem, ERESTARTSYS);
    err = kstrtou32(buf, 10, &res);
    if (!err) {
        radio->frequency = _normalize_frequency(res);
        _write_register(REG_FREQ);
        err = count;
    }
    CRIT_END(&sem);
    
    return err;
}

static ssize_t vfzsdr_frequency_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
    int count = 0;
    CRIT_BEG(&sem, ERESTARTSYS);
    if(buf) {
        count = snprintf(buf, PAGE_SIZE, "%d\n",
                         _unnormalize_frequency(radio->frequency));
    }
    CRIT_END(&sem);
    
    return count;
}

static ssize_t vfzsdr_tune_store(struct device* dev,
                                 struct device_attribute* attr,
                                 const char* buf, size_t count)
{
    s16 res;
    int err;
    
    dev_dbg(dev, "gain store");
    
    CRIT_BEG(&sem, ERESTARTSYS);
    
    err = kstrtos16(buf, 10, &res);
    
    if (!err) {
        radio->frequency += res;
        /* TODO: check bounds */
        _write_register(REG_FREQ);
        err = count;
    }
    
    CRIT_END(&sem);
    
    return err;
}

DEVICE_ATTR(status, S_IRUSR | S_IRGRP | S_IROTH,
            vfzsdr_status_show, NULL);
DEVICE_ATTR(filter, S_IWUSR | S_IWGRP | S_IRUSR | S_IRGRP | S_IROTH,
            vfzsdr_filter_show, vfzsdr_filter_store);
DEVICE_ATTR(mode, S_IWUSR | S_IWGRP | S_IRUSR | S_IRGRP | S_IROTH,
            vfzsdr_mode_show, vfzsdr_mode_store);
DEVICE_ATTR(gain, S_IWUSR | S_IWGRP | S_IRUSR | S_IRGRP | S_IROTH,
            vfzsdr_gain_show, vfzsdr_gain_store);
DEVICE_ATTR(frequency, S_IWUSR | S_IWGRP | S_IRUSR | S_IRGRP | S_IROTH,
            vfzsdr_frequency_show, vfzsdr_frequency_store);
DEVICE_ATTR(tune, S_IWUSR | S_IWGRP,
            NULL, vfzsdr_tune_store);


static const struct attribute *vfzsdr_attrs[] = {
    &dev_attr_status.attr,
    &dev_attr_filter.attr,
    &dev_attr_mode.attr,
    &dev_attr_gain.attr,
    &dev_attr_frequency.attr,
    &dev_attr_tune.attr,
    NULL,
};

static const struct attribute_group vfzsdr_device_attr_group = {
    .attrs = (struct attribute **) vfzsdr_attrs,
};

/* lowlevel i2c */

static inline u32 _normalize_frequency(u32 frequency) {
    /* 0x4795319c = (2^25/120e6) * 2^32 */
    u32 norm_freq;
    norm_freq = (((u64)frequency * 0x4795319cull) >> 32) + 1;
    /* frequency is 25 bits le */
    norm_freq = cpu_to_le32(norm_freq) & 0x1ffffff;
    return norm_freq;
}

static inline u32 _unnormalize_frequency(u32 norm_freq) {
    u32 frequency;
    frequency = ((u64)le32_to_cpu(norm_freq) * 0x393870000ull) >> 32;
    return frequency;
}

static void _write_command(u8* data) {
    print_hex_dump_debug("vfzsdr sending: ", DUMP_PREFIX_NONE, 5, 1, data, 5, true);
    i2c_master_send(client, data, 5);
}

static void _write_register(enum vfzsdr_reg reg) {

    u8 cmd[5] = {0};
    
    dev_dbg(&client->dev, "writing register %d", reg);
    
    cmd[0] = (reg << 6);
    
    switch (reg) {
        case REG_MODE:
            cmd[0] |= (radio->mode << 5);
            cmd[0] |= (radio->filter << 4);
            cmd[0] |= (radio->sideband << 3);
            cmd[0] |= (radio->forced_tx << 2);
            cmd[0] |= (radio->forced_key << 1);
            cmd[1] |= (radio->tx_att << 6);
            cmd[1] |= (radio->tx_att << 3);
            cmd[1] |= (radio->if_freq >> 2);
            cmd[2] |= (radio->if_freq << 7);
            cmd[2] |= (radio->cw_tx_nomod << 5);
            cmd[2] |= (radio->fconf << 3);
            break;
        case REG_GAIN:
            cmd[4] |= (radio->gain & 0x3f);
            break;
        case REG_FREQ:
            cmd[1] |= (radio->frequency >> (8*3)) & 0x01;
            cmd[2] |= (radio->frequency >> (8*2)) & 0xff;
            cmd[3] |= (radio->frequency >> (8*1)) & 0xff;
            cmd[4] |= (radio->frequency >> (8*0)) & 0xff;
            break;
    }
    
    _write_command(cmd);
}

// lowlevel i2c
static u8 _read_status(void) {
    u8 byte;
    /* TODO: add error checking */
    i2c_master_recv(client, &byte, 1);
    return byte;
}

static ssize_t vfzsdr_read(struct file *file, char __user *buffer,
                           size_t length, loff_t *offset) {
    int err_count = 0;
    u8 byte;
    
    
    CRIT_BEG(&sem, EBUSY);
    byte = _read_status();
    
    err_count = copy_to_user(buffer, &byte, 1);
    if (!err_count) {
        /* number of byte written to buffer */
        err_count = 1;
    }
    CRIT_END(&sem);
    
    return err_count;
}

static struct file_operations vfzsdr_fops =
{
    .owner = THIS_MODULE,
    .read = vfzsdr_read,
};


int vfzsdr_probe(struct i2c_client *client, const struct i2c_device_id *device_id) {

    dev_dbg(&client->dev, "probing");
    
    radio = (struct vfzsdr_radio*) devm_kzalloc(&client->dev,
                                                sizeof(struct vfzsdr_radio),
                                                GFP_KERNEL);
    if (!radio)
        return -ENOMEM;
    
    radio->mode        = MODE_SSB;
    radio->filter      = FILTER_WIDE;
    radio->sideband    = SIDEBAND_UPPER;
    radio->forced_tx   = false;
    radio->forced_key  = false;
    radio->tx_att      = ATT_0DB;
    radio->rx_att      = ATT_0DB;
    radio->if_freq     = IF_45MHZ;
    radio->cw_tx_nomod = true;
    radio->fconf       = FCONF_DIRECT;
    radio->gain        = (gain & 0x3f);
    radio->frequency   = _normalize_frequency(frequency);
    
    _write_register(REG_MODE);
    _write_register(REG_GAIN);
    _write_register(REG_FREQ);

    return 0;
}

static int vfzsdr_remove(struct i2c_client *client)
{
    dev_dbg(&client->dev, "removing");
    
    return 0;
}

static const struct i2c_device_id vfzsdr_id[] = {
    { "vfzsdr", 0 },
    { /* sentinel */ },
};
MODULE_DEVICE_TABLE(i2c, vfzsdr_id);

static struct i2c_driver vfzsdr_driver = {
    //.class		= I2C_CLASS_HWMON,
    .driver = {
        .owner  = THIS_MODULE,
        .name	= "vfzsdr",
    },
    .probe          = vfzsdr_probe,
    .remove         = vfzsdr_remove,
    .id_table       = vfzsdr_id,
};

static int __init vfzsdr_init(void)
{
    int ret;
    
    struct i2c_board_info board_info = {
        .type = "vfzsdr",
        .addr = address,
        
    };

    pr_debug("vfzsdr initilizing");
    
    adapter = i2c_get_adapter(busno);
    if (!adapter) {
        printk(KERN_ERR "failed to get i2c adapter\n");
        return -EINVAL;
    }
    
    client = i2c_new_device(adapter, &board_info);
    if (!client) {
        printk(KERN_ERR "failed to get i2c client\n");
        i2c_put_adapter(adapter);
        return -EINVAL;
    }
    
    ret = i2c_add_driver(&vfzsdr_driver);
    if(ret) {
        dev_warn(&client->dev, "failed to add driver");
        i2c_put_adapter(adapter);
        return ret;
    }
    
    /* Try to dynamically allocate a major number for the device */
    major_number = register_chrdev(0, DEVICE_NAME, &vfzsdr_fops);
    if (major_number < 0) {
        dev_warn(&client->dev, "failed to register device with error: %d\n",
                 major_number);
        ret = major_number;
        goto failed_chrdev;
    }
    
    /* Register the device class */
    vfzsdr_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(vfzsdr_class)) {
        //
        dev_warn(&client->dev, "failed to register device class");
        ret = PTR_ERR(vfzsdr_class);
        goto failed_class;
    }
    
    /* Register the device driver */
    vfzsdr_device = device_create(vfzsdr_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(vfzsdr_device)) {
        
        class_destroy(vfzsdr_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        
        i2c_unregister_device(client);
        i2c_del_driver(&vfzsdr_driver);
        
        dev_warn(&client->dev, "failed to create the device");
        ret = PTR_ERR(vfzsdr_device);
        goto failed_device;
    }
    
    if(sysfs_create_group(&vfzsdr_device->kobj, &vfzsdr_device_attr_group))
    {
        dev_warn(&client->dev, "device attribute group creation failed\n");
        ret = -EINVAL;
        goto failed_sysfs;
    }
    
    return ret;
    
    /* it's a bit hairy unrolling everything we've done */
failed_sysfs:
failed_device:
    class_unregister(vfzsdr_class);
    class_destroy(vfzsdr_class);
failed_class:
    unregister_chrdev(major_number, DEVICE_NAME);
failed_chrdev:
    i2c_unregister_device(client);
    i2c_del_driver(&vfzsdr_driver);
    
    return ret;
}

static void __exit vfzsdr_cleanup(void)
{
    sysfs_remove_group(&vfzsdr_device->kobj, &vfzsdr_device_attr_group);

    device_destroy(vfzsdr_class, MKDEV(major_number, 0));
    class_unregister(vfzsdr_class);
    class_destroy(vfzsdr_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    
    if(client)
        i2c_unregister_device(client);
    
    i2c_del_driver(&vfzsdr_driver);
    
    pr_debug("73s es gb");
}

module_init(vfzsdr_init);
module_exit(vfzsdr_cleanup);

MODULE_LICENSE("GPL");            ///< The license type -- this affects available functionality
MODULE_AUTHOR("Albin Stigo");    ///< The author -- visible when you use modinfo
MODULE_DESCRIPTION("A simple Linux char driver for SM6VFZ FPGA SDR");  ///< The description -- see modinfo
MODULE_VERSION("0.1");            ///< A version number to inform users

