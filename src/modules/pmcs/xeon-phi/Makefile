MODULE_NAME=mchw_phi
obj-m += $(MODULE_NAME).o 
$(MODULE_NAME)-objs +=  mchw_core.o mc_experiments.o pmu_config_phi.o cbuffer.o monitoring_mod.o syswide.o 
SYMLINKS=$(patsubst %.o,%.c,$($(MODULE_NAME)-objs))
SOURCES=$(patsubst %.o,../%.c,$($(MODULE_NAME)-objs))

USER_CFLAGS ?=
#USER_CFLAGS ?= -ggdb -O0
EXTRA_CFLAGS := $(USER_CFLAGS) -DCONFIG_PMC_PHI -I$(src)/../include -I$(src)/..

KERNEL_TREE?=/var/tmp/jcsaez/Phi/build/linux-2.6.38+mpss3.5
# KERNEL_OUT=$(KERNEL_TREE)

.PHONY: debug

.SUFFIXES: .ko .debug

all:	$(SYMLINKS)
	make ARCH=k1om CROSS_COMPILE=k1om-mpss-linux- -C $(KERNEL_TREE) O=$(KERNEL_OUT)  M=$(PWD)  modules

clean:
	make ARCH=k1om CROSS_COMPILE=k1om-mpss-linux- -C $(KERNEL_TREE) O=$(KERNEL_OUT) M=$(PWD)  clean
	-rm -f $(SYMLINKS) ../*.o

$(SYMLINKS): $(SOURCES)
	@for file in $(SYMLINKS) ; do if [ ! -f $$file ]; then  ln -s ../$$file . ; fi ; done
	
.ko.debug: 
	objcopy --only-keep-debug $< $@
	strip --strip-debug --strip-unneeded $<
	objcopy --add-gnu-debuglink=$@ $<


debug: ${MODULE_NAME}.debug 



		
