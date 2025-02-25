obj-m += scull_pipe.o

KERNEL_DIR := /lib/modules/$(shell uname -r)/build

INCLUDE_DIR := $(PWD)/include
SRC_DIR := $(PWD)/src

BUILD_DIR := $(PWD)/build

all:
	make -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	make -C $(KERNEL_DIR) M=$(PWD) clean

EXTRA_CFLAGS += -I$(INCLUDE_DIR)

scull_pipe-objs := src/scull_pipe.o
