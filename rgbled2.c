/* vim: set tabstop=4 softtabstop=0 noexpandtab shiftwidth=4 : */
/* 
 * Author: Mathew Rumsey
 * Date: 03 11 15
 * 
 * This file contains the source code for ECE 331 Project #1.
 * This is a kernel module that interfaces with the RGB LED on Sheaff's 
 * RPi function generator expansion board.
 * It allows one or more processes to open the RGB LED device special file 
 * but only one process at a time should write to the GPIO pins while 
 * completing the 11 bit data write pattern to the RGB LED. 
 * Should a process open the device special file read/write or read only, it
 * receives an appropriate error. (The RGB LED is a write only device). 
 * The driver auto-magically creates the device special file with 
 * appropriate permissions.
 */

#include <linux/module.h>	/* Need */
#include <linux/kernel.h>	/* Need */
#include <linux/init.h>		/* Allow rename init and exit functions */
#include <linux/stat.h>		/* File status */
#include <linux/fs.h>		/* Required for device drivers */
#include <linux/types.h>	/* Required for dev_t (device numbers) */
#include <linux/spinlock.h>	/* Required for spinlocks */
#include <linux/ioctl.h>	/* Required for ioctl commands */
#include <linux/delay.h>	/* Required for udelay */
#include <linux/errno.h>	/* Set error messages */
#include <linux/cdev.h>		/* Char device registration */
#include <linux/device.h>	/* Creation of device file */
#include <linux/mutex.h>	/* Required for use of mutexes */
#include <asm/uaccess.h>	/* Move data to and from user-space */
#include "myioctl.h"		/* My ioctl command is here (sync with user) */

/* Defines for module */
#define RGBLED_MOD_AUTHOR "RUMSEY"
#define RGBLED_MOD_DESCR "DEVICE_DRIVER_FOR_SHEAFFS_XMEGA_RPi_EXPANSION_FG"
#define RGBLED_MOD_SDEV "SHEAFF_XMEGA_RPi_EXPANSION_FG"
/* Defines for constants */
#define COUNT 1
#define FIRST 0
#define NAME "rgbled"
#define MODE 0222
/* ioctl() defines. Source macros in "myioctl.h" */
#define IOCTL_WRITE MYWRITE

/* Function prototypes */
static int __init rgbled_init(void);
static void rgbled_exit(void);
static int rgbled_open(struct inode *, struct file *);
static int rgbled_release(struct inode *, struct file *); 
static int rgbled_set(u64 __user *);
static char *rgbled_perm(struct device *dev, umode_t *mode);
static long rgbled_unlocked_ioctl(struct file *, unsigned int, unsigned long);

/* Used to keep track of registrations to handle cleanup */
static bool alloc_chrdev = false;
static bool add_cdev = false;
static bool create_device = false;
static bool create_class = false;

/* Struct defines */
struct rgbled_dev {
	struct cdev cdev;
	dev_t devno;
	struct class *rgbled_class;
	dev_t rgbled_major; 
	dev_t rgbled_minor; 
	spinlock_t check_mtx_sl;
	struct mutex write_mtx;
};

/* Global variables */
static struct rgbled_dev first_dev = {
	.rgbled_class = NULL,
	/* 0 for dynamic assignment */
	.rgbled_major = 0, 
	.rgbled_minor = 0, 
};
static const struct file_operations rgbled_fops = {
	.owner = THIS_MODULE,
	.open = rgbled_open,
	.release = rgbled_release,
	.write = NULL,
	.read = NULL,
	.unlocked_ioctl = rgbled_unlocked_ioctl,
};

