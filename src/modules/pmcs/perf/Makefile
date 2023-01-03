MODULE_NAME=mchw_perf
obj-m += $(MODULE_NAME).o
$(MODULE_NAME)-objs +=  mchw_core.o mc_experiments.o pmu_config_perf.o cbuffer.o monitoring_mod.o syswide.o  pmctrack_stub.o
ifeq ($(shell uname -m),x86_64)
PMCSCHED-objs= pmcsched.o dummy_plugin.o group_plugin.o	busybcs_plugin.o
$(MODULE_NAME)-objs += 	intel_rdt_mm.o intel_rapl_mm.o intel_rdt_core.o intel_rapl_core.o \
						ipc_sampling_sf_mm.o edp_core.o cache_part_set.o cache_partitioning.o \
						intel_rdt_userspace_mm.o intel_ehfi.o intel_perf_metrics.o $(PMCSCHED-objs)  
else



#$(info not x86)
$(MODULE_NAME)-objs += 
endif
SYMLINKS=$(patsubst %.o,%.c,$($(MODULE_NAME)-objs))
SOURCES=$(patsubst %.o,../%.c,$($(MODULE_NAME)-objs))
HOSTNAME=$(shell hostname)

#USER_CFLAGS ?= -O2
USER_CFLAGS ?= -ggdb #-O1O
EXTRA_CFLAGS += $(USER_CFLAGS) -DCONFIG_PMC_PERF -I$(src)/../include -I$(src)/.. -DHOST$(HOSTNAME)

ifeq ($(CONFIDENTIAL),1)
EXTRA_CFLAGS += -DPMCTRACK_CONFIDENTIAL
ifeq ($(HOSTNAME),alderlake)
EXTRA_CFLAGS += -DHAVE_HRESET
endif
endif

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



		
