#define DEBUG 1

#include "vfzsdr.h"


static uint frequency   = 7074000;
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
//static inline u32 _unnormalize_frequency(u32);


/* gain show/store */
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

static ssize_t vfzsdr_gain_store(struct device* dev,
                                 struct device_attribute* attr,
                                 const char* buf, size_t count)
{
    u8 res;
    int err;

    CRIT_BEG(&sem, ERESTARTSYS);
    err = kstrtou8(buf, 10, &res);
    if (!err && res < 64) {
        // TODO: compare if it's different?
        radio->gain = res;
        _write_register(REG_GAIN);
        err = count;
        sysfs_notify(&dev->kobj, NULL, "gain");
    } else if (!err) {
        dev_err(dev, "gain parameter out of range 0-63");
        err = -ERANGE;
    }
    CRIT_END(&sem);
    
    return err;
}

/* frequency show/store */
static ssize_t vfzsdr_frequency_show(struct device *dev,
                                  struct device_attribute *attr, char *buf)
{
    int count = 0;
    CRIT_BEG(&sem, ERESTARTSYS);
    if(buf) {
        // _IF_ count = snprintf(buf, PAGE_SIZE, "%u\n",
        //                 IF - _unnormalize_frequency(radio->frequency));
        count = snprintf(buf, PAGE_SIZE, "%u\n", radio->frequency);
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
    // TODO: check range
    if (!err) {
        radio->frequency = res;
        _write_register(REG_FREQ);
        err = count;
    }
    sysfs_notify(&dev->kobj, NULL, "frequency");
    CRIT_END(&sem);
    
    return err;
}

/* iqswap */
static ssize_t vfzsdr_iqswap_show(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
    int count = 0;
    CRIT_BEG(&sem, ERESTARTSYS);
    if(buf) {
        count = snprintf(buf, PAGE_SIZE, "%d\n", radio->iq_swap);
    }
    CRIT_END(&sem);
    return count;
}

static ssize_t vfzsdr_iqswap_store(struct device* dev,
                                      struct device_attribute* attr,
                                      const char* buf, size_t count)
{
    bool res;
    int err;
    
    CRIT_BEG(&sem, ERESTARTSYS);
    err = kstrtobool(buf, &res);
    if (!err) {
        radio->iq_swap = res;
        _write_register(REG_FREQ);
        err = count;
    }
    sysfs_notify(&dev->kobj, NULL, "iqswap");
    CRIT_END(&sem);
    
    return err;
}

/* forxed_tx */
static ssize_t vfzsdr_tx_show(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
    int count = 0;
    CRIT_BEG(&sem, ERESTARTSYS);
    if(buf) {
        count = snprintf(buf, PAGE_SIZE, "%d\n", radio->tx);
    }
    CRIT_END(&sem);
    return count;
}

static ssize_t vfzsdr_tx_store(struct device* dev,
                                   struct device_attribute* attr,
                                   const char* buf, size_t count)
{
    bool res;
    int err;
    
    CRIT_BEG(&sem, ERESTARTSYS);
    err = kstrtobool(buf, &res);
    if (!err) {
        radio->tx = res;
        _write_register(REG_MODE);
        // we also need to change to tx freq
        _write_register(REG_FREQ);
        err = count;
    }
    sysfs_notify(&dev->kobj, NULL, "tx");
    CRIT_END(&sem);
    
    return err;
}

/* tune store */
static ssize_t vfzsdr_tune_store(struct device* dev,
                                 struct device_attribute* attr,
                                 const char* buf, size_t count)
{
    s16 res;
    int err;
    
    err = kstrtos16(buf, 10, &res);
    
    if (!err) {

        CRIT_BEG(&sem, ERESTARTSYS);
        radio->frequency -= res;
        /* TODO: check bounds */
        _write_register(REG_FREQ);
        CRIT_END(&sem);

        err = count;
        
        sysfs_notify(&dev->kobj, NULL, "frequency");
    } else {
        dev_dbg(dev, "got %s error %d", buf, err);
    }
    
    return err;
}

DEVICE_ATTR(gain, S_IWUSR | S_IWGRP | S_IRUSR | S_IRGRP | S_IROTH,
            vfzsdr_gain_show, vfzsdr_gain_store);
DEVICE_ATTR(frequency, S_IWUSR | S_IWGRP | S_IRUSR | S_IRGRP | S_IROTH,
            vfzsdr_frequency_show, vfzsdr_frequency_store);
DEVICE_ATTR(iqswap, S_IWUSR | S_IWGRP | S_IRUSR | S_IRGRP | S_IROTH,
            vfzsdr_iqswap_show, vfzsdr_iqswap_store);
DEVICE_ATTR(tx, S_IWUSR | S_IWGRP | S_IRUSR | S_IRGRP | S_IROTH,
            vfzsdr_tx_show, vfzsdr_tx_store);
DEVICE_ATTR(tune, S_IWUSR | S_IWGRP,
            NULL, vfzsdr_tune_store);

static const struct attribute *vfzsdr_attrs[] = {
    &dev_attr_gain.attr,
    &dev_attr_frequency.attr,
    &dev_attr_iqswap.attr,
    &dev_attr_tx.attr,
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

/*static inline u32 _unnormalize_frequency(u32 norm_freq) {
    u32 frequency;
    frequency = ((u64)le32_to_cpu(norm_freq) * 0x393870000ull) >> 32;
    return frequency;
}*/

static void _write_command(u8* data) {
    // print_hex_dump_debug("vfzsdr sending: ", DUMP_PREFIX_NONE, 5, 1, data, 5, true);
    i2c_master_send(client, data, 5);
}

static void _write_register(enum vfzsdr_reg reg) {
    int norm_freq;
    u8 cmd[5] = {0};
    cmd[0] = (reg << 6);

    // dev_dbg(&client->dev, "writing register %d", reg);
    
    if (radio->tx) {
        // No IF compensation in tx
        norm_freq = _normalize_frequency(radio->frequency);
    } else {
        // IF compensation in rx
        norm_freq = _normalize_frequency(IF - radio->frequency);
    }
    
    switch (reg) {
        case REG_MODE:
            cmd[0] |= (radio->tx << 2);
            cmd[0] |= (radio->key << 1);
            cmd[1] |= (radio->tx_att << 6);
            cmd[1] |= (radio->tx_att << 3);
            cmd[1] |= (radio->if_freq >> 2);
            cmd[2] |= (radio->if_freq << 7);
            // cmd[2] |= (radio->fconf << 3);
            break;
        case REG_GAIN:
            cmd[4] |= (radio->gain & 0x3f);
            break;
        case REG_FREQ:
            cmd[0] |= (radio->loopback << 1);
            cmd[0] |= (radio->iq_swap);
            cmd[1] |= (norm_freq >> (8*3)) & 0x01;
            cmd[2] |= (norm_freq >> (8*2)) & 0xff;
            cmd[3] |= (norm_freq >> (8*1)) & 0xff;
            cmd[4] |= (norm_freq >> (8*0)) & 0xff;
            break;
        default:
            goto error;
    }
    
    _write_command(cmd);
error:
    return;
}

static ssize_t vfzsdr_read(struct file *file, char __user *buffer,
                           size_t length, loff_t *offset) {
    int err_count = 0;
    u8 byte;
    
    CRIT_BEG(&sem, EBUSY);
    
    // TODO: Add error checking
    i2c_master_recv(client, &byte, 1);
    
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
    
    radio->tx           = false;
    radio->key          = false;
    radio->tx_att      = ATT_0DB;
    radio->rx_att      = ATT_0DB;
    radio->if_freq     = IF_45MHZ;
    radio->iq_swap     = true;
    // radio->fconf       = FCONF_DIRECT;
    radio->loopback    = false;
    radio->gain        = gain;
    //radio->frequency   = IF - _normalize_frequency(frequency);
    radio->frequency   = frequency;
    
    /* Write all registers */
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
MODULE_DESCRIPTION("A Linux char driver for SM6VFZ FPGA SDR");  ///< The description -- see modinfo
MODULE_VERSION("0.1");            ///< A version number to inform users
