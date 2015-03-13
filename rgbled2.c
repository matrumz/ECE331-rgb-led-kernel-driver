/* vim: set tabstop=4 softtabstop=0 noexpandtab shiftwidth=4 : */
/* 
 * Author: Mathew Rumsey
 * Date: 03 12 15
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

/* 
 * XMEGA TO GPIO PIN MAPPING
 * red: 	GPIO22 = pin# 15
 * green:	GPIO23 = pin# 16
 * blue: 	GPIO24 = pin# 18
 * clk:		GPIO25 = pin# 22
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
#include <linux/gpio.h>		/* Required for use of GPIO pins */
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
static int rgbled_gpio_config(void);

/* Used to keep track of registrations to handle cleanup */
static bool alloc_chrdev = false;
static bool add_cdev = false;
static bool create_device = false;
static bool create_class = false;
static bool red_pin_taken = false;
static bool green_pin_taken = false;
static bool blue_pin_taken = false;
static bool clk_pin_taken = false;

/* Enumeration defines */
//enum state {low, high};
//enum direction {out, in};

/* Struct defines */
//struct rpi_gpio_pin {
//	const u8 pin_num;
//	enum state state;
//	enum direction dir;
//};

struct rgbled_dev {
	struct cdev cdev;
	struct class *rgbled_class;
	struct mutex write_mtx;
	dev_t devno;
	dev_t rgbled_major; 
	dev_t rgbled_minor; 
	spinlock_t check_mtx_sl;
//	struct rpi_gpio_pin red_pin;
//	struct rpi_gpio_pin green_pin;
//	struct rpi_gpio_pin blue_pin;
//	struct rpi_gpio_pin clk_pin;
	struct gpio red_pin;
	struct gpio green_pin;
	struct gpio blue_pin;
	struct gpio clk_pin;
};

/* Global variables */
static struct rgbled_dev first_dev = {
	.rgbled_class = NULL,
	/* 0 for dynamic assignment */
	.rgbled_major = 0, 
	.rgbled_minor = 0, 
//	.red_pin = {.pin_num=15, .state=low, .dir=out},
//	.green_pin = {.pin_num=16, .state=low, .dir=out},
//	.blue_pin = {.pin_num=18, .state=low, .dir=out},
//	.clk_pin = {.pin_num=22, .state=low, .dir=out},
	.red_pin = {.gpio=22, .flags=GPIOF_OUT_INIT_LOW, .label="LED red val"},
	.green_pin = {.gpio=23, .flags=GPIOF_OUT_INIT_LOW, .label="LED green val"},
	.blue_pin = {.gpio=24, .flags=GPIOF_OUT_INIT_LOW, .label="LED blue val"},
	.clk_pin = {.gpio=25, .flags=GPIOF_OUT_INIT_LOW, .label="Clocks writes"},
};
static const struct file_operations rgbled_fops = {
	.owner = THIS_MODULE,
	.open = rgbled_open,
	.release = rgbled_release,
	.unlocked_ioctl = rgbled_unlocked_ioctl,
	.write = NULL,
	.read = NULL,
//Complete this list with NULLS
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
	
	/* Configure GPIO pins */
	if ((err = rgbled_gpio_config())) {
		rgbled_exit();
		return err;
	}

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

int rgbled_gpio_config(void)
{
	int err = 0;

/* ========== END VARIABLE LIST ========== */

	if ((err = gpio_request_one(first_dev.red_pin.gpio, 
								first_dev.red_pin.flags, 
								first_dev.red_pin.label))) {
		printk(KERN_ERR "E REQUESTING RED PIN\n");
		return err;
	} else 
		red_pin_taken = true;

	if ((err = gpio_request_one(first_dev.green_pin.gpio, 
								first_dev.green_pin.flags, 
								first_dev.green_pin.label))) {
		printk(KERN_ERR "E REQUESTING GREEN PIN\n");
		return err;
	} else 
		green_pin_taken = true;

	if ((err = gpio_request_one(first_dev.blue_pin.gpio, 
								first_dev.blue_pin.flags, 
								first_dev.blue_pin.label))) {
		printk(KERN_ERR "E REQUESTING BLUE PIN\n");
		return err;
	} else 
		blue_pin_taken = true;

	if ((err = gpio_request_one(first_dev.clk_pin.gpio, 
								first_dev.clk_pin.flags, 
								first_dev.clk_pin.label))) {
		printk(KERN_ERR "E REQUESTING CLOCK PIN\n");
		return err;
	} else
		clk_pin_taken = true;

	return 0;
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
	if (red_pin_taken)
		gpio_free(first_dev.red_pin.gpio);
	if (green_pin_taken)
		gpio_free(first_dev.green_pin.gpio);
	if (blue_pin_taken)
		gpio_free(first_dev.blue_pin.gpio);
	if (clk_pin_taken)
		gpio_free(first_dev.clk_pin.gpio);
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
