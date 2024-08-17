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
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Daly Adem"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

bool contains_newline(const char *buffer) {
    size_t i;
    size_t a=strlen(buffer);
    for (i = 0; i < a; i++) {
        if (buffer[i] == '\n') {
            return true;  // Newline found
        }
    }
    return false;  // Newline not found
}

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");

    dev=container_of(inode->i_cdev,struct aesd_dev, cdev);
    filp->private_data=dev;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    size_t *pos_result;
    struct aesd_buffer_entry *p=aesd_circular_buffer_find_entry_offset_for_fpos(&filp->private_data,*f_pos, pos_result)
    copy_to_user(buf,p->(buffptr+f_pos),sizeof(p->buffptr));

    PDEBUG("read %zu bytes with offset %lld\n",count,*f_pos);
    retval=*pos_result;
    *f_pos=*f_pos+*pos_result;
    if (retval<0){
        PDEBUG("problem in reading\n");
        return -EFAULT;
    }
    else if(retval==0)PDEBUG("file is ended\n");
    else if(retval>0 && retval<count)PDEBUG("didn't return all the bytes\n");
    else PDEBUG("returned all the bytes\n");
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    mutex_lock(&filp->private_data->lock);
    struct aesd_buffer_entry *tmp_entry;
    struct aesd_buffer_entry *tmp2_entry;
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    tmp_entry = kmalloc(sizeof(struct aesd_buffer_entry), GFP_KERNEL);
    tmp2_entry = kmalloc(sizeof(struct aesd_buffer_entry), GFP_KERNEL);
    
    if (!tmp_entry || !tmp2_entry) {
        kfree(tmp_entry);
        kfree(tmp2_entry);
        return retval;  // Memory allocation failed
    }

    tmp2_entry->buffptr = kmalloc(count, GFP_KERNEL);
    if (!tmp2_entry->buffptr) {
            kfree(tmp_entry);
            kfree(tmp2_entry);
            return retval;  // Memory allocation failed
    }

    tmp_entry->buffptr = kmalloc(tmp2_entry->size + count, GFP_KERNEL);
    if (!tmp_entry->buffptr) {
        kfree(tmp2_entry->buffptr);
        kfree(tmp_entry);
        kfree(tmp2_entry);
        return retval;  // Memory allocation failed
    }
    
   
    if (copy_from_user(tmp2_entry->buffptr, buf, count)) {
        kfree(tmp2_entry->buffptr);
        kfree(tmp_entry->buffptr);
        kfree(tmp_entry);
        kfree(tmp2_entry);
        return -EFAULT;  // Failed to copy data from user space
    }
    tmp2_entry->size = count;

    memcpy(tmp_entry->buffptr + tmp_entry->size, buf, count);
    tmp_entry->size += count;
    
    if (contains_newline(tmp_entry->buffptr)) aesd_circular_buffer_add_entry(&filp->private_data, tmp_entry);
    
    if (tmp_entry->size<0) 
    {
        kfree(tmp2_entry->buffptr);
        kfree(tmp_entry->buffptr);
        kfree(tmp_entry);
        kfree(tmp2_entry);
        return retval;
    }
    else 
    {
        size_t a= tmp_entry->size
        kfree(tmp2_entry->buffptr);
        kfree(tmp_entry->buffptr);
        kfree(tmp_entry);
        kfree(tmp2_entry);
        return a;
    }
    mutex_unlock(&filp->private_data->lock);
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
    mutex_init(&aesd_device.lock);
    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);
    kfree(&aesd_device.buffer)
    kfree(&aesd_device.entry)
    kfree(&aesd_device.lock)
    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
