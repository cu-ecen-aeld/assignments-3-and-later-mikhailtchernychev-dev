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
#include <linux/uaccess.h>
#include <linux/slab.h> // For kvfree
#include <linux/mm.h>
#include "aesdchar.h"
#include "aesd_ioctl.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Your Name Here"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    filp->private_data =  container_of(inode->i_cdev, struct aesd_dev, cdev);
    PDEBUG("private data = %p", filp->private_data);
    PDEBUG("Offset = %d", filp->f_pos);
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    //module_put(THIS_MODULE);
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    struct aesd_dev * dev = filp->private_data;

    size_t entry_offset_byte_rtn;
    struct aesd_buffer_entry *current_entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &entry_offset_byte_rtn); 

    if(current_entry) {
        if(current_entry->size - entry_offset_byte_rtn <= count) {
            copy_to_user(buf, &current_entry->buffptr[entry_offset_byte_rtn], current_entry->size - entry_offset_byte_rtn);
            *f_pos += current_entry->size - entry_offset_byte_rtn;   
            retval =  current_entry->size - entry_offset_byte_rtn;
        } else {
            copy_to_user(buf, &current_entry->buffptr[entry_offset_byte_rtn], count);
            retval = count;
            *f_pos += count;
        }
    }


    PDEBUG("end read %zu bytes with offset %lld %lld",count,*f_pos, entry_offset_byte_rtn);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    struct aesd_dev * dev = filp->private_data;
    mutex_lock(&dev->access_lock);
    PDEBUG("write private data = %p in_offs=%d", dev, dev->buffer.in_offs);

    if(!dev->current_entry.buffptr) {
        dev->current_entry.buffptr = kmalloc(count, GFP_KERNEL);
        if(!dev->current_entry.buffptr) {
            goto end_write;
        }
        copy_from_user(dev->current_entry.buffptr, buf, count);
        dev->current_entry.size = count;
    } else { // already allocated, append
        char * tmp_buffer = kmalloc(count +  dev->current_entry.size, GFP_KERNEL);
        if(!tmp_buffer) {
             goto end_write;
        } 
        memcpy(tmp_buffer, dev->current_entry.buffptr, dev->current_entry.size);
        copy_from_user(&tmp_buffer[dev->current_entry.size], buf, count);
        dev->current_entry.size += count;
        kfree(dev->current_entry.buffptr);
        dev->current_entry.buffptr = tmp_buffer;
    }

    // if end command write to circlular buffer 
    if(dev->current_entry.buffptr[dev->current_entry.size-1] == '\n') {
        PDEBUG("Line feed detected");
        aesd_circular_buffer_add_entry(&dev->buffer, &dev->current_entry);
        dev->current_entry.buffptr = NULL;
        dev->current_entry.size = 0;
    }
    retval =  count;
    PDEBUG("count return %lld",count);

end_write:
    mutex_unlock(&dev->access_lock);
    return retval;
}

static loff_t aesd_llseek(struct file *filp, loff_t offset, int whence) {
    loff_t newpos;
    PDEBUG("llseek called");
    size_t entry_offset_byte_rtn;
    struct aesd_dev * dev = filp->private_data;

    switch(whence) {
        case SEEK_SET:
            newpos = offset;
            break;
        case SEEK_CUR:
            newpos = filp->f_pos + offset;
            break;
        case SEEK_END:
            PDEBUG("------------- SEEK_END");
            //newpos = MY_DEVICE_SIZE + offset;
            break;
        default:
            return -EINVAL;
    }

    struct aesd_buffer_entry *current_entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, newpos, &entry_offset_byte_rtn);
    if(!current_entry) {
        return -EINVAL;
    }

    filp->f_pos = newpos;
    PDEBUG("llseek new position %u", newpos);
    return newpos;
}

static long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    PDEBUG("called aesd_ioctl");
    switch (cmd) {
        struct aesd_seekto seekto;
        case AESDCHAR_IOCSEEKTO:
        if(copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)) !=0) {
            return EFAULT;
        } else {
            PDEBUG("aesd_ioctl: Offsets %d %d", seekto.write_cmd, seekto.write_cmd_offset);
            struct aesd_dev * dev = filp->private_data;
            size_t offset;
            if(!aesd_adjust_file_offset(&dev->buffer, seekto.write_cmd, seekto.write_cmd_offset, &offset)) {
                filp->f_pos = offset;
                PDEBUG("New offset %d", filp->f_pos);
            }
        }

        break;

        default:
        return -ENOTTY; 
    }
    return 0;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek  =  aesd_llseek,
    .unlocked_ioctl =  aesd_ioctl,
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
    PDEBUG("aesd_device = %p", &aesd_device);
    /**
     * TODO: initialize the AESD specific portion of the device
     */

    mutex_init(&aesd_device.access_lock);
    memset(&aesd_device.current_entry, 0, sizeof(struct aesd_buffer_entry));
    memset(&aesd_device.buffer, 0, sizeof(struct aesd_circular_buffer));

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

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */

    if(aesd_device.current_entry.buffptr) {
        kfree(aesd_device.current_entry.buffptr);
        aesd_device.current_entry.buffptr = NULL;
        PDEBUG("Freed aesd_device.current_entry.buffptr");
    }
    int i;
    for(i=0; i<AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
        if(aesd_device.buffer.entry[i].buffptr) {
            kfree(aesd_device.buffer.entry[i].buffptr);
            aesd_device.buffer.entry[i].buffptr = NULL;
            PDEBUG("Freed circle buffer entry %d", i);
        }
    }


    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
