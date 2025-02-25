#include <linux/module.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#include "scull.h"

struct scull_pipe {
        wait_queue_head_t inq, outq;       /* read and write queues */
        char *start, *end;                 /* begin of buf, end of buf */
        int buffersize;                    /* used in pointer arithmetic */
        char *rp, *wp;                     /* where to read, where to write */
        int nreaders, nwriters;            /* number of openings for r/w */
        struct fasync_struct *async_queue; /* asynchronous readers */
        struct semaphore sem;              /* mutual exclusion semaphore */
        struct cdev cdev;                  /* Char device structure */
};


static dev_t scull_p_dev_num;
static int scull_p_nr_devs     = SCULL_P_NR_DEVS;      /*number of pipe devices*/
static int scull_p_buffer_size = SCULL_P_BUFFER_SIZE;  /*pipe buffer size*/
static int scull_p_major       = SCULL_MAJOR;
static int scull_p_minor       = SCULL_MINOR;


static struct scull_pipe *scull_p_devices = NULL;

module_param(scull_p_major, int, S_IRUGO);
module_param(scull_p_nr_devs, int, S_IRUGO);
module_param(scull_p_buffer_size, int, S_IRUGO);


static int scull_p_fasync(int fd, struct file *filp, int mode);
static int spacefree(struct scull_pipe *dev);


static int scull_p_open(struct inode *inode, struct file *filp){
    struct scull_pipe *dev = NULL;
    dev = container_of(inode->i_cdev, struct scull_pipe, cdev);
    filp->private_data = dev;

    //获取信号量
    if(down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    //buffer此时还没有分配空间
    if(!dev->start){
        dev->start = kmalloc(scull_p_buffer_size, GFP_KERNEL);
        //分配失败
        if(!dev->start){
            up(&dev->sem);
            return -ENOMEM;
        }
    }

    //初始化
    dev->buffersize = scull_p_buffer_size;
    dev->end = dev->start + scull_p_buffer_size;
    dev->rp = dev->wp = dev->start;

    if(filp->f_mode & FMODE_READ)
        dev->nreaders++;

    if(filp->f_mode & FMODE_WRITE)
        dev->nwriters++;

    up(&dev->sem);

    return nonseekable_open(inode, filp);
}

static int scull_p_release(struct inode *inode, struct file *filp){
    struct scull_pipe *dev = filp->private_data;

    scull_p_fasync(-1, filp, 0);
    down(&dev->sem);

    if(filp->f_mode & FMODE_READ)
        dev->nreaders--;

    if(filp->f_mode & FMODE_WRITE)
        dev->nwriters--;

    if(dev->nreaders + dev->nwriters == 0){
        kfree(dev->start);
        dev->start = NULL;
    }

    up(&dev->sem);
    return 0;
}

static ssize_t scull_p_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
    struct scull_pipe *dev = filp->private_data;

    if(down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    //此时数据为空，没有数据可读
    while(dev->rp == dev->wp){
        up(&dev->sem); //释放信号量
        
        //设置了非阻塞IO,此时又无数据可读，直接返回
        if(filp->f_flags & O_NONBLOCK)
            return -EAGAIN;

        PDEBUG("[%s] reading: going to sleep\n", current->comm);

        //休眠当前进程，直到有数据可以读再被唤醒
        if(wait_event_interruptible(dev->inq, (dev->rp != dev->wp)))
            return -ERESTARTSYS;

        //此时进程已经被唤醒了，准备读取数据，但是首先需要先获取到信号量
        if(down_interruptible(&dev->sem))
            return -ERESTARTSYS;
    }

    //此时有数据可读
    if(dev->wp > dev->rp)
        count = min(count, (size_t)(dev->wp - dev->rp));
    else
        count = min(count, (size_t)(dev->end - dev->rp));

    //把数据拷贝到用户态指针所指向的内存
    if(copy_to_user(buf, dev->rp, count)){
        up(&dev->sem);
        return -EFAULT;
    }

    dev->rp += count;
    if(dev->rp == dev->end)
        dev->rp = dev->start;

    up(&dev->sem);
    //唤醒write进程，此时已经有多余的空间可以写入数据了
    wake_up_interruptible(&dev->outq);
    PDEBUG("[%s] did read %li bytes\n",current->comm, (long)count);
	return count;
}

static int get_writespace(struct scull_pipe *dev, struct file *filp){
    while(spacefree(dev) == 0){
        DEFINE_WAIT(wait);

        up(&dev->sem);
        if(filp->f_flags & O_NONBLOCK)
            return -EAGAIN;

        PDEBUG("[%s] writing: going to sleep\n",current->comm);
        //1.准备休眠
        prepare_to_wait(&dev->outq, &wait, TASK_INTERRUPTIBLE);

        //2.再次检查条件，如果还是没有空间，就让出CPU进入休眠
        if(spacefree(dev) == 0)
            schedule();
        
        //休眠完成，从休眠状态转变为调度状态
        finish_wait(&dev->outq, &wait);

        /*
            signal_pending用于判断当前进程是否还有未处理的信号，如果有的话这里直接返回先处理信号
            而不是继续执行进程
        */
        if(signal_pending(current))
            return -ERESTARTSYS;

        if(down_interruptible(&dev->sem))
            return -ERESTARTSYS;
    }

    return 0;
}

static int spacefree(struct scull_pipe *dev){
    if(dev->rp == dev->wp)
        return dev->buffersize - 1;

    return ((dev->rp + dev->buffersize - dev->wp) % dev->buffersize) - 1;
}

static ssize_t scull_p_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    struct scull_pipe *dev = filp->private_data;
    if(down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    int res = get_writespace(dev, filp);
    if(res)
        return res;

    count = min(count, (size_t)spacefree(dev));

    if(dev->wp >= dev->rp)
        count = min(count, (size_t)(dev->end - dev->wp));
    else
        count = min(count, (size_t)(dev->rp - dev->wp - 1));
    

    PDEBUG("Going to accept %li bytes to %p from %p\n", (long)count, dev->wp, buf);
	if (copy_from_user(dev->wp, buf, count)) {
		up (&dev->sem);
		return -EFAULT;
	}

    dev->wp += count;
    if(dev->wp == dev->end) dev->wp = dev->start;

    up(&dev->sem);

    wake_up_interruptible(&dev->inq);

    if(dev->async_queue)
        kill_fasync(&dev->async_queue, SIGIO, POLL_IN);

    PDEBUG("[%s] did write %li bytes\n",current->comm, (long)count);
	return count;
}


static int scull_p_fasync(int fd, struct file *filp, int mode)
{
	struct scull_pipe *dev = filp->private_data;

	return fasync_helper(fd, filp, mode, &dev->async_queue);
}


struct file_operations scull_pipe_fops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.read =		scull_p_read,
	.write =	scull_p_write,
	.open =		scull_p_open,
	.release =	scull_p_release,
	.fasync =	scull_p_fasync
};


