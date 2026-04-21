#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/sched/signal.h>
#include <linux/mm.h>
#include <linux/pid.h>
#include <linux/signal.h>
#include <linux/version.h>

#include "monitor_ioctl.h"

#define DEVICE_NAME "container_monitor"
#define CLASS_NAME  "container_monitor_class"
#define CHECK_INTERVAL_MS 1000

struct monitored_proc {
    pid_t pid;
    char container_id[CONTAINER_ID_LEN];
    unsigned long soft_limit_bytes;
    unsigned long hard_limit_bytes;
    int soft_warned;
    struct list_head list;
};

static dev_t dev_num;
static struct class *monitor_class;
static struct cdev monitor_cdev;

static LIST_HEAD(monitored_list);
static DEFINE_MUTEX(monitored_lock);
static struct timer_list monitor_timer;

static unsigned long get_task_rss_bytes(struct task_struct *task)
{
    struct mm_struct *mm;
    unsigned long rss_pages = 0;
    unsigned long bytes = 0;

    if (!task)
        return 0;

    mm = get_task_mm(task);
    if (!mm)
        return 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 2, 0)
    rss_pages = get_mm_rss(mm);
#else
    rss_pages = get_mm_rss(mm);
#endif

    bytes = rss_pages << PAGE_SHIFT;
    mmput(mm);
    return bytes;
}

static void monitor_timer_fn(struct timer_list *t)
{
    struct monitored_proc *node, *tmp;

    mutex_lock(&monitored_lock);

    list_for_each_entry_safe(node, tmp, &monitored_list, list) {
        struct pid *pid_struct;
        struct task_struct *task;
        unsigned long rss;

        pid_struct = find_get_pid(node->pid);
        if (!pid_struct) {
            pr_info("monitor: removing stale pid=%d (%s)\n", node->pid, node->container_id);
            list_del(&node->list);
            kfree(node);
            continue;
        }

        task = get_pid_task(pid_struct, PIDTYPE_PID);
        put_pid(pid_struct);

        if (!task) {
            pr_info("monitor: removing exited pid=%d (%s)\n", node->pid, node->container_id);
            list_del(&node->list);
            kfree(node);
            continue;
        }

        rss = get_task_rss_bytes(task);

        if (!node->soft_warned && rss > node->soft_limit_bytes) {
            pr_info("monitor: SOFT LIMIT crossed container=%s pid=%d rss=%lu soft=%lu\n",
                    node->container_id, node->pid, rss, node->soft_limit_bytes);
            node->soft_warned = 1;
        }

        if (rss > node->hard_limit_bytes) {
            pr_info("monitor: HARD LIMIT crossed container=%s pid=%d rss=%lu hard=%lu -> SIGKILL\n",
                    node->container_id, node->pid, rss, node->hard_limit_bytes);
            send_sig(SIGKILL, task, 0);
        }

        put_task_struct(task);
    }

    mutex_unlock(&monitored_lock);

    mod_timer(&monitor_timer, jiffies + msecs_to_jiffies(CHECK_INTERVAL_MS));
}

static long monitor_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct monitor_request req;
    struct monitored_proc *node, *tmp;

    if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
        return -EFAULT;

    switch (cmd) {
    case MONITOR_REGISTER:
        node = kmalloc(sizeof(*node), GFP_KERNEL);
        if (!node)
            return -ENOMEM;

        node->pid = req.pid;
        node->soft_limit_bytes = req.soft_limit_bytes;
        node->hard_limit_bytes = req.hard_limit_bytes;
        node->soft_warned = 0;
        strscpy(node->container_id, req.container_id, sizeof(node->container_id));
        INIT_LIST_HEAD(&node->list);

        mutex_lock(&monitored_lock);
        list_add_tail(&node->list, &monitored_list);
        mutex_unlock(&monitored_lock);

        pr_info("monitor: registered container=%s pid=%d soft=%lu hard=%lu\n",
                node->container_id, node->pid,
                node->soft_limit_bytes, node->hard_limit_bytes);
        return 0;

    case MONITOR_UNREGISTER:
        mutex_lock(&monitored_lock);
        list_for_each_entry_safe(node, tmp, &monitored_list, list) {
            if (node->pid == req.pid) {
                list_del(&node->list);
                kfree(node);
                mutex_unlock(&monitored_lock);
                pr_info("monitor: unregistered pid=%d (%s)\n", req.pid, req.container_id);
                return 0;
            }
        }
        mutex_unlock(&monitored_lock);
        return -ENOENT;

    default:
        return -EINVAL;
    }
}

static int monitor_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int monitor_release(struct inode *inode, struct file *file)
{
    return 0;
}

static const struct file_operations monitor_fops = {
    .owner = THIS_MODULE,
    .open = monitor_open,
    .release = monitor_release,
    .unlocked_ioctl = monitor_ioctl,
};

static int __init monitor_init(void)
{
    int ret;

    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0)
        return ret;

    cdev_init(&monitor_cdev, &monitor_fops);
    monitor_cdev.owner = THIS_MODULE;

    ret = cdev_add(&monitor_cdev, dev_num, 1);
    if (ret < 0) {
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }

    monitor_class = class_create(CLASS_NAME);
    if (IS_ERR(monitor_class)) {
        cdev_del(&monitor_cdev);
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(monitor_class);
    }

    if (IS_ERR(device_create(monitor_class, NULL, dev_num, NULL, DEVICE_NAME))) {
        class_destroy(monitor_class);
        cdev_del(&monitor_cdev);
        unregister_chrdev_region(dev_num, 1);
        return -EINVAL;
    }

    timer_setup(&monitor_timer, monitor_timer_fn, 0);
    mod_timer(&monitor_timer, jiffies + msecs_to_jiffies(CHECK_INTERVAL_MS));

    pr_info("monitor: loaded\n");
    return 0;
}

static void __exit monitor_exit(void)
{
    struct monitored_proc *node, *tmp;

    timer_delete_sync(&monitor_timer);

    mutex_lock(&monitored_lock);
    list_for_each_entry_safe(node, tmp, &monitored_list, list) {
        list_del(&node->list);
        kfree(node);
    }
    mutex_unlock(&monitored_lock);

    device_destroy(monitor_class, dev_num);
    class_destroy(monitor_class);
    cdev_del(&monitor_cdev);
    unregister_chrdev_region(dev_num, 1);

    pr_info("monitor: unloaded\n");
}

module_init(monitor_init);
module_exit(monitor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Student");
MODULE_DESCRIPTION("Container memory monitor with soft/hard limits");
