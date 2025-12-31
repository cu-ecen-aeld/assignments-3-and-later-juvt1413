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
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include "aesdchar.h"
#include "aesd-circular-buffer.h"
#include "aesd_ioctl.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("mdawsari");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    PDEBUG("open");
    
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    filp->private_data = NULL;
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry = NULL;
    size_t entry_offset_byte = 0;
    size_t bytes_to_read = 0;
    
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    
    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;
    
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->circular_buffer, *f_pos, &entry_offset_byte);
    
    if (entry == NULL) {
        mutex_unlock(&dev->lock);
        return 0;
    }
    
    bytes_to_read = entry->size - entry_offset_byte;
    if (bytes_to_read > count)
        bytes_to_read = count;
    
    if (copy_to_user(buf, entry->buffptr + entry_offset_byte, bytes_to_read)) {
        retval = -EFAULT;
    } else {
        retval = bytes_to_read;
        *f_pos += bytes_to_read;
    }
    
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *dev = filp->private_data;
    char *kernel_buf = NULL;
    char *newline_pos = NULL;
    char *complete_buffer = NULL;
    size_t complete_size = 0;
    struct aesd_buffer_entry new_entry;
    struct aesd_buffer_entry *old_entry = NULL;
    
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    
    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;
    
    kernel_buf = kmalloc(count, GFP_KERNEL);
    if (!kernel_buf) {
        retval = -ENOMEM;
        goto out;
    }
    
    if (copy_from_user(kernel_buf, buf, count)) {
        retval = -EFAULT;
        goto free_kernel_buf;
    }
    
    if (dev->partial_write_buffer) {
        size_t new_size = dev->partial_write_size + count;
        char *temp = krealloc(dev->partial_write_buffer, new_size, GFP_KERNEL);
        if (!temp) {
            retval = -ENOMEM;
            goto free_kernel_buf;
        }
        dev->partial_write_buffer = temp;
        memcpy(dev->partial_write_buffer + dev->partial_write_size, kernel_buf, count);
        dev->partial_write_size = new_size;
        
        newline_pos = memchr(dev->partial_write_buffer, '\n', dev->partial_write_size);
        if (newline_pos) {
            complete_size = (newline_pos - dev->partial_write_buffer) + 1;
            complete_buffer = dev->partial_write_buffer;
            
            // If there's data after the newline, save it as new partial write
            size_t remaining = dev->partial_write_size - complete_size;
            if (remaining > 0) {
                char *new_partial = kmalloc(remaining, GFP_KERNEL);
                if (new_partial) {
                    memcpy(new_partial, dev->partial_write_buffer + complete_size, remaining);
                    dev->partial_write_buffer = new_partial;
                    dev->partial_write_size = remaining;
                } else {
                    // If allocation fails, just use the complete part
                    kfree(dev->partial_write_buffer);
                    dev->partial_write_buffer = NULL;
                    dev->partial_write_size = 0;
                }
            } else {
                dev->partial_write_buffer = NULL;
                dev->partial_write_size = 0;
            }
        } else {
            retval = count;
            goto free_kernel_buf;
        }
    } else {
        newline_pos = memchr(kernel_buf, '\n', count);
        if (newline_pos) {
            complete_size = (newline_pos - kernel_buf) + 1;
            complete_buffer = kmalloc(complete_size, GFP_KERNEL);
            if (!complete_buffer) {
                retval = -ENOMEM;
                goto free_kernel_buf;
            }
            memcpy(complete_buffer, kernel_buf, complete_size);
            
            // If there's data after the newline, save it as partial write
            size_t remaining = count - complete_size;
            if (remaining > 0) {
                dev->partial_write_buffer = kmalloc(remaining, GFP_KERNEL);
                if (!dev->partial_write_buffer) {
                    kfree(complete_buffer);
                    retval = -ENOMEM;
                    goto free_kernel_buf;
                }
                memcpy(dev->partial_write_buffer, kernel_buf + complete_size, remaining);
                dev->partial_write_size = remaining;
            }
        } else {
            dev->partial_write_buffer = kmalloc(count, GFP_KERNEL);
            if (!dev->partial_write_buffer) {
                retval = -ENOMEM;
                goto free_kernel_buf;
            }
            memcpy(dev->partial_write_buffer, kernel_buf, count);
            dev->partial_write_size = count;
            retval = count;
            goto free_kernel_buf;
        }
    }
    
    if (complete_buffer) {
        new_entry.buffptr = complete_buffer;
        new_entry.size = complete_size;
        
        // If buffer is full, free the entry at in_offs (which will be overwritten)
        if (dev->circular_buffer.full) {
            old_entry = &dev->circular_buffer.entry[dev->circular_buffer.in_offs];
            if (old_entry->buffptr) {
                kfree(old_entry->buffptr);
            }
        }
        
        aesd_circular_buffer_add_entry(&dev->circular_buffer, &new_entry);
        retval = count;
    }
    
free_kernel_buf:
    kfree(kernel_buf);
out:
    mutex_unlock(&dev->lock);
    return retval;
}

/**
 * Helper function to calculate total size of all entries in circular buffer
 */
