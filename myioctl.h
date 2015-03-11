#include <linux/ioctl.h>

#define IOC_MAGIC 'r'

#define WRITENUM 0x20
#define TYPE unsigned short *

#define MYWRITE _IOW(IOC_MAGIC,WRITENUM,TYPE)
