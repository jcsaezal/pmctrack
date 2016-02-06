/*
 *  include/pmc/amd/pmc_const.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef AMD_PMC_CONST_H
#define AMD_PMC_CONST_H

#define MAX_HL_EXPS 20

/* 2 PMCs + 3 FIXED COUNTERS */
#define MAX_LL_EXPS 4

/* Max number of CPUs on AMD: Biggest AMD machine we own ... */
#define NR_LOGICAL_PROCESSORS 48

#define PMC_MSR_INITIAL_ADDRESS		0xC0010004
#define EVTSEL_MSR_INITIAL_ADDRESS	0xC0010000
#define IA32_PMC0_MSR			0xC0010004
#define IA32_PMC1_MSR			0xC0010005
#define IA32_PMC2_MSR			0xC0010006
#define IA32_PMC3_MSR			0xC0010007

/* This value can be obtained  CPUID 0AH. EAX[7:0] */
#define NUM_PERFORMANCE_COUNTERS	4

#define NUM_EVENT_SELECTORS		NUM_PERFORMANCE_COUNTERS

/*
 * Basic events set of AMD Opteron processors
 * */
#define UNHALTED_CORE_CYCLES_UMASK		0x00
#define UNHALTED_CORE_CYCLES_EVTSEL		0x76
#define INST_RETIRED_UMASK			0x00
#define INST_RETIRED_EVTSEL			0xC0
#define UOPS_RETIRED_UMASK			0x00
#define UOPS_RETIRED_EVTSEL			0xC1

/* Cache events OPTERON */
#define L3_READ_REQ_EVTSEL			0x4E0
#define L3_READ_REQ_EVTSEL2			0x4	/* Extended bit for the EVTSEL */
#define L3_READ_REQ_UMASK                       0x07    /* Enable per-core bit*/
#define L3_MISS_EVTSEL	                      	0x4E1
#define L3_MISS_EVTSEL2				0x4	/* Extended bit for the EVTSEL */
#define L3_MISS_UMASK 	                        0x07    /* Enable per-core bit*/


/*
 * Macros for other MSRs associated to the performance counters.
 */
#define _TIME_STAMP_COUNTER_MSR	0x010

/*
 * Counter Length (48 bits)
 */
#define _MAX_COUNTER_VALUE		0xffffffffffff




#endif
