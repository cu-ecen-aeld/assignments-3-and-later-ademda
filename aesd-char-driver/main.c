#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Adrian Hall");
MODULE_LICENSE("Dual BSD/GPL");


struct aesd_dev aesd_device =
{
    .entry.buffPtr = NULL,
    .entry.size = KMALLOC_MAX_SIZE,
    .entry.endIdx = 0,
};

void aesd_cleanup_module(void);

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    struct aesd_dev* dev;
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    struct aesd_dev* dev;
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    return 0;
}

/*
 * filp  = file pointer
 * buf   = destination buffer
 * count = Size of the destination buffer
 * f_pos = Character position to start reading from
 */
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev* dev = filp->private_data;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    /*
     Acquire the lock.
     If we are interrupted while waiting,
      return and ask the kernel to automatically retry the call.
    */
    if (mutex_lock_interruptible(&dev->buffMutex))
        {return -ERESTARTSYS;}

    struct aesd_buffer_entry* entryPtr = NULL;
    size_t entryOffset = 0;

    // Get the sub-buffer that contains the specified character
    entryPtr = aesd_circular_buffer_find_entry_offset_for_fpos(
            &dev->buff,
            (size_t)(*f_pos),
            &entryOffset);

    //The specified character does not exist in the buffer.
    if(entryPtr == NULL)
        {
        PDEBUG("no entry found");
        goto end;
        }

    const size_t entrySize = entryPtr->size - entryOffset;
    const size_t sizeToRead = entrySize < count ? entrySize : count;
    const size_t notCopied = copy_to_user(buf, entryPtr->buffptr + entryOffset, sizeToRead);
    PDEBUG("notcopied is %zu",notCopied);
    retval = sizeToRead - notCopied;
    *f_pos = *f_pos + retval;
    PDEBUG("retval is %zu", retval);

    end:
    mutex_unlock(&dev->buffMutex);
    return retval;
}


ssize_t StorePartial(struct aesd_dev* dev, const char __user* buf, size_t count)
{
    ssize_t retval = 0;
    PDEBUG("Storing partial of %zu bytes",count);

    if(count == 0)
        {return 0;}


    if(dev == NULL)
    {
    PDEBUG("No device");
    goto end;
    }

    if(count == 0)
        {}

//Allocate the buffer if needed
    if(dev->entry.buffPtr == NULL)
    {
        char* newBuff = kmalloc(KMALLOC_MAX_SIZE, GFP_KERNEL);
        if(newBuff == NULL)
        {
            PDEBUG("Failed to allocate memory for partial");
            retval = -ENOMEM;
            goto end;
        }
        dev->entry.buffPtr = newBuff;
    }

    //Catch it if the whole thing would be too large
    if(dev->entry.endIdx + count > dev->entry.size)
    {
        PDEBUG("Write count exceeds maximum entry size");
        retval = -ENOMEM;
        goto end;
    }

    //Store the shizzle.
    for(size_t i = 0; i < count; ++i)
        {dev->entry.buffPtr[dev->entry.endIdx + i] = buf[i];}
    dev->entry.endIdx += count;

    PDEBUG("Partial stored");
    end:
    return retval;
}

//must already hold entry mutex
ssize_t FlushPartial(struct aesd_dev* dev)
{
    PDEBUG("Flushing partial");

    //this is wrong, but ok for this simple example
    if (mutex_lock_interruptible(&dev->buffMutex))
        {
        PDEBUG("BuffMutex bad time");
        return -ENOMEM;
        }


    if(dev->entry.buffPtr == NULL)
    {
        PDEBUG("No partial to flush");
        return -ENOMEM;
    }

    struct aesd_buffer_entry newEntry;
    newEntry.buffptr = dev->entry.buffPtr;
    newEntry.size = dev->entry.endIdx;
    //transfers ownership of the buffer.
    const char* freePtr =
            aesd_circular_buffer_add_entry(&dev->buff, &newEntry);
    kfree(freePtr);

    dev->entry.buffPtr = NULL;
    dev->entry.endIdx = 0;

    mutex_unlock(&dev->buffMutex);
    return 0;
}

ssize_t aesd_write(struct file *filp, const char __user* buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    struct aesd_dev* dev = filp->private_data;

   dev->entry.size = KMALLOC_MAX_SIZE;

    /*
    Acquire the lock.
    If we are interrupted while waiting,
    return and ask the kernel to automatically retry the call.
    */
    if (mutex_lock_interruptible(&dev->entryMutex))
        {return -ERESTARTSYS;}

    // Verify that the request is possible.
    if((count > dev->entry.size) ||
        (dev->entry.endIdx + count > dev->entry.size ))
    {
        PDEBUG("write request too large duder: %lu, %lu,  %ld",
               dev->entry.size, dev->entry.endIdx, count);
        retval = -ENOMEM;
        goto end;
    }


    size_t inputIdx = 0;
    size_t inputBaseIdx = 0;

    while(inputBaseIdx + inputIdx < count)
    {
        if(buf[inputBaseIdx + inputIdx++] == '\n')
        {
            if(StorePartial(dev, buf + inputBaseIdx, inputIdx) != 0)
                {
                PDEBUG("Failed to store partial");
                retval = -ENOMEM;
                goto end;
                }

            FlushPartial(dev);
            inputBaseIdx = inputBaseIdx + inputIdx;
            inputIdx = 0;
        }
        //else keep searching
    }

    //If we are partway through a sentence, store it
    if(inputBaseIdx < count)
        {
        PDEBUG("Some left over");
        StorePartial(dev, buf + inputBaseIdx, count - inputBaseIdx);
        }

    retval = count;

    end:
    mutex_unlock(&dev->entryMutex);
    PDEBUG("Finished write");
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
    if (err)
        {printk(KERN_ERR "Error %d adding aesd cdev", err);}
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0)
    {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        goto fail;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    mutex_init(&aesd_device.entryMutex);
    mutex_init(&aesd_device.buffMutex);

    aesd_circular_buffer_init(&aesd_device.buff);
    result = aesd_setup_cdev(&aesd_device);

    if( result )
        {unregister_chrdev_region(dev, 1);}
    return result;

    fail:
        aesd_cleanup_module();
        return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    cdev_del(&aesd_device.cdev);

    PDEBUG("Cleaning up");

    struct aesd_buffer_entry* e;
    uint8_t i;
    AESD_CIRCULAR_BUFFER_FOREACH(e,&(aesd_device.buff),i)
        {kfree(e->buffptr);}
    kfree(aesd_device.entry.buffPtr);
    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
