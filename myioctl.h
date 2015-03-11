#include <linux/ioctl.h>

#define IOC_MAGIC 't'

#define READNUM 0
#define WRITENUM 1
#define TYPE unsigned long long *

#define MYREAD _IOR(IOC_MAGIC,READNUM,TYPE)
#define MYWRITE _IOW(IOC_MAGIC,WRITENUM,TYPE)
