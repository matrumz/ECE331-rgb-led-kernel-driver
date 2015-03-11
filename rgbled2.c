/*
 * This program allows multiple reading/writing threads to interact with a
 * 64 bit number, and controls concurrency problems.
 * Once a value is written, all other writers are blocked until the value
 * is read. Then the next writer may write.
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
#define HOLDER_MOD_AUTHOR "RUMSEY"
#define HOLDER_MOD_DESCR "64b_NUMBER_HOLDER"
#define HOLDER_MOD_SDEV "PSEUDO_DEVICE"
/* Defines for constants */
#define COUNT 1
#define FIRST 0
#define NAME "64bHolder"
#define MODE 0666
/* ioctl() defines. Source macros in "myioctl.h" */
#define IOCTL_WRITE MYWRITE
#define IOCTL_READ MYREAD

/* Function prototypes */
static int __init holder_init(void);
static void holder_exit(void);
static int holder_open(struct inode *, struct file *);
static int holder_release(struct inode *, struct file *); 
static int holder_num_read(u64 __user *);
static int holder_num_write(u64 __user *);
static char *holder_perm(struct device *dev, umode_t *mode);
static long holder_unlocked_ioctl(struct file *, unsigned int, unsigned long);

/* Used to keep track of registrations to handle cleanup */
static bool alloc_chrdev = false;
static bool add_cdev = false;
static bool create_device = false;
static bool create_class = false;

/* Struct defines */
struct holder_dev {
	struct cdev cdev;
	dev_t devno;
	struct class *holder_class;
	dev_t holder_major; 
	dev_t holder_minor; 
	u64 number;
	spinlock_t readable;
	spinlock_t check_mtx_sl;
	struct mutex write_mtx;
};

/* Global variables */
static struct holder_dev first_dev = {
	.holder_class = NULL,
	/* 0 for dynamic assignment */
	.holder_major = 0, 
	.holder_minor = 0, 
	.number = 0,
};
static const struct file_operations holder_fops = {
	.owner = THIS_MODULE,
	.open = holder_open,
	.release = holder_release,
	.write = NULL,
	.read = NULL,
	.unlocked_ioctl = holder_unlocked_ioctl,
};

int holder_init(void)
{
	int err = 0;
	struct device *test_device_create;

/* ========== END VARIABLE LIST ========== */

	printk(KERN_ALERT "holder here!\n");

	/* Allocate dynamic device number */
	if (first_dev.holder_major) {
		first_dev.devno = MKDEV(first_dev.holder_major, 
					first_dev.holder_minor);
		err = register_chrdev_region(first_dev.devno, COUNT, NAME);
	} else {
		err = alloc_chrdev_region(&(first_dev.devno), 
					first_dev.holder_minor, COUNT, NAME);
		first_dev.holder_major = MAJOR(first_dev.devno);
	}
	if (err) {
		printk(KERN_ERR "E REGISTERING DEVICE NUMBERS : %d\n", -err );
		holder_exit();
		return -err;
	} else
		alloc_chrdev = true;

	/* Build class */
	first_dev.holder_class = class_create(THIS_MODULE, NAME);
	if ((err = IS_ERR(first_dev.holder_class))) {
		printk(KERN_ERR "E CREATING CLASS : %d\n", -err);
		holder_exit();
		return -err;
	} else
		create_class = true;

	/* 
	 * Set permissions for device file access.
	 * Big thanks to Jerry S. for helping me with this.
	 */
	first_dev.holder_class->devnode = holder_perm;	

	/* Create device from class */
	test_device_create = device_create(first_dev.holder_class, NULL, 
						first_dev.devno, NULL, NAME);
	if ((err = IS_ERR(test_device_create))) {
		printk(KERN_ERR "E CREATING DEVICE : %d\n", -err);
		holder_exit();
		return -err;
	} else 
		create_device = true;
	
	/* Add device to system */
	cdev_init(&(first_dev.cdev), &holder_fops);
	first_dev.cdev.owner = THIS_MODULE;
	first_dev.cdev.ops = &holder_fops;
	if ((err = cdev_add(&(first_dev.cdev), first_dev.devno, COUNT))) {
		printk(KERN_ERR "E ADDING CDEV : %d", -err);
		holder_exit();
		return -err;
	} else
		add_cdev = true;
	
	/* Initialize mutexes */
	mutex_init(&(first_dev.write_mtx));

	/* Initialize spinlocks */
	spin_lock_init(&(first_dev.readable));
	spin_lock_init(&(first_dev.check_mtx_sl));

	return 0;
}

