/* vim: set tabstop=4 softtabstop=0 noexpandtab shiftwidth=4 : */
#include <linux/ioctl.h>

/* IOCTL defines */
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
