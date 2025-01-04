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

#include "aesdchar.h"
#include "aesd-circular-buffer.h"

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/rwsem.h>
#include <linux/types.h>
#include <linux/uaccess.h>

int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("serges147");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

static int aesd_open(struct inode *const inode, struct file *const filp)
{
    PDEBUG("open\n");

    struct aesd_dev *const dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    return 0;
}

static int aesd_release(struct inode *const inode, struct file *const filp)
{
    PDEBUG("release\n");
    return 0;
}

static ssize_t aesd_read(struct file *const filp, char __user *const buf, const size_t count, loff_t *const f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld\n", count, *f_pos);

    if (count == 0)
    {
        return 0;
    }

    struct aesd_dev *const dev = filp->private_data;
    assert(dev);

    int res = down_read_interruptible(&dev->lock);
    if (res == 0)
    {
        size_t offset;
        const struct aesd_buffer_entry *const entry = aesd_circular_buffer_find_entry_offset_for_fpos(
            &dev->buffer,
            *f_pos,
            &offset);
        if (entry)
        {
            assert(entry->buffptr);
            assert(offset < entry->size);

            const size_t size = min(count, entry->size - offset);
            if (0 == copy_to_user(buf, entry->buffptr + offset, size))
            {
                *f_pos += size;
                retval = size;
            }
            else
            {
                retval = -EFAULT;
            }
        }
        else
        {
            retval = 0; // EOF
        }

        up_read(&dev->lock);
    }
    else
    {
        retval = -ERESTARTSYS;
    }
    return retval;
}

static ssize_t aesd_write(struct file *const filp, const char __user *const user_buf, const size_t count, loff_t *const f_pos)
{
    PDEBUG("write %zu bytes with offset %lld\n", count, *f_pos);

    if (count == 0)
    {
        return 0;
    }

    struct aesd_dev *const dev = filp->private_data;
    assert(dev);

    char *kern_buf = NULL;
    bool is_locked = false;
    ssize_t retval = -ENOMEM;
    struct aesd_buffer_entry evicted_entry = {.buffptr = NULL, .size = 0};
    do
    {
        // Allocate a new buffer and copy the data from the user
        // even before acquiring the lock to minimize the locking time.
        //
        kern_buf = kmalloc(count, GFP_KERNEL);
        if (!kern_buf)
        {
            retval = -ENOMEM;
            break;
        }
        if (copy_from_user(kern_buf, user_buf, count))
        {
            retval = -EFAULT;
            break;
        }
        const bool newline_terminated = (kern_buf[count - 1] == '\n');

        // Lock the write access.
        //
        if (down_write_killable(&dev->lock))
        {
            retval = -EINTR;
            break;
        }
        is_locked = true;

        // Append the new buffer to the temporary one (if any).
        //
        if (dev->temp_entry.buffptr)
        {
            // Resize the temporary buffer to accommodate the new fragment.
            //
            const size_t new_size = dev->temp_entry.size + count;
            char* const new_buffptr = krealloc(dev->temp_entry.buffptr, new_size, GFP_KERNEL);
            if (!new_buffptr)
            {
                retval = -ENOMEM;
                break;
            }
            
            // Append (copy) the new fragment.
            //
            memcpy(new_buffptr + dev->temp_entry.size, kern_buf, count);
            dev->temp_entry.size = new_size;
            dev->temp_entry.buffptr = new_buffptr;
            // `kern_buf` will be freed later.
        }
        else
        {
            // Store (move) the new fragment.
            //
            dev->temp_entry.size = count;
            dev->temp_entry.buffptr = kern_buf;
            kern_buf = NULL;
        }
        retval = count;

        if (newline_terminated)
        {
            evicted_entry = aesd_circular_buffer_add_entry(&dev->buffer, &dev->temp_entry);
            dev->temp_entry.size = 0;
            dev->temp_entry.buffptr = NULL;
        }

    } while (0);

    if (is_locked)
    {
        up_write(&dev->lock);
        is_locked = false;
    }
    if (evicted_entry.buffptr)
    {
        kfree(evicted_entry.buffptr);
        evicted_entry.size = 0;
        evicted_entry.buffptr = NULL;
    }
    if (kern_buf)
    {
        kfree(kern_buf);
        kern_buf = NULL;
    }

    return retval;
}

struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *const dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err)
    {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

static int __init aesd_init_module(void)
{
    PDEBUG("init\n");

    dev_t dev = 0;
    int result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0)
    {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device, 0, sizeof(struct aesd_dev));

    init_rwsem(&aesd_device.lock);
    aesd_device.temp_entry.size = 0;
    aesd_device.temp_entry.buffptr = NULL;
    aesd_circular_buffer_init(&aesd_device.buffer);

    result = aesd_setup_cdev(&aesd_device);
    if (result)
    {
        unregister_chrdev_region(dev, 1);
    }

    return result;
}

static void __exit aesd_cleanup_module(void)
{
    PDEBUG("exit\n");

    const dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    size_t index;
    struct aesd_buffer_entry *entryptr;
    AESD_CIRCULAR_BUFFER_FOREACH(entryptr, &aesd_device.buffer, index)
    {
        if (entryptr->buffptr)
        {
            kfree(entryptr->buffptr);
            entryptr->buffptr = NULL;
            entryptr->size = 0;
        }
    }
    if (aesd_device.temp_entry.buffptr)
    {
        kfree(aesd_device.temp_entry.buffptr);
        aesd_device.temp_entry.size = 0;
        aesd_device.temp_entry.buffptr = NULL;
    }

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