int rgbled_init(void)
{
	int err = 0;
	struct device *test_device_create;

/* ========== END VARIABLE LIST ========== */

	printk(KERN_ALERT "rgbled here!\n");

	/* Allocate dynamic device number */
	if (first_dev.rgbled_major) {
		first_dev.devno = MKDEV(first_dev.rgbled_major, 
					first_dev.rgbled_minor);
		err = register_chrdev_region(first_dev.devno, COUNT, NAME);
	} else {
		err = alloc_chrdev_region(&(first_dev.devno), 
					first_dev.rgbled_minor, COUNT, NAME);
		first_dev.rgbled_major = MAJOR(first_dev.devno);
	}
	if (err) {
		printk(KERN_ERR "E REGISTERING DEVICE NUMBERS : %d\n", -err );
		rgbled_exit();
		return -err;
	} else
		alloc_chrdev = true;

	/* Build class */
	first_dev.rgbled_class = class_create(THIS_MODULE, NAME);
	if ((err = IS_ERR(first_dev.rgbled_class))) {
		printk(KERN_ERR "E CREATING CLASS : %d\n", -err);
		rgbled_exit();
		return -err;
	} else
		create_class = true;

	/* Set permissions for device file access. */
	first_dev.rgbled_class->devnode = rgbled_perm;	

	/* Create device from class */
	test_device_create = device_create(first_dev.rgbled_class, NULL, 
						first_dev.devno, NULL, NAME);
	if ((err = IS_ERR(test_device_create))) {
		printk(KERN_ERR "E CREATING DEVICE : %d\n", -err);
		rgbled_exit();
		return -err;
	} else 
		create_device = true;
	
	/* Add device to system */
	cdev_init(&(first_dev.cdev), &rgbled_fops);
	first_dev.cdev.owner = THIS_MODULE;
	first_dev.cdev.ops = &rgbled_fops;
	if ((err = cdev_add(&(first_dev.cdev), first_dev.devno, COUNT))) {
		printk(KERN_ERR "E ADDING CDEV : %d", -err);
		rgbled_exit();
		return -err;
	} else
		add_cdev = true;
	
	/* Initialize mutexes */
	mutex_init(&(first_dev.write_mtx));

	/* Initialize spinlocks */
	spin_lock_init(&(first_dev.check_mtx_sl));

	return 0;
}

char *rgbled_perm(struct device *dev, umode_t *mode)
{
	/* If mode is a valid pointer, set my mode value */
	if (mode)
		*mode = MODE;

	return NULL;
}

long rgbled_unlocked_ioctl(struct file * filp, unsigned int cmd, 
				unsigned long arg)
{
	/* Service list of ioctl() commands */
	switch (cmd) {
		case (IOCTL_WRITE): /* User set LED color */
			return rgbled_set((void *)arg);
			break;
		default: /* User entered bad command */
			printk(KERN_WARNING 
				"W BAD UNLOCKED_IOCTL CMD:cmd=%d\n", cmd);
			return -EINVAL;
			break;
	}
	
}

int rgbled_open(struct inode *inode, struct file *filp)
{
	struct rgbled_dev *dev;

/* ========== END VARIABLE LIST ========== */

	/* User opens device file */
	dev = container_of(inode->i_cdev, struct rgbled_dev, cdev);
	filp->private_data = dev;

	return 0;
}

int rgbled_release(struct inode *inode, struct file *filp)
{
	/* No special actions upon releasing a file */
	return 0;
}

int rgbled_set(u64 *src)
{
	int err;

/* ========== END VARIABLE LIST ========== */

	/* 
	 * Wait in line to set LED color.
	 * Allow interrupts to cancel call.
	 */
	if ((err = mutex_lock_interruptible(&(first_dev.write_mtx))))
		return err;

	/* Write a value */

	return 0;
}

void rgbled_exit(void)
{
	printk(KERN_ALERT "rgbled leaving!\n");

	/* Deallocate resources that were created in init() */
	if (add_cdev)
		cdev_del(&(first_dev.cdev));
	if (create_device)
		device_destroy(first_dev.rgbled_class, first_dev.devno);
	if (create_class)
		class_destroy(first_dev.rgbled_class);
	if (alloc_chrdev)
		unregister_chrdev_region(first_dev.devno, COUNT);
}

module_init(rgbled_init);
module_exit(rgbled_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(RGBLED_MOD_AUTHOR);
MODULE_DESCRIPTION(RGBLED_MOD_DESCR);
MODULE_SUPPORTED_DEVICE(RGBLED_MOD_SDEV);
