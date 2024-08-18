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
 * @modifed_by Daniel Mendez for AESD Assignment 8
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/slab.h>
#include "aesdchar.h"

#define TEMP_BUFFER_SIZE 0
#define TEMP_CHUNK_SIZE 40
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Daniel Mendez"); 
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;
char * temp_buffer = NULL;
size_t temp_buffer_size = 0;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
	//Setup fil-->private data
	
	struct aesd_dev *dev;
	
	dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
	filp->private_data = dev;
	
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
	
	//Nothing to do as the memory was allocated in init_module
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
    struct aesd_circular_buffer *circular_buffer = &dev->circular_buffer;
    struct aesd_buffer_entry *entry = NULL;
    size_t bytes_to_read;
    ssize_t retval = 0;
    size_t entry_offset_byte;
    
    if (buf == NULL) {
        retval = -EFAULT;
        goto exit;
    }

    mutex_lock(&dev->lock);


    entry = aesd_circular_buffer_find_entry_offset_for_fpos(circular_buffer, *f_pos, &entry_offset_byte);
    
    if(entry == NULL) {
        retval = 0;
        goto exit;
    }

    bytes_to_read = entry->size - entry_offset_byte;

    if (bytes_to_read > count) {
        bytes_to_read = count;
    }


    if (copy_to_user(buf, entry->buffptr + entry_offset_byte, bytes_to_read)) {
        retval = -EFAULT;
        goto exit;
    }

    *f_pos += bytes_to_read;
    retval = bytes_to_read;

    exit:
        mutex_unlock(&dev->lock);
        return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
	int res;
	int bytes_scanned = 0;
    ssize_t retval = -ENOMEM;
	
	
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    
	//Get the aesd device structure
	struct aesd_dev *dev = filp->private_data;
	//First check to see if there is a partial command ongoing
	//Lock the mutex
	res = mutex_lock_interruptible(&(dev->lock));
	if(res){
		return -ERESTARTSYS;
	}
	//Allocate for the incoming data
	char * new_data = (char *)kmalloc(count,GFP_KERNEL);
	if(new_data == NULL){
		goto exit ;
	}
	//Set the new data to zeros
	memset(new_data,0,count)		;
	//Copy over the user data to the newly allocated memory
	res =  copy_from_user(new_data,buf,count);
	
	//Iterate until we scanned everything inside
	while(bytes_scanned <= (count - 1)){
		//We realloc to add a byte and increase the size by one
		temp_buffer = krealloc(temp_buffer,++temp_buffer_size,GFP_KERNEL);
		//Check if allocation successful
		if(!temp_buffer){
			retval = -ENOMEM;
			goto exit;
		}
		//Store the current character at the end of the temp buffer
		temp_buffer[temp_buffer_size-1] = new_data[bytes_scanned];
		
		
		//Check to see if the new character added was a newline
		if(new_data[bytes_scanned] == '\n'){
			//Now we have to copy the temp buffer into the circular buffer
			//Create a temporary new entry object
			//We first allocate a buffer for the current size of the temp buffer
			//We allocate for the new entry
			struct aesd_buffer_entry new_entry;
			new_entry.buffptr = (char *)kmalloc(temp_buffer_size,GFP_KERNEL);
			new_entry.size = temp_buffer_size;
			//Now copy over the data
			memcpy(new_entry.buffptr,temp_buffer,temp_buffer_size);
			
	
			//First check if the circular buffer is full, then deallocate the current location
			if(dev->circular_buffer.full){
				//Free the oldest entry
				if(dev->circular_buffer.entry[dev->circular_buffer.in_offs].buffptr)
				kfree(dev->circular_buffer.entry[dev->circular_buffer.in_offs].buffptr);
				//No need to change the size since it would immediately be overwritten?			
			}
			
			//Store it into the circular buffer directly
			aesd_circular_buffer_add_entry(&(dev->circular_buffer),&new_entry);
			
			//After inserting we can reallocate the temporary buffer to be zero and set the size to be zero
			temp_buffer = krealloc(temp_buffer,0,GFP_KERNEL);
			temp_buffer_size = 0;
			
		}
		//Increase the characters scanned by one
		bytes_scanned++;
		
	}
	retval = bytes_scanned;
	*f_pos += bytes_scanned;
	
	exit:
		mutex_unlock(&dev->lock);
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

    //Initialize the mutex used
	mutex_init(&aesd_device.lock);
	
	//Initalize the circular buffer
	aesd_circular_buffer_init(&aesd_device.circular_buffer); // Probably not necessary since already set to zero?
	
	//Alloate memory for the temporary entry buffer
	temp_buffer = (char *)kmalloc(TEMP_BUFFER_SIZE,GFP_KERNEL);
	
	
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
	
	uint8_t index;
	struct aesd_buffer_entry *entryptr;

	
	//Free the temp pointer
	if(temp_buffer)
		kfree(temp_buffer);
	
	//Free all allocated memory in the circular buffer
	AESD_CIRCULAR_BUFFER_FOREACH(entryptr,&aesd_device.circular_buffer,index){
		if(entryptr->buffptr)
			kfree(entryptr->buffptr);
	}
	//Destory the mutex
	mutex_destroy(&aesd_device.lock);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
