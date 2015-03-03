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
#include <asm/uaccess.h>	/* Move data to and from user-space */
/* #include <asm/semaphore.h>	 Required for use of semaphores */
/* #include <linux/tty.h>	 used for console_print() */

#define RGBLED_MOD_AUTHOR Rumsey
#define RGBLED_MOD_DESCR XXXX
#define RGBLED_MOD_SDEV SHEAFF_XMEGA_RPi_EXPANSION_FG

MODULE_LICENSE("GPL");
/*MODULE_AUTHOR(RGBLED_MOD_AUTHOR);
MODULE_DESCRIPTION(RGBLED_MOD_DESCR);
MODULE_SUPPORTED_DEVICE(RGBLED_MOD_SDEV);
*/

static int led_init(void)
{
	printk(KERN_ALERT "LED here!\n");
	return 0;
}

static void led_exit(void)
{
	printk(KERN_ALERT "LED leaving!\n");
}

module_init(led_init);
module_exit(led_exit);
