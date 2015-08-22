/*
 *  include/pmc/core2duo/pmc_const.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 * 
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef CORE2DUO_PMC_CONST_H
#define CORE2DUO_PMC_CONST_H

#define MAX_HL_EXPS 20

/* 2 GP PMCs + 3 FIXED-FUNCTION COUNTERS */
#define MAX_LL_EXPS 5

/* The biggest old Intel Machine we own (Harpertown) */
#define NR_LOGICAL_PROCESSORS 8

#define PMC_MSR_INITIAL_ADDRESS		0x0C1
#define EVTSEL_MSR_INITIAL_ADDRESS	0x186
#define IA32_PMC0_MSR			0x0C1
#define IA32_PMC1_MSR			0x0C2

/* This value can be obtained  CPUID 0AH. EAX[7:0] */
#define NUM_PERFORMANCE_COUNTERS	2

#define NUM_EVENT_SELECTORS		NUM_PERFORMANCE_COUNTERS

/*
 *
 * MSRs related to performance monitoring
 *
 */
#define MSR_PERF_FIXED_CTR0		0x309  /* Count Instr Retired */
#define MSR_PERF_FIXED_CTR1		0x30A  /* Count Core Clock Cycles*/
#define MSR_PERF_FIXED_CTR2		0x30B  /* Count Bus Clock Cycles*/
#define MSR_PERF_FIXED_CTR_CTRL		0x38D
#define NUM_FIXED_COUNTERS		3	/* Number of fixed counters */
#define MSR_PERF_GLOBAL_STATUS		0x38E
#define MSR_PERF_GLOBAL_CTRL		0x38F
#define	MSR_PERF_GLOBAL_OVF_CTRL	0x390
#define	IA32_PEBS_ENABLE_MSR		0x3F1
#define	IA32_MISC_ENABLE_MSR		0x1A0

/*
 * Basic events set of Intel Core Microarchitecture
 * */
#define UNHALTED_CORE_CYCLES_UMASK		0x00
#define UNHALTED_CORE_CYCLES_EVTSEL		0x3C
#define INST_RETIRED_UMASK			0x00
#define INST_RETIRED_EVTSEL			0xC0
#define UNHALTED_REF_CYCLES_UMASK		0x01
#define UNHALTED_REF_CYCLES_EVTSEL		0x3C
#define LLC_REFERENCE_UMASK			0x4F
#define LLC_REFERENCE_EVTSEL			0x2E
#define LLC_MISSES_UMASK			0x41
#define LLC_MISSES_EVTSEL			0x2E
#define BRANCH_INST_RETIRED_UMASK		0x00
#define BRANCH_INST_RETIRED_EVTSEL		0xC4
#define BRANCH_MISS_RETIRED_UMASK		0x00
#define BRANCH_MISS_RETIRED_EVTSEL		0xC5


/*
 * Other events
 * */

/*  This event indicates that a pending L2
      	cache request that requires a bus
        transaction is delayed from moving to the
    	bus queue. Some of the reasons for this
    	event are:
    		• The bus queue is full.
    		• The bus queue already holds an entry
      			for a cache line in the same set.
    	The number of events is greater or equal
    	to the number of requests that were
   	rejected.
    		• for this core or both cores.
    		• due to demand requests and L2
      		 	hardware prefetch requests together, or
      			separately.
    		• of accesses to cache lines at different
      			MESI states.
*/

#define L2_REJECTED_BUSQ			0x30


/*
 * Macros for other MSRs associated to the performance counters.
 */
#define _TIME_STAMP_COUNTER_MSR	0x010

/*
 * Counter Length (40 bits)
 */
#define _MAX_COUNTER_VALUE		0xffffffffff


/*
   0x1A0 (416)
	IA32_MISC_ENABLE   Enable Misc. Processor Features. (R/W)
                            Allows a variety of processor functions to be
                                   enabled and disabled.
                0          Thread  Fast-Strings Enable. see Table B-2
                2:1                Reserved.
                                   Automatic Thermal Control Circuit Enable.
                3          Thread
                                   (R/W) see Table B-2
                6:4                Reserved.
                7          Thread  Performance Monitoring Available. (R) see
                                   Table B-2
                10:8               Reserved.
                11         Thread  Branch Trace Storage Unavailable. (RO) see
                                   Table B-2
                12         Thread  Precise Event Based Sampling Unavailable.
                                   (RO) see Table B-2
                15:13              Reserved.
                16         Package Enhanced Intel SpeedStep Technology
                                   Enable. (R/W) see Table B-2
                18         Thread  ENABLE MONITOR FSM. (R/W) see Table B-2
                21:19              Reserved.
                22         Thread  Limit CPUID Maxval. (R/W) see Table B-2
                23         Thread  xTPR Message Disable. (R/W) see Table B-2
                33:24              Reserved.
                34         Thread    XD Bit Disable. (R/W) see Table B-2
                37:35                      Reserved.
                38         Package   Turbo Mode Disable. (R/W)
                                          When set to 1 on processors that support Intel
                                          Turbo Boost Technology, the turbo mode
                                          feature is disabled and the IDA_Enable feature
                                          flag will be clear (CPUID.06H: EAX[1]=0).
                                          When set to a 0 on processors that support
                                          IDA, CPUID.06H: EAX[1] reports the
                                          processor’s support of turbo mode is enabled.
                                          Note: the power-on default value is used by
                                          BIOS to detect hardware support of turbo
                                          mode. If power-on default value is 1, turbo
                                          mode is available in the processor. If power-on
                                          default value is 0, turbo mode is not available.
               63:39       Reserved.

*/

#define IA32_MISC_ENABLE_ADDR	 0x1A0
#define PLATFORM_INFO_MSR_ADDR	0x0ce
#define IA32_CLOCK_MODULATION_ADDR		0x19A



#endif
