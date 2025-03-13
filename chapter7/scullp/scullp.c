#include <linux/module.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#include "scull.h"

static dev_t dev_num;
static int scullp_order   = SCULL_ORDER;
static int scullp_major   = SCULL_MAJOR;
static int scullp_minor   = SCULL_MINOR;
static int scullp_nr_devs = SCULL_NR_DEVS;
static int scullp_quantum = SCULL_QUANTUM;
static int scullp_qset    = SCULL_QSET;

static struct scull_dev *sculldev = NULL;

module_param(scullp_order, int, S_IRUGO);
module_param(scullp_major, int, S_IRUGO);
module_param(scullp_nr_devs, int, S_IRUGO);
module_param(scullp_quantum, int, S_IRUGO);
module_param(scullp_qset, int, S_IRUGO);

static int scullp_trim(struct scull_dev *dev) {
    struct scull_qset_t *ptr, *next;
    int qset = dev->qset;

    for (ptr = dev->data; ptr; ) {
        if (ptr->data) {
            for (int i = 0; i < qset; ++i) {
                free_pages((unsigned long)ptr->data[i], dev->order);
            }
            kfree(ptr->data);
            ptr->data = NULL;
        }
        next = ptr->next;
        kfree(ptr);
        ptr = next;
    }

    dev->order = scullp_order;
    dev->size = 0;
    dev->quantum = scullp_quantum;
    dev->qset = scullp_qset;
    dev->data = NULL;

    return 0;
}

static struct scull_qset_t *scullp_follow(struct scull_dev *dev, int index) {
    struct scull_qset_t *ptr = dev->data;

    if (!ptr) {
        ptr = kmalloc(sizeof(struct scull_qset_t), GFP_KERNEL);
        if (!ptr)
            return NULL;
        memset(ptr, 0, sizeof(struct scull_qset_t));
        dev->data = ptr;
    }

    while (index--) {
        if (!ptr->next) {
            ptr->next = kmalloc(sizeof(struct scull_qset_t), GFP_KERNEL);
            if (!ptr->next)
                return NULL;
            memset(ptr->next, 0, sizeof(struct scull_qset_t));
        }
		
        ptr = ptr->next;
    }

    return ptr;
}

static ssize_t scullp_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
    struct scull_dev *dev = filp->private_data;
    struct scull_qset_t *ptr = NULL;
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset;
    int item, s_pos, q_pos, rest;
    ssize_t ret = 0;

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    if (*f_pos > dev->size)
        goto out;

    item = (long)*f_pos / itemsize;
    rest = (long)*f_pos % itemsize;
    s_pos = rest / quantum, q_pos = rest % quantum;

    ptr = scullp_follow(dev, item);

    if (NULL == ptr || !ptr->data || !ptr->data[s_pos])
        goto out;

    if (count > quantum - q_pos)
        count = quantum - q_pos;

    if (copy_to_user(buf, ptr->data[s_pos] + q_pos, count)) {
        ret = -EFAULT;
        goto out;
    }

    *f_pos += count;
    ret = count;
    printk(KERN_INFO "[%s] read data from kernel to user: count is %lu\n", __func__, (long unsigned int)count);

out:
    up(&dev->sem);
    return ret;
}

static ssize_t scullp_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    struct scull_dev *dev = filp->private_data;
    struct scull_qset_t *ptr = NULL;
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = qset * quantum;
    int item, rest, s_pos, q_pos;
    ssize_t ret = -ENOMEM;

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    item = (long)*f_pos / itemsize;
    rest = (long)*f_pos % itemsize;
    s_pos = rest / quantum, q_pos = rest % quantum;

    ptr = scullp_follow(dev, item);

    if (NULL == ptr)
        goto out;

    if (!ptr->data) {
        ptr->data = kmalloc(sizeof(char*) * qset, GFP_KERNEL);
        if (!ptr->data)
            goto out;
        memset(ptr->data, 0, sizeof(char*) * qset);
    }

    if (!ptr->data[s_pos]) {
        ptr->data[s_pos] = (void*)__get_free_pages(GFP_KERNEL, dev->order);
        if (!ptr->data[s_pos])
            goto out;

        memset(ptr->data[s_pos], 0, PAGE_SIZE << dev->order);
    }

    if (count > quantum - q_pos)
        count = quantum - q_pos;

    if (copy_from_user(ptr->data[s_pos] + q_pos, buf, count)) {
        ret = -EFAULT;
        goto out;
    }

    *f_pos += count;
    ret = count;

    if (*f_pos > dev->size)
        dev->size = *f_pos;

    printk(KERN_INFO "[%s] write data from user to kernel: count is %lu\n", __func__, (long unsigned int)count);

