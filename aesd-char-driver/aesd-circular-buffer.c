/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */


#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"



static struct aesd_buffer_entry* get_entry_at_virtual_offset(struct aesd_circular_buffer *buffer, uint8_t voffset)
{
    size_t offset = buffer->out_offs;
    offset += voffset;
    offset %= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    return &(buffer->entry[offset]);
}

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffPtr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    const bool inAndOutSame = (buffer->in_offs == buffer->out_offs);

    if(inAndOutSame && !buffer->full) //the buffer is empty
        {return NULL;}

    uint8_t entryOffset = 0;
    size_t totalPassed = 0;

    while(totalPassed <= char_offset && entryOffset < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
    {
        totalPassed += get_entry_at_virtual_offset(buffer, entryOffset)->size;
        entryOffset++;
    }

    if(totalPassed <= char_offset) //index not found
        {return NULL;}

    struct aesd_buffer_entry* entry = get_entry_at_virtual_offset(buffer, entryOffset-1);
    totalPassed -= entry->size;
    *entry_offset_byte_rtn = char_offset - totalPassed;


    return entry;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
* @returns NULL if not full or a pointer to a *buffptr member of aesd_buffer_entry that needs to be freed.
*/
const char* aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    const char* retVal = buffer->entry[buffer->in_offs].buffptr;

    buffer->entry[buffer->in_offs] = *add_entry;
    buffer->in_offs++;

    if(buffer->in_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
        {buffer->in_offs = 0;}

    //if buffer was already full, these should be kept in lock step.
    if(buffer->full)
        {buffer->out_offs = buffer->in_offs;}
    else
        {buffer->full = (buffer->in_offs == buffer->out_offs);}

    return retVal;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
