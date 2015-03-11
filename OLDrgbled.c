/* vim: set tabstop=4 softtabstop=0 noexpandtab shiftwidth=4 : */
/* 
 * Author: Mathew Rumsey
 * Date: 03 02 15
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
#include <linux/stat.h>		/* Permissions */
#include <linux/fs.h>		/* Required for device drivers */
#include <linux/types.h>	/* Required for dev_t (device numbers) */
#include <linux/spinlock.h>	/* Required for spinlocks */
#include <linux/ioctl.h>	/* Required for ioctl commands */
#include <linux/delay.h>	/* Required for udelay */
#include <linux/errno.h>	/* Set error messages */
#include <linux/cdev.h>		/* Char device registration */
#include <linux/device.h>	/* Creation of device file */
#include <asm/uaccess.h>	/* Move data to and from user-space */
//#include <asm/semaphore.h>	/*  Required for use of semaphores */
//#include <linux/tty.h>	 /* Used for console_print() */

/* Defines for module */
#define RGBLED_MOD_AUTHOR RUMSEY
#define RGBLED_MOD_DESCR DEVICE_DRIVER_FOR_SHEAFFS_XMEGA_RPi_EXPANSION_FG
#define RGBLED_MOD_SDEV SHEAFF_XMEGA_RPi_EXPANSION_FG
/* Defines for constants */
#define COUNT 1
#define FIRST 0
#define NAME "rgbled"
#define MODE 222

/* Used to keep track of registrations to handle cleanup */
static bool alloc_chrdev = false;
static bool add_cdev = false;
static bool create_device = false;
static bool create_class = false;

/* Other necessary global variables */
dev_t devno;
static struct class *rgbled_class = NULL;
static struct file *filp = NULL;
struct rgbled_dev {
	struct cdev cdev;
};
static struct rgbled_dev dev;
static const struct file_operations rgbled_fops = {
	.owner = THIS_MODULE,
//	.open = rgbled_open,
//	.release = rgbled_release,
//	.write = rgbled_write,
//	.read = NULL,
//	.ioctl = rgbled_ioctl,
};
dev_t rgbled_major = 0, rgbled_minor = 0;

/* Function prototypes */
static int __init led_init(void);
static void led_exit(void);

int led_init(void)
{
	int err = 0;

/* ========== END VARIABLE LIST ========== */

	printk(KERN_ALERT "LED here!\n");

	/* Allocate dynamic device number */
	if (rgbled_major) {
		devno = MKDEV(rgbled_major, rgbled_minor);
		err = register_chrdev_region(devno, COUNT, NAME);
	} else {
		err = alloc_chrdev_region(&devno, rgbled_minor, COUNT, NAME);
		rgbled_major = MAJOR(devno);
	}
	if (err) {
		printk(KERN_ERR "E REGISTERING DEVICE NUMBERS : %d\n", -err );
		led_exit();
		return -err;
	} else
		alloc_chrdev = true;
	
	/* Build class */
	if ((rgbled_class = class_create(THIS_MODULE, NAME)) == NULL) {
		printk(KERN_ERR "E CREATING CLASS\n");
		led_exit();
		return -1;
//CHANGE THIS RETURN VALUE AT SOME POINT
	} else
		create_class = true;

	/* Create device from class */
	if (device_create(rgbled_class, NULL, devno, NULL, NAME) == NULL) {
		printk(KERN_ERR "E CREATING DEVICE\n");
		led_exit();
		return -1;
//CHANGE THIS RETURN VALUE AT SOME POINT
	} else 
		create_device = true;
	
	/* Register device */
	cdev_init(&(dev.cdev), &rgbled_fops);
	dev.cdev.owner = THIS_MODULE;
	dev.cdev.ops = &rgbled_fops;
	if ((err = cdev_add(&(dev.cdev), devno, COUNT))) {
		printk(KERN_ERR "E ADDING CDEV : %d", -err);
		led_exit();
		return -err;
	} else
		add_cdev = true;

	return 0;
}

void led_exit(void)
{
	printk(KERN_ALERT "LED leaving!\n");

	if (add_cdev)
		cdev_del(&(dev.cdev));
	if (create_device)
		device_destroy(rgbled_class, devno);
	if (create_class)
		class_destroy(rgbled_class);
	if (alloc_chrdev)
		unregister_chrdev_region(devno, COUNT);
}

module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
//MODULE_AUTHOR(RGBLED_MOD_AUTHOR);
//MODULE_DESCRIPTION(RGBLED_MOD_DESCR);
//MODULE_SUPPORTED_DEVICE(RGBLED_MOD_SDEV);
