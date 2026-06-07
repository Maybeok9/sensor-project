#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/random.h>
#include "sensor_ioctl.h"

#define KERNEL_RING_BUF_SIZE 8

//-----------------------Simul Sensor Structure--------------//
/* Structure of each sensor */
struct sensor_dev {
    int sensor_id;
    int sampling_rate;                  /* Hz */
    struct hrtimer timer;               /* Sim data */
    spinlock_t lock;                    /* Read/write lock for dev buffer */
    
    /* 8 entries per sensor */
    struct sensor_sample buffer[KERNEL_RING_BUF_SIZE];
    int head;                           /* Write */
    int tail;                           /* Read */
    int count;                          /* Remained */
    
    /* Stats */
    uint32_t read_count;
    uint32_t error_count;
    int32_t last_value;
};

/* 3 sensors */
static struct sensor_dev sensor_group[SENSOR_COUNT];

//---------------------Simul Data Source-----------------//
/* Random gen per s_dev for writing */
static void s_random_gen(struct sensor_dev *s_dev) {

    unsigned long flags;

    struct sensor_sample new_sample;
    /* Per sensor id */
    new_sample.sensor_id = s_dev->sensor_id;
    /* Per sensor value acquiring */
    switch (s_dev->sensor_id) {
	case SENSOR_TYPE_TEMPERATURE:
	new_sample.value = (2000 + (get_random_u32()%6001));
	break;
	case SENSOR_TYPE_HUMIDITY:
	new_sample.value = (1000 + (get_random_u32()%8501));
	break;
	case SENSOR_TYPE_PRESSURE:
	new_sample.value = (90000 + (get_random_u32()%20001));
	break;
	default :
	new_sample.value = 0;
	break;
    }
    /* Per sensor timestamp */
    new_sample.timestamp_us = ktime_to_us(ktime_get());
    spin_lock_irqsave(&s_dev->lock , flags);
    if (s_dev->count==KERNEL_RING_BUF_SIZE){
	s_dev->tail = (s_dev->tail + 1) % KERNEL_RING_BUF_SIZE;
	s_dev->error_count ++;
    }
    else s_dev->count ++;
    s_dev->buffer[s_dev->head] = new_sample;
    s_dev->head = ((s_dev->head+1)%KERNEL_RING_BUF_SIZE);
    spin_unlock_irqrestore(&s_dev->lock , flags);
};

/* Random number gen via hrtimer */
static enum hrtimer_restart hrtimer_handler(struct hrtimer *timer) {
    /* Get sensor_dev from each hrtimer */
    struct sensor_dev *s_dev = container_of(timer,struct sensor_dev, timer);
    s_random_gen(s_dev);
    /* Timer run for each sampling rate */
    hrtimer_forward_now(timer, ktime_set(0, 1000000000 / s_dev->sampling_rate));
    return HRTIMER_RESTART;
};

