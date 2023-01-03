MODULE_NAME=mchw_intel_core
obj-m += $(MODULE_NAME).o 
PMCSCHED-objs= pmcsched.o dummy_plugin.o group_plugin.o	busybcs_plugin.o
$(MODULE_NAME)-objs +=  mchw_core.o mc_experiments.o pmu_config_x86.o cbuffer.o monitoring_mod.o syswide.o \
					intel_rdt_mm.o intel_rapl_mm.o intel_rdt_core.o intel_rapl_core.o ipc_sampling_sf_mm.o \
					edp_core.o cache_part_set.o cache_partitioning.o pmctrack_stub.o intel_ehfi.o intel_rdt_userspace_mm.o intel_perf_metrics.o $(PMCSCHED-objs)

SYMLINKS=$(patsubst %.o,%.c,$($(MODULE_NAME)-objs))
SOURCES=$(patsubst %.o,../%.c,$($(MODULE_NAME)-objs))
HOSTNAME=$(shell hostname)

USER_CFLAGS ?= -O1 -fno-pie -ggdb  
EXTRA_CFLAGS += $(USER_CFLAGS) -DCONFIG_PMC_CORE_I7 -I$(src)/../include -I$(src)/.. -DHOST$(HOSTNAME)


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



		
