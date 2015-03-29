/* vim: set tabstop=4 softtabstop=0 noexpandtab shiftwidth=4 : */
/* 
 * Author: Mathew Rumsey
 * Date: 03 28 15
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
#include <linux/fs.h>		/* Required for device drivers */
#include <linux/types.h>	/* Required for dev_t (device numbers) */
#include <linux/ioctl.h>	/* Required for ioctl commands */
#include <linux/delay.h>	/* Required for udelay */
#include <linux/errno.h>	/* Set error messages */
#include <linux/cdev.h>		/* Char device registration */
#include <linux/device.h>	/* Creation of device file */
#include <linux/mutex.h>	/* Required for use of mutexes */
#include <linux/gpio.h>		/* Required for use of GPIO pins */
#include <asm/uaccess.h>	/* Move data to and from user-space */
#include "rgbled2.h"		/* Definitions that must be synced with user */

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

/* 
 * Function prototypes 
 * 
 * All functions return 0 on success and <0 on failure
 * unless specified otherwise.
*/
static int __init rgbled_init(void);
static int rgbled_gpio_config(void);
static int rgbled_open(struct inode *, struct file *);
static int rgbled_release(struct inode *, struct file *); 
static int rgbled_write_color(COLOR __user *);
static int rgbled_set(int, int, int);
static char get_int_bit(int, u16);
static char *rgbled_perm(struct device *dev, umode_t *mode);
static long rgbled_unlocked_ioctl(struct file *, unsigned int, unsigned long);
static void rgbled_exit(void);

/* Used to keep track of registrations to handle cleanup */
static bool alloc_chrdev = false;
static bool add_cdev = false;
static bool create_device = false;
static bool create_class = false;
static bool red_pin_taken = false;
static bool green_pin_taken = false;
static bool blue_pin_taken = false;
static bool clk_pin_taken = false;

/* Custom struct defines */
struct rgbled_dev {
	struct cdev cdev;
	struct class *rgbled_class;
	struct mutex write_mtx;
	struct gpio red_pin;
	struct gpio green_pin;
	struct gpio blue_pin;
	struct gpio clk_pin;
	dev_t devno;
	dev_t rgbled_major; 
	dev_t rgbled_minor; 
};

/* Global variables */
static struct rgbled_dev first_dev = {
	.rgbled_class = NULL,
	/* 0 for dynamic assignment */
	.rgbled_major = 0, 
	.rgbled_minor = 0, 
	/* Set GPIO pin configurations: GPIO#, OUTPUT, DESCRIPTION */
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
	.llseek = NULL,
	.write = NULL,
	.read = NULL,
	.aio_read = NULL,
	.aio_write = NULL,
	.read_iter = NULL,
	.write_iter = NULL,
	.iterate = NULL,
	.poll = NULL,
	.compat_ioctl = NULL,
	.flush = NULL,
	.fsync = NULL,
	.aio_fsync = NULL,
	.fasync = NULL,
	.sendpage = NULL,
	.splice_write = NULL,
	.splice_read = NULL,
};

/*
 * This is the module_init() equivalent.
 * It creates a device and device file with the proper permissions,
 * configures the GPIO pins used, and initializes a mutex.
 * Errors in this function will cause kernel module insertion to bail.
*/
int rgbled_init(void)
{
	int err = 0;
	struct device *test_device_create;

/* ========== END VARIABLE LIST ========== */

	/* This printk() can be used for debugging purposes */
	printk(KERN_ALERT "rgbled here!\n");

	/* Allocate dynamic device number */
	if (first_dev.rgbled_major) { /* != 0 means static allocation */
		first_dev.devno = MKDEV(first_dev.rgbled_major, 
					first_dev.rgbled_minor);
		err = register_chrdev_region(first_dev.devno, COUNT, NAME);
	} else { /* == 0 means dynamic allocation */
		err = alloc_chrdev_region(&(first_dev.devno), 
					first_dev.rgbled_minor, COUNT, NAME);
		first_dev.rgbled_major = MAJOR(first_dev.devno);
	}
	/* Catch device number allocation error or set task as completed */
	if (err) {
		printk(KERN_ERR "E REGISTERING DEVICE NUMBERS : %d\n", err );
		rgbled_exit();
		/* 
		 * register_chrdev_region and alloc_chrdev_region 
		 * return <0 for errors. 
		 */
		return err; 
	} else
		alloc_chrdev = true;

	/* Build class */
	first_dev.rgbled_class = class_create(THIS_MODULE, NAME);
	if ((err = IS_ERR(first_dev.rgbled_class))) {
		printk(KERN_ERR "E CREATING CLASS : %d\n", -err);
		rgbled_exit();
		/* IS_ERR has >0 return values for errors */
		return -err;
	} else
		create_class = true;

	/* Set permissions for device file access */
	first_dev.rgbled_class->devnode = rgbled_perm;	

	/* Create device from class */
	test_device_create = device_create(first_dev.rgbled_class, NULL, 
						first_dev.devno, NULL, NAME);
	/* Catch class creation error or set task as completed */
	if ((err = IS_ERR(test_device_create))) {
		printk(KERN_ERR "E CREATING DEVICE : %d\n", -err);
		rgbled_exit();
		/* IS_ERR has >0 return values for errors */
		return -err;
	} else 
		create_device = true;
	
	/* Add device to system */
	cdev_init(&(first_dev.cdev), &rgbled_fops);
	first_dev.cdev.owner = THIS_MODULE;
	first_dev.cdev.ops = &rgbled_fops;
	if ((err = cdev_add(&(first_dev.cdev), first_dev.devno, COUNT))) {
		printk(KERN_ERR "E ADDING CDEV : %d", err);
		rgbled_exit();
		/* cdev_add has <0 return values for errors */
		return err;
	} else
		add_cdev = true;
	
	/* Configure GPIO pins */
	if ((err = rgbled_gpio_config())) {
		rgbled_exit();
		/* rgbled_gpio_config has <0 return values for errors */
		return err;
	}

	/* Initialize mutex */
	mutex_init(&(first_dev.write_mtx));

	return 0;
}