//------------File operations for /dev/sensor0--------//
/* Open */
static int sensor_open(struct inode *inode, struct file *file) {
    int *slt = kmalloc(sizeof(int), GFP_KERNEL);
    if (!slt) return -ENOMEM;
    *slt = 0;
    file->private_data = (void*)slt;
    return 0;
};
/* Release */
static int sensor_release(struct inode *inode, struct file *file) {
    if (file->private_data) kfree(file->private_data);
    return 0;
};
/* Read */
static ssize_t sensor_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    struct sensor_dev * s_dev;
    unsigned long flags;
    struct sensor_sample new_sample ;
    char str[128];
    int p_len;
    int avai =0;
    
    if (*ppos > 0)
        return 0;
    s_dev = &sensor_group[*(int*)file->private_data];
    /* Read per sensor gen buffer */
    spin_lock_irqsave(&s_dev->lock, flags);
    if (s_dev->count) {
        new_sample = s_dev->buffer[s_dev->tail];
        s_dev->tail = (s_dev->tail + 1) % KERNEL_RING_BUF_SIZE;
        s_dev->count --;
        s_dev->read_count ++;
        s_dev->last_value = new_sample.value;
        spin_unlock_irqrestore(&s_dev->lock, flags);
        avai = 1;
    }
    else {
        spin_unlock_irqrestore(&s_dev->lock, flags);
        return -EAGAIN;
    }
    if (avai) {
        p_len = snprintf(str, sizeof(str), "<%d>:<%d>:<%lld>\n",new_sample.sensor_id,new_sample.value,new_sample.timestamp_us);
        if (p_len > count){
            return -EINVAL;
        }
        
        if (copy_to_user(buf,str,p_len)) {
            return -EFAULT;
        }
        *ppos += p_len;
        return p_len;
    }
    return -EIO;
};
/* IOCTL */
static long sensor_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    unsigned long flags;
    struct sensor_dev * s_dev;
    struct sensor_sample new_sample;
    struct sensor_stats temp_stats;
    
    switch (cmd) {
    /* SELECT SENSOR */
    case SENSOR_SELECT :
    *(int*)file->private_data = (int)arg;
    break;
    /* READ SAMPLE */
    case SENSOR_READ_SAMPLE :
    s_dev = &sensor_group[*(int*)file->private_data];
    /* Read per sensor gen buffer */
    
    spin_lock_irqsave(&s_dev->lock, flags);
    if (s_dev->count) {
        new_sample = s_dev->buffer[s_dev->tail];
        s_dev->tail = (s_dev->tail + 1) % KERNEL_RING_BUF_SIZE;
        s_dev->count --;
        s_dev->read_count ++;
        s_dev->last_value = new_sample.value;
        spin_unlock_irqrestore(&s_dev->lock, flags);
        
    }
    else {
        spin_unlock_irqrestore(&s_dev->lock, flags);
        
        return -EAGAIN;
    }
    if (copy_to_user((struct sensor_sample *)arg, &new_sample, sizeof(struct sensor_sample))) {
        return -EFAULT;
    }
    
    break;
    /* SET RATE */
    case SENSOR_SET_RATE :
    s_dev = &sensor_group[*(int*)file->private_data];
    
    spin_lock_irqsave(&s_dev->lock, flags);
    s_dev->sampling_rate = (int) arg;
    hrtimer_start(&s_dev->timer, ktime_set (0, 1000000000 / s_dev->sampling_rate),HRTIMER_MODE_REL);
    spin_unlock_irqrestore(&s_dev->lock, flags);
    
    break;
    /* GET STATS */
    case SENSOR_GET_STATS :
    s_dev = &sensor_group[*(int*)file->private_data];
    
    spin_lock_irqsave(&s_dev->lock, flags);
    temp_stats.sensor_id = s_dev->sensor_id;
    temp_stats.read_count = s_dev->read_count;
    temp_stats.error_count = s_dev->error_count;
    temp_stats.sampling_rate = s_dev->sampling_rate;
    temp_stats.last_value = s_dev->last_value;
    spin_unlock_irqrestore(&s_dev->lock, flags);
    
    if (copy_to_user((struct sensor_stats *)arg, &temp_stats, sizeof(struct sensor_stats))) {
        return -EFAULT;
    }
    break;
    
    /* RESET */
    case SENSOR_RESET :
    s_dev = &sensor_group[*(int*)file->private_data];
    hrtimer_cancel(&s_dev->timer);
    
    spin_lock_irqsave(&s_dev->lock, flags);
    s_dev->read_count = 0;
    s_dev->error_count = 0;
    s_dev->head = 0;
    s_dev->tail = 0;
    s_dev->count = 0;
    s_dev->last_value = 0;
    s_dev->sampling_rate = 1;
    hrtimer_start(&s_dev->timer, ktime_set(0, 1000000000 / s_dev->sampling_rate),HRTIMER_MODE_REL);
    spin_unlock_irqrestore(&s_dev->lock, flags);
    
    break;
    
    default :
    return -EINVAL;
    break;
    
    }
    return 0;
};
static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = sensor_open,
    .release = sensor_release,
    .read = sensor_read,
    .unlocked_ioctl = sensor_ioctl,
};
static struct miscdevice sensor0 = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "sensor0",
    .fops = &fops,
    .mode = 0644,
};

//------------------Module integrating--------------//
static int __init s_driver_init(void){
    int i,regi;
    regi = misc_register(&sensor0);
    if (regi) {
        pr_err("Ko dang ky duoc sensor0");
        return regi;
    }
    /* Bring up each sensor and their sample gen */
    for (i=0 ; i < SENSOR_COUNT ; i++) {
    sensor_group[i].sensor_id = i;
    sensor_group[i].sampling_rate = 1;
    hrtimer_init(&sensor_group[i].timer,CLOCK_MONOTONIC,HRTIMER_MODE_REL);
    sensor_group[i].timer.function = hrtimer_handler;
    spin_lock_init(&sensor_group[i].lock);
    sensor_group[i].head = 0;
    sensor_group[i].tail = 0;
    sensor_group[i].count = 0;
    sensor_group[i].read_count = 0;
    sensor_group[i].error_count = 0;
    sensor_group[i].last_value = 0;
    hrtimer_start(&sensor_group[i].timer, ktime_set(0, 1000000000 / sensor_group[i].sampling_rate),HRTIMER_MODE_REL);
    }
    pr_info("Load thanh cong module");
    return 0;
};

//------------------Module disintegrating--------------//
static void __exit s_driver_exit(void){
    int i;
    misc_deregister(&sensor0);
    for (i=0 ; i < SENSOR_COUNT ; i++) {
    hrtimer_cancel(&sensor_group[i].timer);
    }
    pr_info("Unload module thanh cong");
};

module_init(s_driver_init);
module_exit(s_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Phai gi");
MODULE_DESCRIPTION("Phai chiu");
