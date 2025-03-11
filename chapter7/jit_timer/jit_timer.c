#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>

// 添加函数声明
static int __init jittimer_init(void);
static void __exit jittimer_cleanup(void);

#define JIT_ASYNC_LOOPS 5


static int g_delay = 10;
module_param(g_delay, int, 0644);



struct jit_data{
    struct timer_list timer;
    wait_queue_head_t wait;
    unsigned long prev_jiffies;
    struct seq_file *m;  // 改用 seq_file 指针
    int loops;
};


static void jit_timer_fn(struct timer_list *t)
{
    struct jit_data *data = from_timer(data, t, timer);
    unsigned long cur_jiffies = jiffies;
    
    // 使用 seq_printf 替代 sprintf
    seq_printf(data->m, "%9lu  %3lu     %i    %6i   %i   %s\n",
        cur_jiffies,
        cur_jiffies - data->prev_jiffies,
        in_interrupt() ? 1 : 0,
        current->pid,
        smp_processor_id(),
        current->comm
    );

    if(--data->loops){
        data->timer.expires += g_delay;
        data->prev_jiffies = cur_jiffies;
        add_timer(&data->timer);
    }
    else{
        wake_up_interruptible(&data->wait);
    }
}

static int jit_timer_show(struct seq_file *m, void *v)
{
    struct jit_data *data;
    unsigned long cur_jiffies;

    data = kmalloc(sizeof(struct jit_data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    cur_jiffies = jiffies;
    
    timer_setup(&data->timer, jit_timer_fn, 0);
    init_waitqueue_head(&data->wait);

    seq_printf(m, "   time   delta  inirq    pid   cpu command\n");
    seq_printf(m, "%9lu  %3lu     %i    %6i   %i   %s\n",
            cur_jiffies, 0L, in_interrupt() ? 1 : 0,
            current->pid, smp_processor_id(), current->comm);
    
    data->loops = JIT_ASYNC_LOOPS;
    data->m = m;  // 保存 seq_file 指针
    data->prev_jiffies = cur_jiffies;
    
    data->timer.expires = jiffies + g_delay;
    add_timer(&data->timer);

    wait_event_interruptible(data->wait, !data->loops);
    if (signal_pending(current)) {
        del_timer_sync(&data->timer);
        kfree(data);
        return -ERESTARTSYS;
    }

    del_timer_sync(&data->timer);
    kfree(data);
    
    return 0;
}

static int jit_timer_open(struct inode *inode, struct file *file)
{
    return single_open(file, jit_timer_show, NULL);
}

static const struct proc_ops jit_timer_ops = {
    .proc_open = jit_timer_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static int __init jittimer_init(void)
{
    if (!proc_create("jitimer", 0, NULL, &jit_timer_ops))
        return -ENOMEM;
    printk(KERN_INFO "jitimer: module loaded successfully!\n");
    return 0;
}

static void __exit jittimer_cleanup(void)
{
	remove_proc_entry("jitimer", NULL);
    printk(KERN_INFO "jitimer: module unloaded successfully!\n");
}

module_init(jittimer_init);
module_exit(jittimer_cleanup);

MODULE_AUTHOR("dinggongwurusai");
MODULE_DESCRIPTION("A simple module to test the kernel timer");
MODULE_LICENSE("Dual BSD/GPL");