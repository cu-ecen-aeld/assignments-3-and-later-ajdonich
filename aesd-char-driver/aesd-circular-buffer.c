/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#include "aesd-circular-buffer.h"

#ifdef __KERNEL__
#include <linux/kern_levels.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>

#define LOG_ERR KERN_ERR
#define LOG_DEBUG KERN_DEBUG
#define mem_free(ptr) kfree(ptr)
#define mem_allocate(size) kmalloc(size, GFP_KERNEL)
#define log_msg(typ, ...) printk(typ, __VA_ARGS__)

#else
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#define mem_free(ptr) free(ptr)
#define mem_allocate(size) malloc(size)
#define log_msg(typ, ...) syslog(typ, __VA_ARGS__)
#endif


/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer, 
size_t char_offset, size_t *entry_offset_byte_rtn ) {
    // Evaluate number of elements in circular buffer
    uint8_t sz = ((MAXSZ + (buffer->in_offs - buffer->out_offs)) % MAXSZ);
    if (sz == 0 && buffer->full) sz = MAXSZ;

    // Iterate buffer, decrementing char_offset until appropriate entry reached
    for (uint8_t i=0, j=buffer->out_offs; i < sz; i++, j = ((j+1) % MAXSZ)) {
        if (char_offset < buffer->entry[j].size) {
            log_msg(LOG_DEBUG, "Found '%s' at pos %u offset %li in find_entry_offset_for_fpos", 
                buffer->entry[j].buffptr, j, char_offset);

            *entry_offset_byte_rtn = char_offset;
            return &buffer->entry[j];
        }

        char_offset -= buffer->entry[j].size;
    }

    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry) {
    // Allocate a new entry buffer (w/terminating null byte)
    char *buffptr = (char *)mem_allocate(add_entry->size + 1);
    if (buffptr == NULL) {
        log_msg(LOG_ERR, "ERROR in aesd_circular_buffer_add_entry::mem_allocate");
        return;
    }
    memcpy(buffptr, add_entry->buffptr, add_entry->size);
    buffptr[add_entry->size] = '\0';

    // If full, free and incr head first
    if (buffer->full) {
        log_msg(LOG_DEBUG, "Deallocating entry at pos %u in buffer_add_entry", buffer->out_offs);
        mem_free((void*)buffer->entry[buffer->out_offs].buffptr);
        buffer->out_offs = (buffer->out_offs + 1) % MAXSZ;
    }

    // Set fields to write entry 
    buffer->entry[buffer->in_offs].size = add_entry->size;
    buffer->entry[buffer->in_offs].buffptr = (const char *)buffptr;
    log_msg(LOG_DEBUG, "Adding '%s' at pos %u in buffer_add_entry", 
        buffer->entry[buffer->in_offs].buffptr, buffer->in_offs);
    
    // Incr tail and set full flag
    buffer->in_offs = (buffer->in_offs + 1) % MAXSZ;
    buffer->full = (buffer->in_offs == buffer->out_offs);
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer) {
    memset(buffer, 0, sizeof(struct aesd_circular_buffer));
}