/* 
 * Called by rgbled_init().
 * Sets the permissions on the device file.
 * Always returns NULL
 */
char *rgbled_perm(struct device *dev, umode_t *mode)
{
	/* If mode is a valid pointer, set my mode value */
	if (mode)
		*mode = MODE;

	return NULL;
}

/*
 * Called by rgbled_init().
 * Requests control of specified GPIO pins and sets their attributes. 
 */
int rgbled_gpio_config(void)
{
	int err = 0;

/* ========== END VARIABLE LIST ========== */

	/* Request and configure GPIO pin for the red LED */
	if ((err = gpio_request_one(first_dev.red_pin.gpio, 
								first_dev.red_pin.flags, 
								first_dev.red_pin.label))) {
		printk(KERN_ERR "E REQUESTING RED PIN\n");
		/* gpio_request_one has <0 return values for errors */
		return err;
	} else 
		red_pin_taken = true;

	/* Request and configure GPIO pin for the green LED */
	if ((err = gpio_request_one(first_dev.green_pin.gpio, 
								first_dev.green_pin.flags, 
								first_dev.green_pin.label))) {
		printk(KERN_ERR "E REQUESTING GREEN PIN\n");
		/* gpio_request_one has <0 return values for errors */
		return err;
	} else 
		green_pin_taken = true;

	/* Request and configure GPIO pin for the blue LED */
	if ((err = gpio_request_one(first_dev.blue_pin.gpio, 
								first_dev.blue_pin.flags, 
								first_dev.blue_pin.label))) {
		printk(KERN_ERR "E REQUESTING BLUE PIN\n");
		/* gpio_request_one has <0 return values for errors */
		return err;
	} else 
		blue_pin_taken = true;

	/* Request and configure GPIO pin for the XMEGA clock */
	if ((err = gpio_request_one(first_dev.clk_pin.gpio, 
								first_dev.clk_pin.flags, 
								first_dev.clk_pin.label))) {
		printk(KERN_ERR "E REQUESTING CLOCK PIN\n");
		/* gpio_request_one has <0 return values for errors */
		return err;
	} else
		clk_pin_taken = true;

	return 0;
}

/*
 * Called when user uses device file in ioctl() call.
 * Uses a switch-case to identify command and run it.
 * Bad commands generate kernel warning and return -EINVAL to user.
 */
long rgbled_unlocked_ioctl(struct file * filp, unsigned int cmd, 
				unsigned long arg)
{
	/* Service list of ioctl() commands */
	switch (cmd) {
		case (IOCTL_WRITE): /* User set LED color */
			/* rgbled_write_color has <0 return values for errors */
			return rgbled_write_color((COLOR *)arg);
			break;
		default: /* User entered bad command */
			printk(KERN_WARNING 
				"W BAD UNLOCKED_IOCTL CMD:cmd=%d\n", cmd);
			return -EINVAL;
			break;
	}
	
}

/* 
 * Called when user calls open() on device file for this module.
 * Checks user's requested permissions and acts accordingly.
 */
int rgbled_open(struct inode *inode, struct file *filp)
{
	struct rgbled_dev *dev;

/* ========== END VARIABLE LIST ========== */

	/* Check open mode/permissions to make sure write only */
	if ((filp->f_flags & O_ACCMODE) != O_WRONLY) {
		printk(KERN_WARNING "W PROCESS TRIED TO OPEN RGBLED2 WITH BAD PERMS\n");
		return -EACCES;
	}

	/* Let user open file */
	dev = container_of(inode->i_cdev, struct rgbled_dev, cdev);
	filp->private_data = dev;

	return 0;
}