char *holder_perm(struct device *dev, umode_t *mode)
{
	/* If mode is a valid pointer, set my mode value */
	if (mode)
		*mode = MODE;

	return NULL;
}

long holder_unlocked_ioctl(struct file * filp, unsigned int cmd, 
				unsigned long arg)
{
	/* Service list of ioctl() commands */
	switch (cmd) {
		case (IOCTL_WRITE): /* User write to number */
			return holder_num_write((void *)arg);
			break;
		case (IOCTL_READ): /* User read from number */
			return holder_num_read((u64 *)arg);
			break;
		default: /* User entered bad command */
			printk(KERN_WARNING 
				"W BAD UNLOCKED_IOCTL CMD:cmd=%d\n", cmd);
			return -EINVAL;
			break;
	}
	
}

int holder_open(struct inode *inode, struct file *filp)
{
	struct holder_dev *dev;

/* ========== END VARIABLE LIST ========== */

	/* Taken from LDD 3e */
	dev = container_of(inode->i_cdev, struct holder_dev, cdev);
	filp->private_data = dev;

	return 0;
}

int holder_release(struct inode *inode, struct file *filp)
{
	/* No special actions upon releasing a file */
	return 0;
}

int holder_num_read(u64 *dst)
{
	/* If a write isn't currently happening, do a read */
	spin_lock(&(first_dev.readable));
	if (copy_to_user(dst, &(first_dev.number), sizeof(u64))) {
		spin_unlock(&(first_dev.readable));
		printk(KERN_WARNING "W READING\n");
		return -EFAULT;
	}
	spin_unlock(&(first_dev.readable));

	/* If there are any write()s waiting, allow next write() to run */
	spin_lock(&(first_dev.check_mtx_sl));
	if (mutex_is_locked(&(first_dev.write_mtx)))
		mutex_unlock(&(first_dev.write_mtx));
	spin_unlock(&(first_dev.check_mtx_sl));

	return 0;
}

int holder_num_write(u64 *src)
{
	int err;

/* ========== END VARIABLE LIST ========== */

	/* 
	 * Wait until a read() allows a write() 
	 * Allow interrupts to cancel call
	 */
	if ((err = mutex_lock_interruptible(&(first_dev.write_mtx))))
		return err;

	/* Write a value (and prevent a concurrent read) */
	spin_lock(&(first_dev.readable));
	if (copy_from_user(&(first_dev.number), src, sizeof(u64))) {
		spin_unlock(&(first_dev.readable));
		printk(KERN_WARNING "W WRITING\n");
		return -EFAULT;
	}
	spin_unlock(&(first_dev.readable));
	
	/* holder_read() will unlock mutex after a read has occurred */

	return 0;
}

void holder_exit(void)
{
	printk(KERN_ALERT "holder leaving!\n");

	/* Deallocate resources that were created in init() */
	if (add_cdev)
		cdev_del(&(first_dev.cdev));
	if (create_device)
		device_destroy(first_dev.holder_class, first_dev.devno);
	if (create_class)
		class_destroy(first_dev.holder_class);
	if (alloc_chrdev)
		unregister_chrdev_region(first_dev.devno, COUNT);
}

module_init(holder_init);
module_exit(holder_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(HOLDER_MOD_AUTHOR);
MODULE_DESCRIPTION(HOLDER_MOD_DESCR);
MODULE_SUPPORTED_DEVICE(HOLDER_MOD_SDEV);
