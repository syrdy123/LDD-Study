#include <linux/module.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/ioctl.h>

#include "scull.h"

static dev_t dev_num;
static int scull_major   = SCULL_MAJOR;
static int scull_minor   = SCULL_MINOR;
static int scull_nr_devs = SCULL_NR_DEVS;
static int scull_quantum = SCULL_QUANTUM;
static int scull_qset    = SCULL_QSET;

static struct scull_dev *sculldev = NULL;

module_param(scull_major, int, S_IRUGO);
module_param(scull_nr_devs, int, S_IRUGO);
module_param(scull_quantum, int, S_IRUGO);
module_param(scull_qset, int, S_IRUGO);

static int scull_trim(struct scull_dev *dev) {
    struct scull_qset_t *ptr, *next;
    int qset = dev->qset;

    for (ptr = dev->data; ptr; ) {
        if (ptr->data) {
            for (int i = 0; i < qset; ++i) {
                kfree(ptr->data[i]);
            }
            kfree(ptr->data);
            ptr->data = NULL;
        }
        next = ptr->next;
        kfree(ptr);
        ptr = next;
    }

    dev->size = 0;
    dev->quantum = scull_quantum;
    dev->qset = scull_qset;
    dev->data = NULL;

    return 0;
}

static struct scull_qset_t *scull_follow(struct scull_dev *dev, int index) {
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

static ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
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

    ptr = scull_follow(dev, item);

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

static ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
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

    ptr = scull_follow(dev, item);

    if (NULL == ptr)
        goto out;

    if (!ptr->data) {
        ptr->data = kmalloc(sizeof(char*) * qset, GFP_KERNEL);
        if (!ptr->data)
            goto out;
        memset(ptr->data, 0, sizeof(char*) * qset);
    }

    if (!ptr->data[s_pos]) {
        ptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
        if (!ptr->data[s_pos])
            goto out;
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

static long int scull_ioctl(struct file *filp, unsigned int cmd, long unsigned int arg){
    int err = 0, tmp;
    int ret;

    if(_IOC_TYPE(cmd) != SCULL_IOC_MAGIC) return -ENOTTY;
    if(_IOC_NR(cmd) > SCULL_IOC_MAXNR) return -ENOTTY;

    if(_IOC_DIR(cmd) & _IOC_READ){
        err = !access_ok((void __user*)arg, _IOC_SIZE(cmd));
    }
    else if(_IOC_DIR(cmd) & _IOC_WRITE){
        err = !access_ok((void __user*)arg, _IOC_SIZE(cmd));
    }

    if(err)
        return -EFAULT;

    switch(cmd){
        case SCULL_IOCRESET:
            scull_quantum = SCULL_QUANTUM;
            scull_qset = SCULL_QSET;
            break;
        
        case SCULL_IOCSQUANTUM:
            if(!capable(CAP_SYS_ADMIN))
                return -EPERM;
            ret = get_user(scull_quantum, (int __user *)arg);
            printk(KERN_DEBUG "[SCULL_IOCSQUANTUM] scull_quantum is %d\n", scull_quantum);
            break;

        case SCULL_IOCSQSET:
            if(!capable(CAP_SYS_ADMIN))
                return -EPERM;
            ret = get_user(scull_qset, (int __user *)arg);
            break;

        case SCULL_IOCTQUANTUM:
            if(!capable(CAP_SYS_ADMIN))
                return -EPERM;
            scull_quantum = arg;
            printk(KERN_DEBUG "[SCULL_IOCTQUANTUM] scull_quantum is %d\n", scull_quantum);
            break;
        
        case SCULL_IOCTQSET:
            if(!capable(CAP_SYS_ADMIN))
                return -EPERM;
            scull_qset = arg;
            break;

        case SCULL_IOCGQUANTUM:
            ret = put_user(scull_quantum, (int __user *)arg);
            printk(KERN_DEBUG "[SCULL_IOCGQUANTUM] scull_quantum is %d\n", scull_quantum);
            break;

        case SCULL_IOCGQSET:
            ret = put_user(scull_qset, (int __user *)arg);
            break;

        case SCULL_IOCQQUANTUM:
            printk(KERN_DEBUG "[SCULL_IOCQQUANTUM] scull_quantum is %d\n", scull_quantum);
            return scull_quantum;
        
        case SCULL_IOCQQSET:
            return scull_qset;
        
        case SCULL_IOCXQUANTUM:
            if(!capable(CAP_SYS_ADMIN))
                return -EPERM;
            tmp = scull_quantum;
            ret = get_user(scull_quantum, (int __user *)arg);
            if(0 == ret)
                ret = put_user(tmp, (int __user *)arg);
            printk(KERN_DEBUG "[SCULL_IOCXQUANTUM] scull_quantum is %d, tmp is %d\n", scull_quantum, tmp);
            break;
        
        case SCULL_IOCXQSET:
            if(!capable(CAP_SYS_ADMIN))
                return -EPERM;
            tmp = scull_qset;
            ret = get_user(scull_qset, (int __user *)arg);
            if(0 == ret)
                ret = put_user(tmp, (int __user *)arg);
            break;

        case SCULL_IOCHQUANTUM:
            if(!capable(CAP_SYS_ADMIN))
                return -EPERM;
            tmp = scull_quantum;
            scull_quantum = arg;
            printk(KERN_DEBUG "[SCULL_IOCHQUANTUM] scull_quantum is %d, tmp is %d\n", scull_quantum, tmp);
            return tmp;
        
        case SCULL_IOCHQSET:
            if(!capable(CAP_SYS_ADMIN))
                return -EPERM;
            tmp = scull_qset;
            scull_qset = arg;
            return tmp;

        default:
            return -ENOTTY;
    }

    return ret;
}

static int scull_open(struct inode *node, struct file *filp) {
    struct scull_dev *dev = NULL;
    dev = container_of(node->i_cdev, struct scull_dev, cdev);
    filp->private_data = dev;

    if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
        scull_trim(dev);
        dev->data = kmalloc(sizeof(struct scull_qset_t), GFP_KERNEL);
        if (!dev->data)
            return -ENOMEM;
        memset(dev->data, 0, sizeof(struct scull_qset_t));
    }

    return 0;
}

static int scull_release(struct inode *node, struct file *filp) {
    return 0;
}

static struct file_operations scull_fops = {
    .owner   = THIS_MODULE,
    .read    = scull_read,
    .write   = scull_write,
    .unlocked_ioctl   = scull_ioctl,
    .open    = scull_open,
    .release = scull_release
};

static void scull_setup_cdev(struct scull_dev *dev, int index) {
    int err, devno = MKDEV(scull_major, scull_minor + index);
    cdev_init(&dev->cdev, &scull_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &scull_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_NOTICE "failed to adding scull%d, err is %d!\n", index, err);
    }
}

