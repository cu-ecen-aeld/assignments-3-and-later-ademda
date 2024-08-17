/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Julian Muellner");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

void debug_print_bytes(const char* prefix, const char* b, size_t size) {
    char* tmp = kmalloc(size + 1, GFP_KERNEL);
    if(tmp) {
        memcpy(tmp, b, size);
        tmp[size] = '\0';
        PDEBUG("%s: %s", prefix, tmp);
        kfree(tmp);
    }
}

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;

    PDEBUG("open");
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = &aesd_device;

    if(dev != &aesd_device) 
        PDEBUG("WARNING: ased_dev does not match aesd_device");
    
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev;
    struct aesd_buffer_entry *target_entry;
    size_t entry_offset, remaining_bytes, written_bytes;
    ssize_t retval = 0;

    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

    dev = filp->private_data;
    while(mutex_lock_interruptible(&dev->aesd_dev_lock)) {}

    target_entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &entry_offset);
    if(target_entry == NULL) {
        goto write_ret;
    }

    remaining_bytes = target_entry->size - entry_offset;
    written_bytes = count > remaining_bytes ? remaining_bytes : count;

    if (copy_to_user(buf, target_entry->buffptr + entry_offset, written_bytes)) {
        retval = -EFAULT;
        goto write_ret;
    }
    retval = written_bytes;
    *f_pos += written_bytes;

    debug_print_bytes("Reading bytes", target_entry->buffptr + entry_offset, count > remaining_bytes ? remaining_bytes : count);

write_ret:
    mutex_unlock(&dev->aesd_dev_lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    struct aesd_dev *dev;
    char *tmp;

    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);
    
    dev = filp->private_data;

    // acq lock
    while(mutex_lock_interruptible(&dev->aesd_dev_lock)) {}

    tmp = kmalloc(dev->tmp_entry.size + count, GFP_KERNEL);
    if(tmp == NULL) {
        goto read_ret;
    }

    memcpy(tmp, dev->tmp_entry.buffptr, dev->tmp_entry.size);
    if(copy_from_user(tmp + dev->tmp_entry.size, buf, count)) {
        retval = -EFAULT;
        kfree(tmp);
        goto read_ret;
    }
    
    dev->tmp_entry.size += count;
    kfree(dev->tmp_entry.buffptr);
    dev->tmp_entry.buffptr = tmp; 
    retval = count;

    // free memory
    if(dev->tmp_entry.buffptr[dev->tmp_entry.size - 1] == '\n') {
        debug_print_bytes("Commiting write string", dev->tmp_entry.buffptr, dev->tmp_entry.size - 1);

        kfree(aesd_circular_buffer_add_entry(&dev->buffer, &dev->tmp_entry));
        dev->tmp_entry.buffptr = NULL;
        dev->tmp_entry.size = 0;
    }

read_ret:
    mutex_unlock(&dev->aesd_dev_lock);
    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    mutex_init(&aesd_device.aesd_dev_lock);
    aesd_circular_buffer_init(&aesd_device.buffer);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    uint8_t index;
    struct aesd_buffer_entry *entry;
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    // free all memory
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index) {
        kfree(entry->buffptr);
    }
    
    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
