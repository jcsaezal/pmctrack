/*
 *  include/pmc/common/msr_asm.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef COMMON_MSR_ASM_H
#define COMMON_MSR_ASM_H

#if defined(__linux__) || (defined(_DEBUG_USER_MODE) && defined(__linux__))
/* Below inline assembly code implements low level operations
	for managing PMCs on Intel Processors (IA-32 and Intel64)
	 */

/* Note that none of these macros has a trailing semicolon (";") */

/*
 * Macros for enabling and disabling usr access to RDTSC and RDPMC.
 */
#define	_ENABLE_USR_RDPMC		__asm__ __volatile__ (		/* enabled when PCE bit set */ \
					"movl %%cr4,%%eax\n\t"\
					"orl $0x00000100,%%eax\n\t"\
					"movl %%eax,%%cr4"\
					: : : "eax")

#define	_DISABLE_USR_RDPMC		__asm__ __volatile__ (		/* disabled when PCE bit clear */ \
					"movl %%cr4,%%eax\n\t"\
					"andl $0xfffffeff,%%eax\n\t"\
					"movl %%eax,%%cr4"\
					: : : "eax")

#define	_ENABLE_USR_RDTSC		__asm__ __volatile__ (		/* enabled when TSD bit clear */ \
					"movl %%cr4,%%eax\n\t"\
					"andl $0xfffffffb,%%eax\n\t"\
					"movl %%eax,%%cr4"\
					: : : "eax")

#define	_DISABLE_USR_RDTSC		__asm__ __volatile__ (		/* disabled when TSD bit set */ \
					"movl %%cr4,%%eax\n\t"\
					"orl $0x000000004,%%eax\n\t"\
					"movl %%eax,%%cr4"\
					: : : "eax")

/*
 * The general structure of the GNU asm() is
 *   "code"
 *   : output registers (with destination)
 *   : input registers (with source)
 *   : other affected registers
 *
 * For more information see 'info gcc', especially the Extended ASM discussion.
 */

/*
 * generic instruction sequence:
 *    - buf must be _uint64_t
 *    - N must be valid for the first instruction*/

#define PMC_ASM(instructions,N,buf) \
  __asm__ __volatile__ ( instructions : "=A" (buf) : "c" (N) )


/*
 * Read a model-specific register:
 *    - buf must be _uint64_t
 *    - N must be a valid MSR number from msr_arch.h
 *    - rdmsr can only be used in kernel mode
 */
#define _ASM_READ_MSR(N,buf) PMC_ASM("rdmsr",N,buf)


/*Low must be a 32-bits number*/
#define _ASM_FAST_READ_PMC(N,low) \
	__asm__ __volatile__("rdpmc" : "=a"(low) : "c"(N) : "edx")


/*
 * Write a model-specific register:
 *     - buf must be _uint64_t
 *     - N must be a valid MSR number from msr_arch.h
 *     - wrmsr can only be used in kernel mode
 */
/**  FAILS : DO NOT USE **/
#define _ASM_WRITE_MSR(N,buf) \
  __asm__ __volatile__ ( "wrmsr" : : "A" (buf), "c" (N) )

/*
 * Read the time-stamp counter:
 *     - buf must be _uint64_t
 *     - rdtsc can be enabled for usr mode via the TSD bit in CR4
 */
#define _ASM_READ_TSC(buf) \
  __asm__ __volatile__ ( "rdtsc" : "=A" (buf) )

/* Read a performance-monitoring counter:
 *    - buf must be pmc_uint64_t
 *    - N must be in the range [0,pmc_event_counters)
 *    - rdpmc can be enabled for usr mode via the PCE bit in CR4
 */
#define _ASM_READ_PMC(N,buf) PMC_ASM("rdpmc",N,buf)

/* Read a control register:
 *     - buf must be _uint32_t
 *     - N must be a valid control register number (0, 2, 3, 4)
 *     - this can only be used in kernel mode (ring 0)
 */
