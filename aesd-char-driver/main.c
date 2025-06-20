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
#include "aesd_ioctl.h"

#include <linux/uaccess.h>
#include <linux/fs.h> // file_operations
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/types.h>

MODULE_AUTHOR("AJ Donich");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;
size_t append_offset;

int aesd_open(struct inode *inode, struct file *filp) {
    PDEBUG("aesd_open mode %u, offset %lld, flags %u", filp->f_mode, filp->f_pos, filp->f_flags);
	filp->private_data = (void *)&aesd_device; // Just for good measure; using aesd_device handle above

    if (filp->f_mode & FMODE_READ) PDEBUG("  FMODE_READ");
    if (filp->f_mode & FMODE_WRITE) PDEBUG("  FMODE_WRITE");
    if (filp->f_mode & FMODE_LSEEK) PDEBUG("  FMODE_LSEEK");
    if (filp->f_mode & FMODE_PREAD) PDEBUG("  FMODE_PREAD");
    if (filp->f_mode & FMODE_PWRITE) PDEBUG("  FMODE_PWRITE");
    if (filp->f_mode & FMODE_EXEC) PDEBUG("  FMODE_EXEC");

    if (filp->f_flags & O_CREAT) PDEBUG("  O_CREAT");
    if (filp->f_flags & O_TRUNC) PDEBUG("  O_TRUNC");
    if (filp->f_flags & O_EXCL) PDEBUG("  O_EXCL");
    if (filp->f_flags & O_NOCTTY) PDEBUG("  O_NOCTTY");
    if (filp->f_flags & O_NONBLOCK) PDEBUG("  O_NONBLOCK");
    if (filp->f_flags & O_APPEND) PDEBUG("  O_APPEND");
    if (filp->f_flags & O_DSYNC) PDEBUG("  O_DSYNC");
    if (filp->f_flags & O_DIRECTORY) PDEBUG("  O_DIRECTORY");
    if (filp->f_flags & O_NOFOLLOW) PDEBUG("  O_NOFOLLOW");
    if (filp->f_flags & O_LARGEFILE) PDEBUG("  O_LARGEFILE");
    if (filp->f_flags & O_DIRECT) PDEBUG("  O_DIRECT");
    if (filp->f_flags & O_NOATIME) PDEBUG("  O_NOATIME");
    if (filp->f_flags & O_CLOEXEC) PDEBUG("  O_CLOEXEC");
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp) {
    PDEBUG("aesd_release mode %u, offset %lld, flags %u", filp->f_mode, filp->f_pos, filp->f_flags);
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
    size_t char_offset, nread, ncopy;
    char *output = (char *)kmalloc(count, GFP_KERNEL);
    if (output == NULL) {
        printk(KERN_ERR "[ENOMEM] in aesd_read::kmalloc\n");
        return -ENOMEM;
    }

    if (mutex_lock_interruptible(&aesd_device.lock) < 0) {
        printk(KERN_WARNING "[EINTR] in aesd_read::mutex_lock_interruptible\n");
        kfree((void *)output);
        return -EINTR;
    }

    char_offset = (size_t)*f_pos;
    nread = _read_count_for_fpos(&aesd_device.cbuffer, output, count, char_offset);
    mutex_unlock(&aesd_device.lock);

    ncopy = nread - __copy_to_user((void __user *)buf, (const void *)output, nread);
    PDEBUG("read %zu of %zu bytes at offset %lld in aesd_read::_read_count_for_fpos\n", nread, count, *f_pos);
    PDEBUG("copy %zu of %zu bytes in aesd_read::__copy_to_user\n", ncopy, nread);

    *f_pos += ncopy;
    kfree((void *)output);
    return (ssize_t)ncopy;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    size_t entry_offset, ncopy, newsz;
    struct aesd_buffer_entry *entry;
    char *inbuffer;
    
    if (count == 0) return 0;
    if ((inbuffer = (char *)kmalloc(count, GFP_KERNEL)) == NULL) {
        printk(KERN_ERR "[ENOMEM] in aesd_write::kmalloc\n");
        return -ENOMEM;
    }

    ncopy = count - __copy_from_user((void *)inbuffer, (const void __user *)buf, count);
    PDEBUG("copy %zu of %zu bytes in aesd_write::__copy_from_user\n", ncopy, count);

    if (mutex_lock_interruptible(&aesd_device.lock) < 0) {
        printk(KERN_WARNING "[EINTR] in aesd_write::mutex_lock_interruptible\n");
        kfree((void *)inbuffer);
        return -EINTR;
    }

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(
        &aesd_device.cbuffer, append_offset, &entry_offset);

    if (entry) {
        newsz = entry_offset + ncopy;
        if (krealloc((void *)entry->buffptr, newsz + 1, GFP_KERNEL) == NULL) {
            printk(KERN_ERR "[ENOMEM] in aesd_write::krealloc\n");
            mutex_unlock(&aesd_device.lock);
            kfree((void *)inbuffer);
            return -ENOMEM;
        }

        memset((void *)&entry->buffptr[entry_offset], 0, ncopy+1); // Ensures '\0' term
        memcpy((void *)&entry->buffptr[entry_offset], (void *)inbuffer, ncopy);
        entry->size = newsz;
    }
    else {
        struct aesd_buffer_entry newentry = { .buffptr = inbuffer, .size = ncopy };
        append_offset -= aesd_circular_buffer_add_entry(&aesd_device.cbuffer, &newentry);
    }

    append_offset += ncopy;
    mutex_unlock(&aesd_device.lock);
    PDEBUG("wrote %zu of %zu bytes at offset %zu in aesd_write\n", ncopy, count, append_offset);
    
    *f_pos += ncopy;
    kfree((void *)inbuffer);
    return (ssize_t)ncopy;
}

