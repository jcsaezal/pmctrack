PMCTrack is an open-source OS-oriented performance monitoring tool for GNU/Linux. This performance tool has been specifically designed to aid kernel developers in implementing scheduling algorithms or resource-management strategies on Linux that leverage data from performance monitoring counters (PMCs) to perform optimizations at run time. Unlike other monitoring tools, PMCTrack features and in-kernel API enabling the OS to access per-thread PMC data in an architecture-independent fashion. 

Despite being an OS-oriented tool, PMCTrack still allows the gathering of PMC values from user space, enabling kernel developers to carry out the necessary offline analysis and debugging to assist them during the OS-level design process. In addition, the tool provides both the OS and the userspace PMCTrack components with other insightful metrics available in modern processors that are not directly exposed as PMCs, such as cache occupancy or energy consumption. 

[![Analytics](https://ga-beacon.appspot.com/UA-76163836-1/github-pmctrack/readme)](http://pmctrack.dacya.ucm.es)

## Project Contributors

* Juan Carlos Saez Alcaide (<jcsaezal@ucm.es>) - Creator of PMCTrack and main maintainer 
* Adrián García García (aka [@Mizadri](https://github.com/mizadri))
* Lazaro Clemen Palafox (aka [@lz-palafox](https://github.com/lz-palafox))
* Jaime Sáez de Buruaga Brouns (aka [@jaimesaez97](https://github.com/jaimesaez97))

## Past Contributors

* Jorge Casas Hernan 
* Abel Serrano Juste (aka [@Akronix](https://github.com/Akronix))
* Germán Franco Dorca
* Andrés Plaza Hernando
* Javier Setoain
* Guillermo Martinez Fernández
* Sergio Sánchez Gordo
* Sofía Dronda Merino

## Publications

* Juan Carlos Saez, Adrian Pousa, Roberto Rodríguez-Rodríguez, Fernando Castro, Manuel Prieto-Matias. (2017) "PMCTrack: Delivering performance monitoring counter support to the OS scheduler", *The Computer Journal*, Volume 60, Issue 1, 1 January 2017, pp. 60–85. ([pdf](https://artecs.dacya.ucm.es/files/cj-jcsaez-pmctrack-2017.pdf)) 
* Juan Carlos Saez, Jorge Casas, Abel Serrano, Roberto Rodríguez-Rodríguez, Fernando Castro, Daniel Chaver, Manuel Prieto-Matias. (2015) "An OS-Oriented Performance Monitoring Tool for Multicore Systems," _Euro-Par 2015 International Workshops: Revised Selected Papers_, pp. 697-709. 

## System requirements

Starting from version v2.0, PMCTrack works with vanilla Linux kernels, and, in this case, Linux kernel v5.9.x and above are highly recommended to enjoy the full functionality of the tool. For earlier versions of PMCTrack, a patched Linux kernel must be installed on the machine. A number of kernel patches for various Linux versions can be found in the `src/kernel-patches` directory. The name of each patch file encodes the Linux kernel version where the patch must be applied to as well as the processor architecture supported. The format is as follows: 

	pmctrack_linux-<kernel_version>_<architecture>.patch 

To apply the patch run the following command from the root directory of the kernel sources:

```
patch -p1 < <path_to_patch_file> 
```

To build the kernel for PMCTrack, the following option must be enabled when configuring the kernel:

	CONFIG_PMCTRACK=y


The kernel headers for the patched Linux version must be installed on the system as well. This is necessary for a successful out-of-tree build of PMCTrack's kernel module. An out-of-tree-ready Makefile can be found in the sources for the different flavors of the kernel module.

Most PMCTrack user-level components are written in C, and do not depend on any external library, (beyond the libc, of course). A separate Makefile is provided for _libpmctrack_ as well as for the various command-line tools. As such, it should be straightforward to build these software components on most Linux distributions.	

We also created PMCTrack-GUI, a Python front-end for the `pmctrack` command-line tool. This application extends the capabilities of the PMCTrack stack with features such as an SSH-based remote monitoring mode or the ability to plot the values of user-defined performance metrics in real time. This GUI application runs on Linux and Mac OS X and has the following software dependencies:

* Python v2.7
* Matplotlib (Python library)
* sshpass (command)
* WxPython v3.0

On Debian or Ubuntu the necessary software to run PMCTrack-GUI can be installed as follows:

	$ sudo apt install python2.7 python-matplotlib python-wxgtk3.0 sshpass 

On Mac OS X, PMCTrack-GUI has been succesfully tested after installing the above software dependencies using MacPorts as follows:

	## Install packages
	$ sudo port install py27-matplotlib py27-numpy py27-scipy py27-ipython py27-wxpython-3.0 sshpass
	
	## Set up default configuration for matplotlib 
	$ mkdir  ~/.matplotlib
	$ cp /opt/local/Library/Frameworks/Python.framework/Versions/2.7/lib/python2.7/site-packages/matplotlib/mpl-data/matplotlibrc  ~/.matplotlib
	
	## Select MacPorts Python27 interpreter by default
	$sudo port select --set python python27
	$sudo port select --set ipython ipython27


## Building PMCTrack from source for ARM and x86 processors

Before building PMCTrack make sure a compatible kernel is running (it must be a patched one for PMCTrack versions earlier than 2.0!), and the associated kernel header files are installed on the system. The `PMCTRACK_ROOT` environment variable must be defined for a successful execution of the various PMCTrack command-line tools. The `shrc` script found in the repository's root directory can be used to set the `PMCTRACK_ROOT` variable appropriately as well as to add command-line tools' directories to the PATH. To make this possible, run the following command in the root directory of the repository:

	$ . shrc

Now kernel-level and user-level components can be easily built with the `pmctrack-manager` script as follows:

	$ pmctrack-manager build
	**** System information ***
	Processor_vendor=Intel
	Kernel_HZ=250
	Processor_bitwidth=64
	***********************************************
	Press ENTER to start the build process...
	
	*************************************************
	*** Building supported PMCTrack kernel modules **
	*************************************************
	Building kernel module intel-core....
	============================================
	make -C /lib/modules/3.17.3.pmctrack-x86+/build M=/home/bench/pmctrack/src/modules/pmcs/intel-core modules
	make[1]: Entering directory '/usr/src/linux-headers-3.17.3.pmctrack-x86+'
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/mchw_core.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/mc_experiments.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/pmu_config_x86.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/cbuffer.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/monitoring_mod.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/syswide.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/intel_cmt_mm.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/intel_rapl_mm.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/ipc_sampling_sf_mm.o
	  LD [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/mchw_intel_core.o
	  Building modules, stage 2.
	  MODPOST 1 modules
	  CC      /home/bench/pmctrack/src/modules/pmcs/intel-core/mchw_intel_core.mod.o
	  LD [M]  /home/bench/pmctrack/src/modules/pmcs/intel-core/mchw_intel_core.ko
	make[1]: Leaving directory '/usr/src/linux-headers-3.17.3.pmctrack-x86+'
	Done!!
	============================================
	Building kernel module core2....
	============================================
	make -C /lib/modules/3.17.3.pmctrack-x86+/build M=/home/bench/pmctrack/src/modules/pmcs/core2 modules
	make[1]: Entering directory '/usr/src/linux-headers-3.17.3.pmctrack-x86+'
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/core2/mchw_core.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/core2/mc_experiments.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/core2/pmu_config_x86.o
	/home/bench/pmctrack/src/modules/pmcs/core2/pmu_config_x86.c: In function 'init_pmu_props':
	/home/bench/pmctrack/src/modules/pmcs/core2/pmu_config_x86.c:155:6: warning: unused variable 'model_cpu' [-Wunused-variable]
	  int model_cpu=0;
	      ^
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/core2/cbuffer.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/core2/monitoring_mod.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/core2/syswide.o
	  CC [M]  /home/bench/pmctrack/src/modules/pmcs/core2/ipc_sampling_sf_mm.o
	  LD [M]  /home/bench/pmctrack/src/modules/pmcs/core2/mchw_core2.o
	  Building modules, stage 2.
	  MODPOST 1 modules
	  CC      /home/bench/pmctrack/src/modules/pmcs/core2/mchw_core2.mod.o
	  LD [M]  /home/bench/pmctrack/src/modules/pmcs/core2/mchw_core2.ko
	make[1]: Leaving directory '/usr/src/linux-headers-3.17.3.pmctrack-x86+'
	Done!!
	============================================
	Building libpmctrack ....
	============================================
	make -C src all
	make[1]: Entering directory '/home/bench/pmctrack/src/lib/libpmctrack/src'
	cc -c -DHZ=250 -Wall -g -fpic -I ../include -I ../../../modules/pmcs/include/pmc -o core.o core.c
	cc -c -DHZ=250 -Wall -g -fpic -I ../include -I ../../../modules/pmcs/include/pmc -o pmu_info.o pmu_info.c
	cc -shared -DHZ=250 -o ../libpmctrack.so core.o pmu_info.o
	ar rcs ../libpmctrack.a core.o pmu_info.o 
	make[1]: Leaving directory '/home/bench/pmctrack/src/lib/libpmctrack/src'
	Done!!
	============================================
	Building pmc-events ....
	============================================
	cc  -DUSE_VFORK -Wall -g -I ../../modules/pmcs/include/pmc -I../../lib/libpmctrack/include   -c -o pmc-events.o pmc-events.c
	cc -o ../../../bin/pmc-events pmc-events.o  -L../../lib/libpmctrack -lpmctrack -static
	Done!!
	============================================
	Building pmctrack ....
	============================================
	cc  -DUSE_VFORK -Wall -g -I ../../modules/pmcs/include/pmc -I../../lib/libpmctrack/include   -c -o pmctrack.o pmctrack.c
	cc -o ../../../bin/pmctrack pmctrack.o  -L../../lib/libpmctrack -lpmctrack -static
	Done!!
	============================================
	*** BUILD PROCESS COMPLETED SUCCESSFULLY ***


The `pmctrack-manager` retrieves key information from the system and builds the command-line tools as well as the different flavors of the PMCTrack kernel module compatible with the current platform. If the build fails, build errors can be found in the `build.log` file created in the current directory.


## Building PMCTrack from source for the Intel Xeon Phi

In order to build the various PMCTrack components from source for the Xeon Phi Coprocessor
the `k1om-mpss-linux-gcc` cross-compiler must be used. Such a compiler is bundled with Intel MPSS. For a successful compilation, `pmctrack-manager` has to know the location of the Intel MPSS installation in the file system. In addition, the out-of-tree compilation of PMCTrack's kernel module for the Xeon Phi requires the build to be performed against a freshly built Linux kernel tree (MPSS version) with the PMCTrack patch. The entire source kernel tree must be found in the same system where the PMCTrack compilation is performed.

Once these requirements are met, `pmctrack-manager` can be used as follows to perform the build for the Xeon Phi:

	$ pmctrack-manager build-phi <mpss-root-dir> <kernel-sources-dir>


## Using PMCTrack from user space

Once the build has been completed, go to the root directory of you local copy of PMCTrack's repository and set up the necessary environment variables as follows:

	$ . shrc

After that, load any of the available flavors of the PMCTrack's kernel module compatible with your processor. Note that builds performed with `pmctrack-manager` (as shown above), only compile those flavors of the kernel module that may be suitable for your system. 

The following table summarizes the properties of the various flavors of the kernel module:

| Name | Path of the .ko file | Supported processors |
| -----| ---------------------| ---------------------|
| intel-core | `src/modules/pmcs/intel-core/mchw_intel_core.ko` | Most Intel multi-core processors are compatible with this module, including recent processors based on the Intel "Broadwell" microarchitecture. |
| amd | `src/modules/pmcs/amd/mchw_amd.ko` | This module has been successfully tested on AMD opteron processors. Nevertheless, it should be compatible with all AMD multicore processors. |
| arm | `src/modules/pmcs/arm/mchw_arm.ko` | This module has been successfully tested on ARM systems featuring 32-bit big.LITTLE processors, which combine ARM Cortex A7 cores with and ARM Cortex A15 cores. Specifically, tests were performed on the ARM Coretile Express Development Board (TC2). |
| odroid-xu | `src/modules/pmcs/odroid-xu/mchw_odroid_xu.ko` | Specific module for Odroid XU3 and XU4 boards. More information on these boards can be found at [www.hardkernel.com](http://www.hardkernel.com) |
| arm64 | `src/modules/pmcs/arm64/mchw_arm64.ko` | This module has been successfully tested on ARM systems featuring 64-bit big.LITTLE processors, which combine ARM Cortex A57 cores with and ARM Cortex A53 cores. Specifically, tests were performed on the ARM Juno Development Board. |
| xeon-phi | `src/modules/pmcs/xeon-phi/mchw_phi.ko` | Intel Xeon Phi Coprocessor |
| core2 | `src/modules/pmcs/phi/mchw_core2.ko` | This module has been specifically designed for the Intel QuickIA prototype system. The Intel QuickIA is a dual-socket asymmetric multicore system that features a quad-core Intel Xeon E5450 processor and a dual-core Intel Atom N330. The module also works with Intel Atom processors as well as "old" Intel multicore processors, such as the Intel Core 2 Duo. Nevertheless, given the numerous existing hacks for the QuickIA in this module, users are advised to use the more general "intel-core" flavor.  |
| perf | `src/modules/pmcs/perf/mchw_perf.ko` | Backend that uses Perf Events's kernel API to access performance monitoring counters. It currently works for Intel, AMD, ARMv7 and ARMv8 processors, only. |


Once the most suitable kernel model for the system has been identified, the module can be loaded in the running PMCTrack-enabled kernel as follows:

	$ sudo insmod <path_to_the_ko_file>

If the command did not return errors, information on the detected Performance Monitoring Units (PMUs) found in the machine can be retrieved by reading from the `/proc/pmc/info` file:

	$ cat /proc/pmc/info 
	*** PMU Info ***
	nr_core_types=1
	[PMU coretype0]
	pmu_model=x86_intel-core.haswell-ep
	nr_gp_pmcs=8
	nr_ff_pmcs=3
	pmc_bitwidth=48
	***************
	*** Monitoring Module ***
	counter_used_mask=0x0
	nr_experiments=0
	nr_virtual_counters=0
	***************

Alternatively the `pmc-events` helper command can be used to get the same information in a slightly different format:

	$ pmc-events -I
	[PMU 0]
	pmu_model=x86_intel-core.haswell-ep
	nr_fixed_pmcs=3
	nr_gp_pmcs=8

 On systems featuring asymmetric multicore processors, such as the ARM big.LITTLE, the `pmc-event` command will list as many PMUs as different core types exist in the system. On a 64-bit ARM big.LITTLE processor the output will be as follows:

	$ pmc-events -I
	[PMU 0]
	pmu_model=armv8.cortex_a53
	nr_fixed_pmcs=1
	nr_gp_pmcs=6
	[PMU 1]
	pmu_model=armv8.cortex_a57
	nr_fixed_pmcs=1
	nr_gp_pmcs=6


To obtain a listing of the hardware events supported, the following command can be used:

	$ pmc-events -L
	[PMU 0]
	instr_retired_fixed
	unhalted_core_cycles_fixed
	unhalted_ref_cycles_fixed
	instr
	cycles
	unhalted_core_cycles
	instr_retired
	unhalted_ref_cycles
	llc_references
	llc_references.prefetch
	llc_misses
	llc_misses.prefetch
	branch_instr_retired
	branch_mispred_retired
	l2_references
	l2_misses
	...


### The `pmctrack` command-line tool

The `pmctrack` command is the most straigthforward way of accessing PMCTrack functionality from user space. The available options for the command can be listed by just typing `pmctrack` in the console:

	$ pmctrack
	Usage: pmctrack [OPTION [OP. ARGS]] [PROG [ARGS]]
	Available options:
	        -c      <config-string>
	                set up a performance monitoring experiment using either raw or mnemonic-based PMC string
	        -o      <output>
	                output: set output file for the results. (default = stdout.)
	        -T      <Time>
	                Time: elapsed time in seconds between two consecutive counter samplings. (default = 1 sec.)
	        -b      <cpu or mask>
	                bind launched program to the specified cpu o cpumask.
	        -n      <max-samples>
	                Run command until a given number of samples are collected
	        -N      <secs>
	                Run command for secs seconds only
	        -e
	                Enable extended output
	        -A
	                Enable aggregate count mode
	        -k      <kernel_buffer_size>
	                Specify the size of the kernel buffer used for the PMC samples
	        -b      <cpu or mask>
	                bind monitor program to the specified cpu o cpumask.
	        -S
	                Enable system-wide monitoring mode (per-CPU)
	        -r
	                Accept pmc configuration strings in the RAW format
	        -p      <pmu>
	                Specify the PMU id to use for the event configuration
	        -L
	                Legacy-mode: do not show counter-to-event mapping
	        -t
	                Show real, user and sys time of child process
	PROG + ARGS:
	                Command line for the program to be monitored. 


Before introducing the basics of this command, it is worth describing the semantics of the `-c` and `-V` options. Essentially, the -c option accepts an argument with a string describing a set of hardware  events to be monitored. This string consists of comma-separated event configurations; in turn, an event configuration can be specified using event mnemonics or event hex codes found in the PMU manual provided by the processor's manufacturer. For example the hex-code based string `0x8,0x11` for an ARM Cortex A57 processor specifies the same event set than that of the `instr,cycles` string. Clearly, the latter format is far more intuitive than the former; the user can probably guess that we are trying to specify the hardware events "retired instructions" and "cycles". 

The `-V` option makes it possible to specify a set of _virtual counters_. Modern systems enable monitoring a set of hardware events using PMCs. Still, other monitoring information (e.g., energy consumption) may be exposed to the OS by other means, such as fixed-function registers, sensors, etc. This "non-PMC" data is exposed by the PMCTrack kernel module as _virtual counters_, rather than HW events. PMCTrack monitoring modules are in charge of implementing low-level access to _virtual counters_. To retrieve the list of HW events exported by the active monitoring module use `pmc-events -V`. More information on PMCTrack monitoring modules can be found in a separate section of this document.

The `pmctrack` command supports three usage modes:

1. **Time-Based Sampling (TBS)**: PMC and virtual counter values for a certain application are collected at regular time intervals.
1. **Event-Based Sampling (EBS)**:  PMC and virtual counter values for an application are collected every time a given PMC event reaches a given count.
2. **Time-Based system-wide monitoring mode**: This mode is a variant of the TBS mode, but monitoring information is provided for each CPU in the system, rather than for a specific application. This mode can be enabled with the `-S` switch. 

To illustrate how the TBS mode works let us consider the following example command invoked on a system featuring a quad-core Intel Xeon Haswell processor:

	$ pmctrack  -T 1  -c instr,llc_misses -V energy_core  ./mcf06
	[Event-to-counter mappings]
	pmc0=instr
	pmc3=llc_misses
	virt0=energy_core
	[Event counts]
	nsample    pid      event          pmc0           pmc3         virt0
	      1  10767       tick    2017968202       30215772       7930969
	      2  10767       tick    1220639346       24866936       7580993
	      3  10767       tick    1204660012       24726068       7432617
	      4  10767       tick    1524589394       20013147       8411560
	      5  10767       tick    1655802083        9520886       8531860
	      6  10767       tick    2555712483       18420142       6615844
	      7  10767       tick    2222232510       19594864       6385986
	      8  10767       tick    1348937378       22795510       5966308
	      9  10767       tick    1455948820       22282935       5994934
	     10  10767       tick    1324007762       22682355       5951354
	     11  10767       tick    1345928005       22477525       5968872
	     12  10767       tick    1345868008       22400733       6024780
	     13  10767       tick    1370194276       22121318       6024169
	     14  10767       tick    1329712408       22154371       6030700 
	     15  10767       tick    1365130132       21859147       6076293 
	     16  10767       tick    1315829803       21780616       6136962 
	     17  10767       tick    1357349957       20889360       6234619 
	     18  10767       tick    1377910047       19539232       6519897 
	...


This command provides the user with the number of instructions retired, last-level cache (LLC) misses and core energy consumption (in uJ) every second. The beginning of the command output shows the event-to-counter mapping for the various hardware events and virtual counters. The "Event counts" section in the output displays a table with the raw  counts for the various events; each sample (one per second) is represented by a different row. Note that the sampling period is specified in seconds via the -T option; fractions of a second can be also specified (e.g, 0.3 for 300ms). If the user includes the -A switch in the command line, `pmctrack` will display the aggregate event count for the application's entire execution instead. At the end of the line, we specify the command to run the associated application we wish to monitor (e.g: ./mcf06).

In case a specific processor model does not integrate enough PMCs to monitor a given set of events at once, the user can turn to PMCTrack's event-multiplexing feature. This boils down to specifying several event sets by including multiple instances of the -c switch in the command line. In this case, the various events sets will be collected in a round-robin fashion and a new `expid` field in the output will indicate the event set a particular sample belongs to. In a similar vein, time-based sampling also supports multithreaded applications. In this case, samples from each thread in the application will be identified by a different value in the pid column.

Event-based Sampling (EBS) constitutes a variant of time-based sampling wherein PMC values are gathered when a certain event count reaches a certain threshold _T_. To support EBS, PMCTrack's kernel module exploits the interrupt-on-overflow feature present in most modern Performance Monitoring Units (PMUs). To use the EBS feature from userspace, the "ebs" flag must be specified in `pmctrack` command line by an event's name. In doing so, a threshold value may be also specified as in the following example:

	$ pmctrack -c instr:ebs=500000000,llc_misses -V energy_core  ./mcf06 
	[Event-to-counter mappings]
	pmc0=instr
	pmc3=llc_misses
	virt0=energy_core
	[Event counts]
	nsample    pid      event          pmc0          pmc3         virt0
	      1  10839        ebs     500000078        892837        526489 
	      2  10839        ebs     500000047       9383500       1946166 
	      3  10839        ebs     500000050       9692922       2544250 
	      4  10839        ebs     500000007      10017122       2818298 
	      5  10839        ebs     500000012       9907918       3055236 
	      6  10839        ebs     500000011      10335579       3108215 
	      7  10839        ebs     500000046      10735151       3118713 
	      8  10839        ebs     500000011      10335980       3119140 
	      9  10839        ebs     500000004      10250777       3053100 
	     10  10839        ebs     500000019      11382679       2997802 
	     11  10839        ebs     500000035       6650139       2587890 
	     12  10839        ebs     500000004        474847       2596313 
	     13  10839        ebs     500000039        532301       2601074 
	     14  10839        ebs     500000019        577618       2617187 
	     15  10839        ebs     500000062       6221112       2442504 
	     16  10839        ebs     500000037       9177684       2325317 
	     17  10839        ebs     500000058       2697348       1106994 
	     18  10839        ebs     500000006       3520781       1264404 
	     19  10839        ebs     500000055       2777145       1119934 
	     20  10839        ebs     500000034       1965457        964843 
	     21  10839        ebs     500000004       2290861       1035095 
	     22  10839        ebs     500000011       3276917       1217895 
	     23  10839        ebs     500000041       4202958       1409973 
	     24  10839        ebs     500000034       5343461       1608947
	...

The `pmc3` and `virt0` columns display the number of LLC misses and energy consumption every 500 million retired instructions. Note, however, that values in the `pmc0` column do not reflect exactly the target instruction count. This has to do with the fact that, in modern processors, the PMU interrupt is not served right after the counter overflows. Instead, due to the out-of-order and speculative execution, several dozen instructions or more may be executed within the period elapsed from counter overflow until the application is actually interrupted. These inaccuracies do not pose a big problem as long as coarse instruction windows are used.   		     


### Libpmctrack

Another way of accessing PMCTrack functionality from user space is via _libpmctrack_. This library enables to characterize performance of specific code fragments via PMCs and virtual counters in sequential and multithreaded programs written in C or C++. Libpmctrack's API makes it possible to indicate the desired PMC and virtual-counter configuration to the PMCTrack's kernel module at any point in the application's code or within a runtime system. The programmer may then retrieve the associated event counts for any code snippet (via TBS or EBS) simply by enclosing the code between invocations to the `pmctrack_start_count*()` and `pmctrack_stop_count()` functions. To illustrate the use of libpmctrack, several example programs are provided in the repository under `test/test_libpmctrack`.

## PMCTrack monitoring modules

PMCTrack's kernel module can be easily extended with support for extra HW monitoring facilities not implemented in the basic PMCTrack stack. To implement such an extension a new PMCTrack _monitoring module_ must be implemented. Several sample monitoring modules are provided along with the PMCTrack distribution; its source code can be found in the `*_mm.c` files found in `src/modules/pmcs`.

From the programmer's standpoint, creating a monitoring module entails implementing the `monitoring_module_t` interface (`<pmc/monitoring_mod.h>`) in a separate .c file of the PMCTrack kernel module sources. The `monitoring_module_t` interface consists of several callback functions enabling to notify the module on activations/deactivations requested by the system administrator, on threads' context switches, every time a thread enters/exits the system, etc. The programmer typically implements only the subset of callbacks required to carry out the necessary internal processing. Notably, any kind of monitoring information accessed by the monitoring module can be exposed to PMCTrack userland tools as a _virtual counter_.

PMCTrack's kernel module also enables monitoring modules to take full control of performance monitoring counters to perform any kind of internal task. To make this possible, the monitoring module's developer does not have to deal with performance-counter registers directly. Instead, the programmer indicates the desired counter configuration (encoded in a string) using an API function. Whenever new PMC samples are collected for a thread, a callback function of the monitoring module gets invoked, passing the samples as a parameter. Thanks to this feature, a monitoring module will only access low-level registers to provide the scheduler or the end user with other hardware monitoring information not modeled as PMC events, such as temperature or energy consumption.

PMCTrack may include several monitoring modules compatible with a given platform. However, only one can be enabled at a time. Monitoring modules available for the current system can be obtained by reading from the `/proc/pmc/mm_manager` file:

	$ cat /proc/pmc/mm_manager 
	[*] 0 - This is just a proof of concept
	[ ] 1 - IPC sampling-based SF estimation model
	[ ] 2 - PMCtrack module that supports Intel CMT
	[ ] 3 - PMCtrack module that supports Intel RAPL

In the example above, four monitoring modules are listed and module #0, marked with "*", is the _active_ monitoring module. 

In the event several compatible monitoring modules exist, the system administrator may tell the system which one to use by writing in the `/proc/pmc/mm_manager` file as follows:
	
	$ echo 'activate 3' > /proc/pmc/mm_manager
	$ cat /proc/pmc/mm_manager 
	[ ] 0 - This is just a proof of concept
	[ ] 1 - IPC sampling-based SF estimation model
	[ ] 2 - PMCtrack module that supports Intel CMT
	[*] 3 - PMCtrack module that supports Intel RAPL

The `pmc-events` command can be used to list the virtual counters exported by the active monitoring module, as follows: 	

	$ pmc-events -V
	[Virtual counters]
	
	energy_pkg
	energy_dram


## Using PMCTrack from the OS scheduler

PMCTrack allows any scheduling algorithm in the Linux kernel (i.e., scheduling class) to collect per-thread monitoring data, thus making it possible to drive scheduling decisions based on tasks' memory behavior or other microarchitectural properties. Turning on this mode for a particular thread from the scheduler's code boils down to activating the `prof_enabled` flag in the thread's descriptor. This flag is added to Linux's task structure when applying PMCTrack's kernel patch. 

To ensure that the implementation of the scheduling algorithm that benefits from this feature remains architecture independent, the scheduler itself (implemented in the kernel) does not configure nor deals with performance counters directly. Instead, the active monitoring module in PMCTrack is in charge of feeding the scheduling policy with the necessary high-level performance monitoring metrics, such as a task's instruction per cycle ratio or its last-level-cache miss rate.

The scheduler can communicate with the active monitoring module to obtain per-thread data via the following function from PMCTrack's kernel API:

	int pmcs_get_current_metric_value( struct task_struct* task, int metric_id,
									   uint64_t* value );

For simplicity, each metric is assigned a numerical ID, known by the scheduler and the monitoring module. To obtain the up-to date value for a specific metric, the aforementioned function may be invoked from the tick processing function in the scheduler. 

Monitoring modules make it possible for a scheduling policy relying on PMC or virtual counter metrics to be seamlessly extended to new architectures or processor models as long as the hardware enables to collect necessary monitoring data. All that needs to be done is to build a monitoring module or adapt an existing one to the platform in question. 
