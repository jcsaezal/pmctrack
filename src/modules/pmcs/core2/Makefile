MODULE_NAME=mchw_core2
obj-m += $(MODULE_NAME).o 
$(MODULE_NAME)-objs +=  mchw_core.o mc_experiments.o pmu_config_x86.o cbuffer.o monitoring_mod.o syswide.o \
					ipc_sampling_sf_mm.o pmctrack_stub.o
					 

SYMLINKS=$(patsubst %.o,%.c,$($(MODULE_NAME)-objs))
SOURCES=$(patsubst %.o,../%.c,$($(MODULE_NAME)-objs))

USER_CFLAGS ?=
#USER_CFLAGS ?= -ggdb -O0
EXTRA_CFLAGS += $(USER_CFLAGS) -DCONFIG_PMC_CORE_2_DUO -I$(src)/../include -I$(src)/..


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



		
