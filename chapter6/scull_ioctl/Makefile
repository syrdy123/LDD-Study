obj-m += scull_ioctl.o

KERNEL_DIR := /lib/modules/$(shell uname -r)/build

INCLUDE_DIR := $(PWD)/include
SRC_DIR := $(PWD)/src

BUILD_DIR := $(PWD)/build

all:
	make -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	make -C $(KERNEL_DIR) M=$(PWD) clean

EXTRA_CFLAGS += -I$(INCLUDE_DIR)

scull_ioctl-objs := src/scull_ioctl.o
