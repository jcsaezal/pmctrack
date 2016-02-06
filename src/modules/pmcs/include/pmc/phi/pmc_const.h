/*
 *  include/pmc/phi/pmc_const.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef PHI_PMC_CONST_H
#define PHI_PMC_CONST_H

#define MAX_HL_EXPS 20

/* This value can be obtained  CPUID 0AH. EAX[7:0] */
#define NUM_PERFORMANCE_COUNTERS	2

#define NUM_EVENT_SELECTORS		NUM_PERFORMANCE_COUNTERS

#define MAX_LL_EXPS NUM_PERFORMANCE_COUNTERS

/* Max Number of CPUs on Intel Xeon Phi */
#define NR_LOGICAL_PROCESSORS 240

#define PMC_MSR_INITIAL_ADDRESS		0x20
#define EVTSEL_MSR_INITIAL_ADDRESS	0x28

/*
 * Counter Length (40 bits)
 */
#define _MAX_COUNTER_VALUE		0xffffffffff

/* Macros extracted from the Intel MPSS kernel code */
#define KNC_ENABLE_COUNTER0			0x00000001
#define KNC_ENABLE_COUNTER1			0x00000002

#define MSR_KNC_IA32_PERF_SPFLT_CONTROL		0x0000002c
#define MSR_KNC_IA32_PERF_GLOBAL_STATUS		0x0000002d
#define MSR_KNC_IA32_PERF_GLOBAL_OVF_CONTROL	0x0000002e
#define MSR_KNC_IA32_PERF_GLOBAL_CTRL		0x0000002f


#endif