static void scull_setup_cdev(struct scull_pipe *dev, int index) {
    int err, devno = MKDEV(scull_p_major, scull_p_minor + index);
    cdev_init(&dev->cdev, &scull_pipe_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &scull_pipe_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_NOTICE "failed to adding scull_pipe%d, err is %d!\n", index, err);
    }
}

static int scull_chrdev_register(void) {
    int ret;
    if (scull_p_major) {
        scull_p_dev_num = MKDEV(scull_p_major, scull_p_minor);
        ret = register_chrdev_region(scull_p_dev_num, scull_p_nr_devs, "scull_pipe");
    } 
    else {
        ret = alloc_chrdev_region(&scull_p_dev_num, scull_p_minor, scull_p_nr_devs, "scull_pipe");
        scull_p_major = MAJOR(scull_p_dev_num);
    }

    return ret;
}

static int __init scull_p_init(void) {
    int ret = scull_chrdev_register();
    if (ret) {
        printk(KERN_ERR "[%s] failed to scull_chrdev_register, ret is %d!\n", __func__, ret);
        goto err;
    }

    printk(KERN_INFO "[%s] Get dev major number is %d\n", __func__, scull_p_major);

    scull_p_devices = kmalloc(sizeof(struct scull_pipe) * scull_p_nr_devs, GFP_KERNEL);
    if (!scull_p_devices) {
        ret = -ENOMEM;
        goto err;
    }

    memset(scull_p_devices, 0, sizeof(struct scull_pipe) * scull_p_nr_devs);
    for(int i = 0; i < scull_p_nr_devs; ++i){
        init_waitqueue_head(&(scull_p_devices[i].inq));
		init_waitqueue_head(&(scull_p_devices[i].outq));
        sema_init(&scull_p_devices[i].sem, 1);
        scull_setup_cdev(scull_p_devices + i, i);
    }

    printk(KERN_INFO "[%s] scull_pipe init success!\n", __func__);

err:
    return ret;
}


static void __exit scull_p_exit(void) {
    if (scull_p_devices) {
        for (int i = 0; i < scull_p_nr_devs; ++i) {
            cdev_del(&scull_p_devices[i].cdev);
            kfree(scull_p_devices[i].start);
        }

        kfree(scull_p_devices);
        scull_p_devices = NULL;
    }

    unregister_chrdev_region(scull_p_dev_num, scull_p_nr_devs);
    printk(KERN_INFO "[%s] scull exit success!\n", __func__);
}

module_init(scull_p_init);
module_exit(scull_p_exit);

MODULE_LICENSE("GPL");
MODULE_VERSION("v1.0");
MODULE_AUTHOR("syrdy");
MODULE_DESCRIPTION("A Simple Character Pipe Device Driver Demo");