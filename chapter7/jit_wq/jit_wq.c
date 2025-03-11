#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/fs.h>    
#include <linux/proc_fs.h>
#include <linux/errno.h>  
#include <linux/workqueue.h>
#include <linux/preempt.h>
#include <linux/interrupt.h> 
#include <linux/seq_file.h>

MODULE_LICENSE("Dual BSD/GPL");

/*
 * The delay for the delayed workqueue timer file.
 */
static long delay = 10;
module_param(delay, long, 0);


/*
 * This module is a silly one: it only embeds short code fragments
 * that show how enqueued tasks `feel' the environment
 */

#define LIMIT	(PAGE_SIZE-128)	/* don't print any more after this size */

/*
 * Print information about the current environment. This is called from
 * within the task queues. If the limit is reached, awake the reading
 * process.
 */
static DECLARE_WAIT_QUEUE_HEAD (jiq_wait);

#define JIT_ASYNC_LOOPS 5  // 添加循环次数限制

/*
 * Keep track of info we need between task queue runs.
 */
struct jiq_data {
	struct work_struct work;
	struct delayed_work delayed_work;
	unsigned long prev_jiffies;
	long delay;
	struct seq_file *m;
	int loops;
};

// 添加全局实例
static struct jiq_data jiq_d;


/*
 * Do the printing; return non-zero if the task should be rescheduled.
 */
static int jiq_print(struct jiq_data *data)
{
	unsigned long cur_jiffies = jiffies;

	if (!data->loops) { 
		wake_up_interruptible(&jiq_wait);
		return 0;
	}

	seq_printf(data->m, "%9lu  %4lu     %3i %5i %3i %s\n",
			cur_jiffies, cur_jiffies - data->prev_jiffies,
			preempt_count(), current->pid, smp_processor_id(),
			current->comm);

	data->prev_jiffies = cur_jiffies;
	data->loops--;
	return 1;
}


/*
 * Call jiq_print from a work queue
 */
static void jiq_print_wq(struct work_struct *work)
{
	struct jiq_data *data = container_of(work, struct jiq_data, work);;

	if (!jiq_print(data))
		return;
	
	schedule_work(&data->work);
}

static void jiq_print_delayed_wq(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct jiq_data *data = container_of(delayed_work, struct jiq_data, delayed_work);

	if (!jiq_print(data))
		return;
	
	schedule_delayed_work(&data->delayed_work, data->delay);
}

static int jiq_show(struct seq_file *m, void *v)
{
	DEFINE_WAIT(wait);
	
	// 初始化数据
	jiq_d.m = m;
	jiq_d.prev_jiffies = jiffies;
	jiq_d.delay = 0;
	jiq_d.loops = JIT_ASYNC_LOOPS;

	seq_printf(m, "    time  delta preempt   pid cpu command\n");
	
	prepare_to_wait(&jiq_wait, &wait, TASK_INTERRUPTIBLE);
	schedule_work(&jiq_d.work);
	schedule();
	finish_wait(&jiq_wait, &wait);

	// 添加信号处理
	if (signal_pending(current)) {
		cancel_work_sync(&jiq_d.work);
		return -ERESTARTSYS;
	}

	return 0;
}

static int jiq_show_delayed(struct seq_file *m, void *v)
{
	DEFINE_WAIT(wait);
	
	// 初始化数据
	jiq_d.m = m;
	jiq_d.prev_jiffies = jiffies;
	jiq_d.delay = delay;
	jiq_d.loops = JIT_ASYNC_LOOPS;

	seq_printf(m, "    time  delta preempt   pid cpu command\n");
	
	prepare_to_wait(&jiq_wait, &wait, TASK_INTERRUPTIBLE);
	schedule_delayed_work(&jiq_d.delayed_work, delay);
	schedule();
	finish_wait(&jiq_wait, &wait);

	// 添加信号处理
	if (signal_pending(current)) {
		cancel_delayed_work_sync(&jiq_d.delayed_work);
		return -ERESTARTSYS;
	}

	return 0;
}

// proc 文件操作函数
static int jiq_open(struct inode *inode, struct file *file)
{
	return single_open(file, jiq_show, NULL);
}

static int jiq_open_delayed(struct inode *inode, struct file *file)
{
	return single_open(file, jiq_show_delayed, NULL);
}

static const struct proc_ops jiq_proc_ops = {
	.proc_open = jiq_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static const struct proc_ops jiq_proc_ops_delayed = {
	.proc_open = jiq_open_delayed,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int __init jiq_init(void)
{
	// 初始化工作队列
	INIT_WORK(&jiq_d.work, jiq_print_wq);
	INIT_DELAYED_WORK(&jiq_d.delayed_work, jiq_print_delayed_wq);

	// 创建 proc 文件
	if (!proc_create("jiq_wq", 0, NULL, &jiq_proc_ops))
		return -ENOMEM;
	if (!proc_create("jiq_wqdelay", 0, NULL, &jiq_proc_ops_delayed))
		goto cleanup_wq;

	printk(KERN_INFO "jiq_wq: module loaded successfully!\n");
	return 0;

cleanup_wq:
	remove_proc_entry("jiq_wq", NULL);
	return -ENOMEM;
}

static void __exit jiq_cleanup(void)
{
	// 确保工作队列已经完成
	cancel_work_sync(&jiq_d.work);
	cancel_delayed_work_sync(&jiq_d.delayed_work);
	
	remove_proc_entry("jiq_wq", NULL);
	remove_proc_entry("jiq_wqdelay", NULL);
	printk(KERN_INFO "jiq_wq: module unloaded successfully!\n");
}

module_init(jiq_init);
module_exit(jiq_cleanup);

MODULE_AUTHOR("dinggongwurusai");
MODULE_DESCRIPTION("A simple module to test the kernel workqueue");
MODULE_LICENSE("Dual BSD/GPL");