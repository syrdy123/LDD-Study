#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>

static struct completion my_completion;

static int worker_thread(void *data)
{
    printk(KERN_INFO "Worker thread started, performing work...\n");
    msleep(2000);  //sleep 2s
    printk(KERN_INFO "Worker thread work done, completing the task.\n");
    //complete(&my_completion);  // Notify the waiting thread
    return 0;
}

static int __init my_module_init(void)
{
    printk(KERN_INFO "completion_test module init!\n");
    init_completion(&my_completion);

    // Create a worker thread
    kthread_run(worker_thread, NULL, "worker_thread");

    printk(KERN_INFO "Waiting for worker_thread complete...\n");
    wait_for_completion(&my_completion);

    printk(KERN_INFO "Completion received, worker thread done.\n");

    return 0;
}

static void __exit my_module_exit(void)
{
    printk(KERN_INFO "completion_test module exit!\n");
}

module_init(my_module_init);
module_exit(my_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("syrdy");
MODULE_DESCRIPTION("A simple completion test module!");