loff_t aesd_lseek(struct file *filp, loff_t off, int whence) {
	loff_t newpos;

    // Lock read and write of f_pos to guarantee atomic update
    if (mutex_lock_interruptible(&aesd_device.lock) < 0) {
        printk(KERN_WARNING "[EINTR] in aesd_lseek::mutex_lock_interruptible\n");
        return -EINTR;
    }

	switch(whence) {
    case 0: /* SEEK_SET */
        newpos = off;
        break;

    case 1: /* SEEK_CUR */
        newpos = filp->f_pos + off;
        break;

    case 2: /* SEEK_END */
        newpos = append_offset + off;
        break;

    default: /* can't happen from lseek(2) */
        mutex_unlock(&aesd_device.lock);
        return -EINVAL;
	}

	if (newpos < 0) {
        mutex_unlock(&aesd_device.lock);
        printk(KERN_ERR "[EINVAL] in aesd_lseek, whence: %i, loff: %lli\n", whence, off);
        return -EINVAL;
    }

	filp->f_pos = newpos;
    mutex_unlock(&aesd_device.lock);
    PDEBUG("set f_pos: %lli in aesd_lseek\n", filp->f_pos);
    return newpos;


}

// Read ioctl command from user space and apply the requested lpos offset
long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    struct aesd_seekto seekobj;
    loff_t offset;

	// Validate cmd is the one we recognize
	if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC || _IOC_NR(cmd) > AESDCHAR_IOC_MAXNR || cmd != AESDCHAR_IOCSEEKTO) {
        printk(KERN_ERR "[ENOTTY] in aesd_ioctl\n");
        return -ENOTTY;
    }
    else if (__copy_from_user((void *)&seekobj, (const void __user *)arg, sizeof(struct aesd_seekto)) != 0) {
        printk(KERN_ERR "[EFAULT] in aesd_ioctl::__copy_from_user\n");
        return -EFAULT; 
    }
    else if ((offset = _get_loffset(&aesd_device.cbuffer, seekobj.write_cmd, seekobj.write_cmd_offset)) == -1) {
        printk(KERN_ERR "[EINVAL] in aesd_ioctl::_set_loffset\n");
        return -EINVAL;
    }
    else if ((offset = aesd_lseek(filp, offset, SEEK_SET)) < 0) return offset;
    PDEBUG("set entry_offset: %u, char_offset: %u in aesd_ioctl\n", seekobj.write_cmd, seekobj.write_cmd_offset);
	return 0;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .llseek  =  aesd_lseek,
    .unlocked_ioctl = aesd_ioctl,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct cdev *cdev, dev_t devno) {
    int err;
    cdev->owner = THIS_MODULE;
    cdev_init(cdev, &aesd_fops);

    // Device goes live in cdev_add call
    if ((err = cdev_add(cdev, devno, 1)) < 0) {
        printk(KERN_ERR "[errno %i] in aesd_setup_cdev::cdev_add\n", -err);
    }
    return err;
}

int aesd_init_module(void) {
    int err;
    dev_t devno = 0;

    // Allocate one devno (dynamic major, minor starts at 0)
    if ((err = alloc_chrdev_region(&devno, 0, 1, "aesdchar")) < 0) {
        printk(KERN_ERR "[errno %i] in aesd_init_module::alloc_chrdev_region\n", -err);
        return err;
    }
    
    append_offset = 0;
    memset(&aesd_device, 0, sizeof(struct aesd_dev)); // Includes init of aesd_device.cbuffer 
    if( (err = aesd_setup_cdev(&aesd_device.cdev, devno)) < 0 ) {
        unregister_chrdev_region(devno, 1);
        return err;
    }

    printk(KERN_NOTICE "aesdchar registered at %x (%i, %i)\n", devno, MAJOR(devno), MINOR(devno));
    return 0;
}

void aesd_cleanup_module(void) {
    dev_t devno = aesd_device.cdev.dev;
    cdev_del(&aesd_device.cdev);
    unregister_chrdev_region(devno, 1);
    aesd_circular_buffer_cleanup(&aesd_device.cbuffer);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