static size_t aesd_circular_buffer_total_size(struct aesd_circular_buffer *buffer)
{
    size_t total_size = 0;
    uint8_t index = buffer->out_offs;
    uint8_t count = 0;
    
    // Handle empty buffer case
    if (!buffer->full && buffer->in_offs == buffer->out_offs) {
        return 0;
    }
    
    // Calculate number of entries to check
    uint8_t entries_to_check = buffer->full ? 
                              AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED :
                              (buffer->in_offs >= buffer->out_offs) ?
                                  buffer->in_offs - buffer->out_offs :
                                  buffer->in_offs + AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - buffer->out_offs;
    
    // Sum up sizes of all entries
    while (count < entries_to_check) {
        total_size += buffer->entry[index].size;
        index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        count++;
    }
    
    return total_size;
}

/**
 * Helper function to calculate byte offset for a specific write command and offset within it
 */
static loff_t aesd_calculate_seek_offset(struct aesd_circular_buffer *buffer, 
                                         uint32_t write_cmd, 
                                         uint32_t write_cmd_offset)
{
    size_t total_offset = 0;
    uint8_t index = buffer->out_offs;
    uint8_t count = 0;
    
    // Handle empty buffer case
    if (!buffer->full && buffer->in_offs == buffer->out_offs) {
        return -EINVAL;
    }
    
    // Calculate number of entries to check
    uint8_t entries_to_check = buffer->full ? 
                              AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED :
                              (buffer->in_offs >= buffer->out_offs) ?
                                  buffer->in_offs - buffer->out_offs :
                                  buffer->in_offs + AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - buffer->out_offs;
    
    // Check if write_cmd is out of range
    if (write_cmd >= entries_to_check) {
        return -EINVAL;
    }
    
    // Iterate to the specified write command
    while (count < write_cmd) {
        total_offset += buffer->entry[index].size;
        index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        count++;
    }
    
    // Check if write_cmd_offset is out of range for this command
    if (write_cmd_offset >= buffer->entry[index].size) {
        return -EINVAL;
    }
    
    return total_offset + write_cmd_offset;
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t new_pos = 0;
    size_t total_size = 0;
    
    PDEBUG("llseek offset %lld whence %d", offset, whence);
    
    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;
    
    total_size = aesd_circular_buffer_total_size(&dev->circular_buffer);
    
    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = filp->f_pos + offset;
            break;
        case SEEK_END:
            new_pos = total_size + offset;
            break;
        default:
            mutex_unlock(&dev->lock);
            return -EINVAL;
    }
    
    // Validate the new position
    if (new_pos < 0) {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }
    
    if (new_pos > total_size) {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }
    
    filp->f_pos = new_pos;
    
    mutex_unlock(&dev->lock);
    return new_pos;
}

long aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_seekto seekto;
    loff_t new_pos;
    uint8_t entries_to_check;
    
    PDEBUG("ioctl cmd %u arg %lu", cmd, arg);
    
    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) {
        return -ENOTTY;
    }
    if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR) {
        return -ENOTTY;
    }
    
    if (cmd == AESDCHAR_IOCSEEKTO) {
        if (mutex_lock_interruptible(&dev->lock))
            return -ERESTARTSYS;
        
        // Copy structure from user space
        if (copy_from_user(&seekto, (struct aesd_seekto __user *)arg, sizeof(seekto))) {
            mutex_unlock(&dev->lock);
            return -EFAULT;
        }
        
        // Calculate number of entries currently in buffer
        if (!dev->circular_buffer.full && 
            dev->circular_buffer.in_offs == dev->circular_buffer.out_offs) {
            mutex_unlock(&dev->lock);
            return -EINVAL;
        }
        
        entries_to_check = dev->circular_buffer.full ? 
                          AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED :
                          (dev->circular_buffer.in_offs >= dev->circular_buffer.out_offs) ?
                              dev->circular_buffer.in_offs - dev->circular_buffer.out_offs :
                              dev->circular_buffer.in_offs + AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - dev->circular_buffer.out_offs;
        
        // Validate write_cmd
        if (seekto.write_cmd >= entries_to_check) {
            mutex_unlock(&dev->lock);
            return -EINVAL;
        }
        
        // Calculate the new file position
        new_pos = aesd_calculate_seek_offset(&dev->circular_buffer, 
                                            seekto.write_cmd, 
                                            seekto.write_cmd_offset);
        
        if (new_pos < 0) {
            mutex_unlock(&dev->lock);
            return new_pos;
        }
        
        // Update file position
        filp->f_pos = new_pos;
        
        mutex_unlock(&dev->lock);
        return 0;
    }
    
    return -ENOTTY;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =   aesd_llseek,
    .unlocked_ioctl = aesd_unlocked_ioctl,
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

    aesd_circular_buffer_init(&aesd_device.circular_buffer);
    mutex_init(&aesd_device.lock);
    aesd_device.partial_write_buffer = NULL;
    aesd_device.partial_write_size = 0;

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

    mutex_lock(&aesd_device.lock);
    
    uint8_t index;
    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.circular_buffer, index) {
        if (entry->buffptr) {
            kfree(entry->buffptr);
        }
    }
    
    if (aesd_device.partial_write_buffer) {
        kfree(aesd_device.partial_write_buffer);
        aesd_device.partial_write_buffer = NULL;
    }
    
    mutex_unlock(&aesd_device.lock);
    mutex_destroy(&aesd_device.lock);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
