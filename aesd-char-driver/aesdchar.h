/*
 * aesdchar.h
 *
 *  Created on: Oct 23, 2019
 *      Author: Dan Walkes
 */

#ifndef AESD_CHAR_DRIVER_AESDCHAR_H_
#define AESD_CHAR_DRIVER_AESDCHAR_H_

#include "aesd-circular-buffer.h"
#include <linux/mutex.h>

#define AESD_DEBUG 1  //Remove comment on this line to enable debug

#undef PDEBUG             /* undef it, just in case */
#ifdef AESD_DEBUG
#  ifdef __KERNEL__
     /* This one if debugging is on, and kernel space */
#    define PDEBUG(fmt, args...) printk( KERN_DEBUG "aesdchar: " fmt, ## args)
#  else
     /* This one for user space */
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

struct aesd_entry_stage
{
    // Store partially completed entry
    char * buffPtr;
    // Store the size of the buffer
    size_t size;
    // Store where we last wrote in the buffer
    size_t endIdx;
};

struct aesd_dev
{
    struct cdev cdev;     /* Char device structure */
    struct aesd_entry_stage entry; // The entry we are currently filling
    struct mutex entryMutex; // For locking the entry
    struct aesd_circular_buffer buff; // The buffer
    struct mutex buffMutex; // For locking the circular buffer
};


#endif /* AESD_CHAR_DRIVER_AESDCHAR_H_ */
