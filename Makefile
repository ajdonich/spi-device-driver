
# ON/OFF flag for debug logging
DEBUG = y

# "-O" is needed to expand inlines
ifeq ($(DEBUG),y)
	DBFLAGS = -O -g -DSPI_TFT_DEBUG
else
	DBFLAGS = -O2
endif
EXTRA_CFLAGS += $(DBFLAGS)

ifneq ($(KERNELRELEASE),)
# call from kernel build system
obj-m := tftdriver.o

else
# Yocto will set KERNEL_SRC to the value of the STAGING_KERNEL_DIR, see:
# https://docs.yoctoproject.org/kernel-dev/common.html#incorporating-out-of-tree-modules
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
SRC := $(shell pwd)

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC)

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install

endif

devicetree:
	dtc -q -I dts -O dtb -o ilitft.dtbo devicetree/ilitft.dtso

clean:
	rm -f *.dtbo *.dtso.pp
	rm -f *.o *~ core .depend .*.cmd *.ko *.mod *.mod.c 
	rm -f Module.markers Module.symvers modules.order
	rm -rf .tmp_versions Modules.symvers
