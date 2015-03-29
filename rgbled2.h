/* vim: set tabstop=4 softtabstop=0 noexpandtab shiftwidth=4 : */
#include <linux/ioctl.h>

/* 
 * IOCTL defines 
 * 
 * Magic values chosen from free values located in
 * /usr/src/linux3.18.y/Documentation/ioctl/ioctl-number.txt
 */
#define IOC_MAGIC 'r'
#define WRITENUM 0x20
#define TYPE unsigned short *
#define MYWRITE _IOW(IOC_MAGIC,WRITENUM,TYPE)

/* Type defines */
typedef struct {
	int r;
	int g;
	int b;
} COLOR;