out:
    up(&dev->sem);
    return ret;
}


static loff_t scullp_llseek(struct file *filp, loff_t off, int whence)
{
    struct scull_dev *dev = filp->private_data;
    loff_t new_pos = 0;

    switch(whence) {
        case SEEK_SET: /*SEEK_SET = 0*/
            new_pos = off;
            break;

        case SEEK_CUR: /*SEEK_CUR = 1*/
            new_pos = filp->f_pos + off;
            break;

        case SEEK_END: /*SEEK_END = 2*/
            new_pos = dev->size + off;
            break;

        default:
            return -EINVAL;
    }

    if(new_pos < 0) 
        return -EINVAL;
    
    filp->f_pos = new_pos;
    return new_pos;
}

static int scullp_open(struct inode *node, struct file *filp) {
    struct scull_dev *dev = NULL;
    dev = container_of(node->i_cdev, struct scull_dev, cdev);
    filp->private_data = dev;

    if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
        scullp_trim(dev);
        dev->data = kmalloc(sizeof(struct scull_qset_t), GFP_KERNEL);
        if (!dev->data)
            return -ENOMEM;
        memset(dev->data, 0, sizeof(struct scull_qset_t));
    }

    return 0;
}

static int scullp_release(struct inode *node, struct file *filp) {
    return 0;
}

static struct file_operations scull_fops = {
    .owner   = THIS_MODULE,
    .read    = scullp_read,
    .write   = scullp_write,
    .open    = scullp_open,
    .release = scullp_release,
    .llseek  = scullp_llseek
};

static void scullp_setup_cdev(struct scull_dev *dev, int index) {
    int err, devno = MKDEV(scullp_major, scullp_minor + index);
    cdev_init(&dev->cdev, &scull_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &scull_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_NOTICE "failed to adding scull%d, err is %d!\n", index, err);
    }
}

static int scullp_chrdev_register(void) {
    int ret;
    if (scullp_major) {
        dev_num = MKDEV(scullp_major, scullp_minor);
        ret = register_chrdev_region(dev_num, scullp_nr_devs, "scullp");
    } 
    else {
        ret = alloc_chrdev_region(&dev_num, scullp_minor, scullp_nr_devs, "scullp");
        scullp_major = MAJOR(dev_num);
    }
    return ret;
}

static void __exit scullp_exit(void) {
    if (sculldev) {
        for (int i = 0; i < scullp_nr_devs; ++i) {
            scullp_trim(&sculldev[i]);
            cdev_del(&sculldev[i].cdev);
        }
        kfree(sculldev);
    }


    unregister_chrdev_region(dev_num, scullp_nr_devs);
    printk(KERN_INFO "scullp exit success!\n");
}

static int __init scullp_init(void) {
    int ret = scullp_chrdev_register();
    if (ret) {
        printk(KERN_ERR "failed to scull_chrdev_register, ret is %d!\n", ret);
    }

    printk(KERN_INFO "[%s] Get dev major number is %d\n", __func__, scullp_major);

    sculldev = kmalloc(sizeof(struct scull_dev) * scullp_nr_devs, GFP_KERNEL);
    if (!sculldev) {
        ret = -ENOMEM;
        goto err;
    }

    memset(sculldev, 0, sizeof(struct scull_dev) * scullp_nr_devs);
    for (int i = 0; i < scullp_nr_devs; ++i) {
        sculldev[i].quantum = scullp_quantum;
        sculldev[i].qset = scullp_qset;
        sculldev[i].size = 0;
        sculldev[i].access_key = 0;
        sema_init(&sculldev[i].sem, 1);
        scullp_setup_cdev(&sculldev[i], i);
    }

    printk(KERN_INFO "scullp init success!\n");

err:
    if (ret) {
        if (sculldev) {
            for (int i = 0; i < scullp_nr_devs; ++i) {
                scullp_trim(&sculldev[i]);
                cdev_del(&sculldev[i].cdev);
            }

            kfree(sculldev);
        }

        unregister_chrdev_region(dev_num, scullp_nr_devs);
        printk(KERN_INFO "scullp init failed, no memory!\n");
    }

    return ret;
}

module_init(scullp_init);
module_exit(scullp_exit);

MODULE_LICENSE("GPL");
MODULE_VERSION("v1.0");
MODULE_AUTHOR("dinggongwurusai");
MODULE_DESCRIPTION("A Simple Character Device Driver Demo By get_free_pages");