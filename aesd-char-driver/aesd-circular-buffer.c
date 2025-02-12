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
#include <linux/string.h>
#else
#include <string.h>
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
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos( //
    struct aesd_circular_buffer *const buffer,
    const size_t char_offset,
    size_t *const entry_offset_byte_rtn)
{
    // Prototype of this function was purely designed b/c neither `buffer` nor return value should be mutable.
    // But I can't correct it (b/c validation test cases are already written expecting non-`const` return pointer).
    // To enforce const-correctness in the following algorithm, I'm using const `buf` instead of the original `buffer`.
    const struct aesd_circular_buffer *const buf = buffer;

    if (buf == NULL)
    {
        return NULL;
    }
    assert(buf->in_offs < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);
    assert(buf->out_offs < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);
    assert(!buf->full || (buf->in_offs == buf->out_offs));

    for (size_t size = aesd_circular_buffer_size(buf), offset = 0, index = buf->out_offs;
         size > 0;
         --size)
    {
        if (char_offset < (offset + buf->entry[index].size))
        {
            *entry_offset_byte_rtn = char_offset - offset;
            return buffer->entry + index; // this is the only place where I have to use original "mutable" buffer
        }

        offset += buf->entry[index].size;

        index += 1;
        if (index == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
        {
            index = 0;
        }
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
struct aesd_buffer_entry aesd_circular_buffer_add_entry( //
    struct aesd_circular_buffer *const buffer,
    const struct aesd_buffer_entry *const new_entry)
{
    struct aesd_buffer_entry evicted_entry = {.buffptr = NULL, .size = 0};
    if (buffer == NULL)
    {
        return evicted_entry;
    }
    assert(buffer->in_offs < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);
    assert(buffer->out_offs < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);
    assert(!buffer->full || (buffer->in_offs == buffer->out_offs));

    if (buffer->full)
    {
        evicted_entry = buffer->entry[buffer->in_offs];
    }

    buffer->entry[buffer->in_offs] = *new_entry;
    buffer->in_offs += 1;
    if (buffer->in_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
    {
        buffer->in_offs = 0;
    }

    if (buffer->full)
    {
        buffer->out_offs = buffer->in_offs;
    }
    else if (buffer->in_offs == buffer->out_offs)
    {
        buffer->full = true;
    }

    return evicted_entry;
}

/**
 * Initializes the circular buffer described by @param buffer to an empty struct
 */
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer, 0, sizeof(struct aesd_circular_buffer));
}

size_t aesd_circular_buffer_size(const struct aesd_circular_buffer *const buffer)
{
    return (!buffer->full && (buffer->in_offs >= buffer->out_offs))
               ? (buffer->in_offs - buffer->out_offs)
               : (buffer->in_offs + AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - buffer->out_offs);
}

size_t aesd_circular_buffer_bytes_size(const struct aesd_circular_buffer *const buffer)
{
    size_t bytes = 0;
    for (size_t size = aesd_circular_buffer_size(buffer), index = buffer->out_offs;
         size > 0;
         --size)
    {
        bytes += buffer->entry[index].size;

        index += 1;
        if (index == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
        {
            index = 0;
        }
    }
    return bytes;
}

bool aesd_circular_buffer_offset_at(
    const struct aesd_circular_buffer *const buffer,
    const size_t entry_index,
    const size_t entry_offset,
    size_t *const offset_rtn)
{
    assert(offset_rtn);

    const size_t size = aesd_circular_buffer_size(buffer);
    if (entry_index >= size)
    {
        // Early return if the entry index is out of bounds.
        return false;
    }

    size_t global_offset = 0, out_offs = buffer->out_offs;
    for (size_t index = 0; index < entry_index; ++index)
    {
        global_offset += buffer->entry[out_offs].size;

        out_offs += 1;
        if (out_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
        {
            out_offs = 0;
        }
    }

    if (entry_offset >= buffer->entry[out_offs].size)
    {
        return false;
    }

    *offset_rtn = global_offset + entry_offset;
    return true;
}
