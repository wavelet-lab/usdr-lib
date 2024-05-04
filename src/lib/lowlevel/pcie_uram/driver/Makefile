ifneq ($(KERNELRELEASE),)

obj-m += usdr_pcie_uram.o

else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build

modules modules_install clean::
	make -C $(KERNELDIR) M=$(PWD) $@

clean::
	rm -f *.o Module.markers modules.order

endif