#define _ASM_READ_CR(N,buf) \
  __asm__ __volatile__ ( "movl %%cr" #N ",%0" : "=r" (buf) )

#define _ASM_READ_PMC_LOW(N,low) \
	__asm__ __volatile__("rdpmc" : "=a"(low) : "c"(N) : "edx")

#define _ASM_WRITE_MSR2(N,low,high) \
         __asm__ __volatile__("wrmsr" \
                           : /* no outputs */ \
                           : "c" (N), "a" (low), "d" (high))


#define _ASM_READ_PMC2(N,low,high) \
         __asm__ __volatile__("rdpmc" \
                           : "=a" (low), "=d" (high) \
                           : "c" (N) )

#define _ASM_READ_MSR2(N,low,high) \
         __asm__ __volatile__("rdmsr" \
                           : "=a" (low), "=d" (high) \
                           : "c" (N) )

#elif defined(__SOLARIS__) && defined(_DEBUG_USER_MODE)

/*
 * if we are not profiling, MCOUNT should be defined to nothing
 */
#if !defined(PROF) && !defined(GPROF) && !defined(MCOUNT)
#define	MCOUNT(x)
#endif /* !defined(PROF) && !defined(GPROF) */

#ifndef ASM_ENTRY_ALIGN
#define	ASM_ENTRY_ALIGN	16
#endif

/* Definition of entry for user-mode applications*/
#ifndef ENTRY
#define	ENTRY(x) \
	.text; \
	.align	ASM_ENTRY_ALIGN; \
	.globl	x; \
	.type	x, @function; \
x:	MCOUNT(x)

#endif

/*
 * SET_SIZE trails a function and set the size for the ELF symbol table.
 */
#ifndef SET_SIZE
#define	SET_SIZE(x) \
	.size	x, [.-x]
#endif

#include <sys/pmc/common/pmc_types.h>

uint64_t rdpmc(uint_t);
uint64_t rdmsr(uint_t);
void wrmsr(uint_t, const uint64_t);

/* Solaris usermode branch */
/*#if defined(__amd64)

	ENTRY(rdmsr)
	movl	%edi, %ecx
	rdmsr
	shlq	$32, %rdx
	orq	%rdx, %rax
	ret
	SET_SIZE(rdmsr)

	ENTRY(rdpmc)
	movl	%edi, %ecx
	rdpmc
	shlq	$32, %rdx
	orq	%rdx, %rax
	ret
	SET_SIZE(rdmsr)

	ENTRY(wrmsr)
	movq	%rsi, %rdx
	shrq	$32, %rdx
	movl	%esi, %eax
	movl	%edi, %ecx
	wrmsr
	ret
	SET_SIZE(wrmsr)

#elif defined(__i386)

ENTRY(rdmsr)
movl	4(%esp), %ecx
rdmsr
ret
SET_SIZE(rdmsr)

ENTRY(rdpmc)
movl	4(%esp), %ecx
rdmsr
ret
SET_SIZE(rdpmc)

ENTRY(wrmsr)
movl	4(%esp), %ecx
movl	8(%esp), %eax
movl	12(%esp), %edx
wrmsr
ret
SET_SIZE(wrmsr)

#endif	 */ /* __i386 */


#define	_ASM_WRITE_MSR(msr, value)					\
	wrmsr((msr), (value))

#define	_ASM_READ_MSR(msr, value)					\
	(value) = rdmsr((msr))

#define	_ASM_READ_PMC(pmcidx, value)					\
	(value) = rdpmc((pmcidx))


#else /* Solaris kernel includes */
#include <sys/sdt.h>		/* DTRACE */
#include <sys/x86_archext.h>	/* MSR OPS */

#define	_ASM_WRITE_MSR(msr, value)					\
	wrmsr((msr), (value));						\
	DTRACE_PROBE2(wrmsr, uint64_t, (msr), uint64_t, (value));

#define	_ASM_READ_MSR(msr, value)					\
	(value) = rdmsr((msr));						\
	DTRACE_PROBE2(rdmsr, uint64_t, (msr), uint64_t, (value));

#define	_ASM_READ_PMC(pmcidx, value)					\
	(value) = rdpmc((pmcidx));					\
	DTRACE_PROBE2(rdmsr, uint64_t, (pmcidx), uint64_t, (value));

#endif



#endif
