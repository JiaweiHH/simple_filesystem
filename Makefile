obj-m := simplefs.o

KERNEL_DIR := /lib/modules/$(shell uname -r)/build

default:
	$(MAKE) -C ${KERNEL_DIR} M=$(PWD) modules

mkfs:
	gcc mkfs.simple.c -o mkfs.simple

clean:
	rm -rf *.ko *.o *.mod.o *.mod.c *.symvers .*.cmd .tmp_versions mkfs.simple *.order