/*
 * Called when user calls close() on file descriptor to this module's
 * device file.
 * It performs no actions.
 */
int rgbled_release(struct inode *inode, struct file *filp)
{
	/* No special actions upon releasing a file */
	return 0;
}

/* 
 * Called from rgbled_unlocked_ioctl() when corresponding command is received.
 * It creates a copy of the color sent by the user, 
 * waits until the mutex is available,
 * then sends that color to rgbled_set().
 * Once rgbled_set() returns, the mutex is released.
 */
int rgbled_write_color(COLOR * ucolor)
{
	int err = 0;
	COLOR kcolor = {0,0,0};

/* ========== END VARIABLE LIST ========== */

	/* Copy color to kernel */
	if (copy_from_user(&kcolor, ucolor, sizeof(COLOR))) {
		printk(KERN_WARNING "W GETTING COLOR FROM USER\n");
		return -EFAULT;
	}

	/* 
	 * Wait in line to set LED color.
	 * Allow interrupts to cancel call.
	 */
	if ((err = mutex_lock_interruptible(&(first_dev.write_mtx)))) {
		printk(KERN_WARNING "W WAITING PROCESS INTERRUPTED\n");
		/* mutex_lock_interruptible has <0 return values for errors */
		return err;
	}

	/* Set LED color */
	/* rgbled_set has <0 return values for errors */
	err = rgbled_set(kcolor.r, kcolor.g, kcolor.b);

	/* Release mutex */
	mutex_unlock(&(first_dev.write_mtx));

	return err;
}

/* 
 * Called by rgbled_write_color() and rgbled_exit().
 * It makes sure the passed color values are valid,
 * then passes the data to the XMEGA via the specified GPIO pins.
 */
int rgbled_set(int rv, int gv, int bv)
{
	int i = 0;

/* ========== END VARIABLE LIST ========== */

	/* Test if arguments are out of range */
	if ((rv < 0) || (rv > 2047)) {
		printk(KERN_WARNING "W BAD COLOR NUMBER\n");
		return -EINVAL; 
	}   
	if ((gv < 0) || (gv > 2047)) {
		printk(KERN_WARNING "W BAD COLOR NUMBER\n");
		return -EINVAL; 
	}   
	if ((bv < 0) || (bv > 2047)) {
		printk(KERN_WARNING "W BAD COLOR NUMBER\n");
		return -EINVAL;
	}

	/* 
	 * Write a value.
	 * Values are 11 bits and written MSb first since
	 * each bit is shifted into the LSb of the value stored on the XMEGA.
	 */
	for (i = 10; i >= 0; i--) {
		gpio_set_value_cansleep(first_dev.red_pin.gpio, 
								(int)(!get_int_bit(i, rv)));
		gpio_set_value_cansleep(first_dev.green_pin.gpio, 
								(int)(!get_int_bit(i, gv)));
		gpio_set_value_cansleep(first_dev.blue_pin.gpio, 
								(int)(!get_int_bit(i, bv)));
		gpio_set_value_cansleep(first_dev.clk_pin.gpio, 1);
		udelay(10);
		gpio_set_value_cansleep(first_dev.clk_pin.gpio, 0);
		udelay(10);
	}

	return 0;
}

/*
 * Called by rgbled_set().
 * Returns the bit value at position 'bit' in data 'num'.
 */
char get_int_bit(int bit, u16 num)
{
	if ((num & (1 << bit)))
		return 1;
	else 
		return 0;
}

/* 
 * This is the module_exit() equivalent.
 * It performs a single attempt to turn off the RGB LED,
 * then deallocates necessary resources in the proper order.
 */
void rgbled_exit(void)
{
	/* This printk() can be used for debugging purposes */
	printk(KERN_ALERT "rgbled leaving!\n");

	/* Attempt to turn off LED */
	if (mutex_trylock(&(first_dev.write_mtx)) && red_pin_taken
												&& green_pin_taken
												&& blue_pin_taken
												&& clk_pin_taken) {
		rgbled_set(0, 0, 0);
		mutex_unlock(&(first_dev.write_mtx));
	}

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

/* Sets custom insmod and rmmod functions */
module_init(rgbled_init);
module_exit(rgbled_exit);

/* Applies various tags to module */
MODULE_LICENSE("GPL");
MODULE_AUTHOR(RGBLED_MOD_AUTHOR);
MODULE_DESCRIPTION(RGBLED_MOD_DESCR);
MODULE_SUPPORTED_DEVICE(RGBLED_MOD_SDEV);
