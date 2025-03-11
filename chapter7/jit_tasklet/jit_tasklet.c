#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>

// 添加函数声明
static int __init jit_tasklet_init(void);
static void __exit jit_tasklet_cleanup(void);

#define JIT_ASYNC_LOOPS 5

// 定义一个包含所需参数的结构
struct jit_tasklet_data {
    struct tasklet_struct tasklet;
    wait_queue_head_t wait;
    unsigned long prev_jiffies;
    struct seq_file *m;
    int loops;
    int hi;  // 是否为高优先级tasklet
};

// 全局参数
static int delay = 10;
module_param(delay, int, 0644);

static void jit_tasklet_fn(unsigned long arg)
{
    struct jit_tasklet_data *data = (struct jit_tasklet_data *)arg;
    unsigned long cur_jiffies = jiffies;

    seq_printf(data->m, "%9lu  %3lu     %i    %6i   %i   %s\n",
            cur_jiffies, cur_jiffies - data->prev_jiffies,
            in_interrupt() ? 1 : 0,
            current->pid, smp_processor_id(), current->comm);

    if (--data->loops) {
        data->prev_jiffies = cur_jiffies;
        if (data->hi)
            tasklet_hi_schedule(&data->tasklet);
        else
            tasklet_schedule(&data->tasklet);
    } else {
        wake_up_interruptible(&data->wait);
    }
}

static int jit_tasklet_show(struct seq_file *m, void *v)
{
    struct jit_tasklet_data *data;
    unsigned long cur_jiffies;

    // 分配数据结构
    data = kmalloc(sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    // 初始化数据
    cur_jiffies = jiffies;
    init_waitqueue_head(&data->wait);
    data->m = m;
    data->loops = JIT_ASYNC_LOOPS;
    data->prev_jiffies = cur_jiffies;
    data->hi = 0;  // 默认为普通优先级

    // 打印表头
    seq_printf(m, "   time   delta  inirq    pid   cpu command\n");
    seq_printf(m, "%9lu  %3lu     %i    %6i   %i   %s\n",
            cur_jiffies, 0L, in_interrupt() ? 1 : 0,
            current->pid, smp_processor_id(), current->comm);

    // 初始化并调度tasklet
    tasklet_init(&data->tasklet, jit_tasklet_fn, (unsigned long)data);
    tasklet_schedule(&data->tasklet);

    // 等待tasklet完成
    wait_event_interruptible(data->wait, !data->loops);
    if (signal_pending(current)) {
        tasklet_kill(&data->tasklet);
        kfree(data);
        return -ERESTARTSYS;
    }

    // 清理
    tasklet_kill(&data->tasklet);
    kfree(data);
    return 0;
}

static int jit_tasklet_open(struct inode *inode, struct file *file)
{
    return single_open(file, jit_tasklet_show, NULL);
}

static const struct proc_ops jit_tasklet_ops = {
    .proc_open = jit_tasklet_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static int __init jit_tasklet_init(void)
{
    if (!proc_create("jit_tasklet", 0, NULL, &jit_tasklet_ops))
        return -ENOMEM;
    printk(KERN_INFO "jit_tasklet: module loaded successfully!\n");
    return 0;
}

static void __exit jit_tasklet_cleanup(void)
{
    remove_proc_entry("jit_tasklet", NULL);
    printk(KERN_INFO "jit_tasklet: module unloaded successfully!\n");
}

module_init(jit_tasklet_init);
module_exit(jit_tasklet_cleanup);

MODULE_AUTHOR("dinggongwurusai");
MODULE_DESCRIPTION("A simple module to test the kernel tasklet");
MODULE_LICENSE("Dual BSD/GPL");