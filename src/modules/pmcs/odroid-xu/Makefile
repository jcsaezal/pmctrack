MODULE_NAME=mchw_odroid_xu
obj-m += $(MODULE_NAME).o
$(MODULE_NAME)-objs +=  mchw_core.o mc_experiments.o pmu_config_arm.o cbuffer.o monitoring_mod.o syswide.o \
			ipc_sampling_sf_mm.o smart_power_driver.o smart_power_mm.o smart_power_2_mm.o
SYMLINKS=$(patsubst %.o,%.c,$($(MODULE_NAME)-objs))
SOURCES=$(patsubst %.o,../%.c,$($(MODULE_NAME)-objs))

USER_CFLAGS ?=
#USER_CFLAGS ?= -ggdb -O0
EXTRA_CFLAGS := $(USER_CFLAGS) -DCONFIG_PMC_ARM -DODROID -DCONFIG_SMART_POWER -DCONFIG_SMART_POWER_2 -I$(src)/../include -I$(src)/..

.PHONY: debug

.SUFFIXES: .ko .debug

all:	$(SYMLINKS)
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	-rm -f $(SYMLINKS) ../*.o

$(SYMLINKS): $(SOURCES)
	@for file in $(SYMLINKS) ; do if [ ! -f $$file ]; then  ln -s ../$$file . ; fi ; done

.ko.debug:
	objcopy --only-keep-debug $< $@
	strip --strip-debug --strip-unneeded $<
	objcopy --add-gnu-debuglink=$@ $<


debug: ${MODULE_NAME}.debug
