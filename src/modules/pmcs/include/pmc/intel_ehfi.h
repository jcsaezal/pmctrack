/*
 *  include/pmc/intel_ehfi.c
 *
 *  Header file with support for Intel's Enhanced Hardware Feedback interface
 *
 *  Copyright (c) 2022 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef PMC_INTEL_EHFI_H
#define PMC_INTEL_EHFI_H
#include <linux/errno.h>
#include <linux/types.h>
#include <pmc/pmu_config.h>


struct ehfi_thread_info {
	int ehfi_class;
	unsigned int perf;	/* Scaled to 1024 == Raw 255 */
	unsigned int eef;
	unsigned int perf_ratio; /* 1000 == 1x , 2000 == 2x */
	unsigned int eef_ratio;
};


#if defined(CONFIG_PMC_PERF_X86) || defined(CONFIG_PMC_CORE_I7)
int intel_ehfi_initialize(void);
void intel_ehfi_release(void);
void intel_ehfi_process_event(void);
void enable_ehfi_thread(void);
void disable_ehfi_thread(void);
static inline void reset_ehfi_thread_history(void)
{
	hreset(0x1);
}

int get_current_ehfi_info(unsigned int last_class, struct ehfi_thread_info* tinfo);
int get_current_ehfi_class(void);
#else
static inline int intel_ehfi_initialize(void)
{
	return -ENOTSUPP;
}
static inline void intel_ehfi_release(void) {}
static inline void intel_ehfi_process_event(void) {}
static inline void enable_ehfi_thread(void) {}
static inline void disable_ehfi_thread(void) {}
static inline void reset_ehfi_thread_history(void) { }
static inline int get_current_ehfi_class(void)
{
	return -1;
}
#endif
#endif