/*
 *  include/pmc/corei7/pmc_const.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef COREI7_PMC_CONST_H
#define COREI7_PMC_CONST_H

#define MAX_HL_EXPS 10

/* 8 GP PMCs + 3 FIXED COUNTERS */
#define MAX_LL_EXPS 11

#define NR_LOGICAL_PROCESSORS 64

#define PMC_MSR_INITIAL_ADDRESS		0x0C1
#define EVTSEL_MSR_INITIAL_ADDRESS	0x186
#define IA32_PMC0_MSR			PMC_MSR_INITIAL_ADDRESS

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

/* FP STUFF */

#define INST_RETIRED_X87_EVTSEL		0xC0
#define INST_RETIRED_X87_UMASK		0x02

/* pmc1=0xc7,umask1=0x0f */
#define SSE_COMP_INSTR_RET_EVTSEL	0xC7
#define SSE_COMP_INSTR_RET_UMASK	0x0F

/* C2H 01H UOPS_RETIRED.ANY **/
#define UOPS_RETIRED_ANY_EVTSEL		0xC2
#define UOPS_RETIRED_ANY_UMASK		0x01

/*

0BH 01H MEM_INST_RETIRED.LOADS

Counts the number of instructions with an architecturally-visible store with retired on the architected path.

*/
#define MEM_INST_RETIRED_LOADS_EVTSEL		0x0B
#define MEM_INST_RETIRED_LOADS_UMASK		0x01

/*
0BH 02H MEM_INST_RETIRED.STORES

Counts the number of instructions with an architecturally-visible store with  retired on the architected path.
*/

#define MEM_INST_RETIRED_STORES_EVTSEL		0x0B
#define MEM_INST_RETIRED_STORES_UMASK		0x02

/*
 * Macros for other related MSRs
 */
#define _TIME_STAMP_COUNTER_MSR	0x010

#define IA32_MISC_ENABLE_ADDR	 0x1A0
#define PLATFORM_INFO_MSR_ADDR	0x0ce
#define IA32_CLOCK_MODULATION_ADDR		0x19A


#endif
