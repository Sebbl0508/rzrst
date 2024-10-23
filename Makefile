obj-m += rzrst.o

PWD := $(shell pwd)

KERNELRELEASE ?= `uname -r`
KERNEL_DIR ?= /lib/modules/$(KERNELRELEASE)/build

all:
	make -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	make -C $(KERNEL_DIR) M=$(PWD) clean
