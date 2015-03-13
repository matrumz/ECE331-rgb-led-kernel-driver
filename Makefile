# Project 1 kernel module make file

MODULE_NAME = rgbled2

obj-m += $(MODULE_NAME).o

all: test
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm test

install:
	sudo insmod $(MODULE_NAME).ko

remove:
	sudo rmmod $(MODULE_NAME)

mi: all install

rc: remove clean

test:
	rm test
	gcc -g -Wall -o test test.c