static int scull_chrdev_register(void) {
    int ret;
    if (scull_major) {
        dev_num = MKDEV(scull_major, scull_minor);
        ret = register_chrdev_region(dev_num, scull_nr_devs, "scull_ioctl");
    } else {
        ret = alloc_chrdev_region(&dev_num, scull_minor, scull_nr_devs, "scull_ioctl");
        scull_major = MAJOR(dev_num);
    }
    return ret;
}

static void __exit scull_exit(void) {
    if (sculldev) {
        for (int i = 0; i < scull_nr_devs; ++i) {
            scull_trim(&sculldev[i]);
            cdev_del(&sculldev[i].cdev);
        }
        kfree(sculldev);
    }
    unregister_chrdev_region(dev_num, scull_nr_devs);
    printk(KERN_INFO "scull exit success!\n");
}

static int __init scull_init(void) {
    int ret = scull_chrdev_register();
    if (ret) {
        printk(KERN_ERR "failed to scull_chrdev_register, ret is %d!\n", ret);
    }

    printk(KERN_INFO "[%s] Get dev major number is %d\n", __func__, scull_major);

    sculldev = kmalloc(sizeof(struct scull_dev) * scull_nr_devs, GFP_KERNEL);
    if (!sculldev) {
        ret = -ENOMEM;
        goto err;
    }

    memset(sculldev, 0, sizeof(struct scull_dev) * scull_nr_devs);
    for (int i = 0; i < scull_nr_devs; ++i) {
        sculldev[i].quantum = scull_quantum;
        sculldev[i].qset = scull_qset;
        sculldev[i].size = 0;
        sculldev[i].access_key = 0;
        sema_init(&sculldev[i].sem, 1);
        scull_setup_cdev(&sculldev[i], i);
    }

    printk(KERN_INFO "scull init success!\n");

err:
    if (ret) {
        printk(KERN_INFO "scull init failed, no memory!\n");
        scull_exit();
    }
    return ret;
}

module_init(scull_init);
module_exit(scull_exit);

MODULE_LICENSE("GPL");
MODULE_VERSION("v1.0");
MODULE_AUTHOR("syrdy");
MODULE_DESCRIPTION("A Simple Character Device Driver Demo");