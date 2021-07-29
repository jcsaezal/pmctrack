/*
 *  include/pmc/pmu_config.h
 *
 *  Data types and functions enabling the PMCTrack platform-independent
 *	core (mchw_core.c) to interact with platform-specific code
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef PMU_CONFIG_H
#define PMU_CONFIG_H
#include <linux/version.h>
#include <linux/types.h>

#define PMC_CORE_TYPES 2 				/* Max core types supported */
#define PMCTRACK_MODEL_STRING_LEN 30
#define PMCTRACK_MAX_LEN_RAW_PMC_STRING 256
#define PMCTRACK_MAX_PMU_FLAGS 15

#if defined CONFIG_PMC_PERF
#if defined(__i386__) || defined (__amd64__) || defined(_x86_64_)
#define CONFIG_PMC_PERF_X86
#elif defined(__aarch64__)
#define CONFIG_PMC_PERF_ARM
#define CONFIG_PMC_PERF_ARM64
#elif defined (__arm__)
#define CONFIG_PMC_PERF_ARM
#define CONFIG_PMC_PERF_ARM32
#endif
#endif


/* Flag to be specified in the raw PMC string format */
typedef struct {
	char* name;
	int bitwidth;
	unsigned char global;
} pmu_flag_t;

/*
 * Structure that holds the properties
 * of a performance monitoring unit (PMU).
 * If the system features two or more core types
 * several pmu_props_t structures (one per PMU)
 * exist.
 */
typedef struct {
	int nr_fixed_pmcs;				/* Number of fixed-function PMCs */
	int nr_gp_pmcs;					/* Number of general-purpose PMCs */
	int pmc_width;					/* Bit width of each PMC */
	uint64_t pmc_width_mask;		/* Bitmask with pmc_width consecutive 1s */
	unsigned long processor_model;	/* Procesor model integer code (meaning is platform specific) */
	unsigned int coretype;			/* Number of core type or PMU id for this PMU */
	char arch_string[PMCTRACK_MODEL_STRING_LEN];	/* Procesor model string (PMCTrack-specific format) */
	pmu_flag_t flags[PMCTRACK_MAX_PMU_FLAGS];		/* Set of configuration flags to be specified for each PMC or experiment */
	unsigned int nr_flags;							/* Number of flags in the array */
} pmu_props_t;

/*
 * The pmc_usrcfg_t data type holds the various flag values
 * found in a performance monitoring counter.
 * (The definition of this structure is platform specific)
 */
typedef struct {
#ifdef CONFIG_PMC_PERF
	uint64_t cfg_evtsel;
#else
	unsigned int cfg_evtsel;
#endif
	unsigned char cfg_usr;
	unsigned char cfg_os;
	uint64_t cfg_reset_value;
	unsigned char cfg_ebs_mode;
#if defined(CONFIG_PMC_CORE_2_DUO) || defined(CONFIG_PMC_AMD) || defined(CONFIG_PMC_CORE_I7) || defined(CONFIG_PMC_PHI)  || defined(CONFIG_PMC_PERF)
	/* extra fields for x86 platforms */
	unsigned char cfg_umask;
	unsigned char cfg_edge;
	unsigned char cfg_inv;
	unsigned char cfg_any;
	unsigned char cfg_cmask;
#endif
#if defined (CONFIG_PMC_PERF)
	int cfg_pmu; /* PMU ID as stated in /sys/bus/event_source/devices/<pmu_type>/type */
	unsigned char cfg_systemwide;	/* whether the event is systemwide or not */
	/* config1 and config1 are used for setting events that need
		an extra register or otherwise do not fit in the regular config field
		See "man 2 perf_event_open" for more information
	 */
	uint64_t cfg_config1;
	uint64_t cfg_config2;
	unsigned char cfg_force_enable; /* For inheritance in MT apps */
#endif
} pmc_usrcfg_t;

#if defined(CONFIG_PMC_CORE_2_DUO) || defined(CONFIG_PMC_AMD) || defined(CONFIG_PMC_CORE_I7) || defined(CONFIG_PMC_PHI) || defined(CONFIG_PMC_PERF)

/*** CPUID-related datatypes and macros ***/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
#include <asm/processor.h>
typedef struct cpuid_regs cpuid_regs_t;
#define run_cpuid(r) __cpuid(&(r).eax,&(r).ebx,&(r).ecx,&(r).edx);
#else
typedef struct cpuid_regs {
	uint32_t eax;
	uint32_t ebx;
	uint32_t edx;
	uint32_t ecx;
} cpuid_regs_t;

#define run_cpuid(rv) asm ("cpuid" : "=a" (rv.eax), "=b" (rv.ebx), "=c" (rv.ecx), "=d" (rv.edx): "a"  (rv.eax), "c" (rv.ecx));
#endif

#endif

/* (int,string) pair that identifies a given processor model */
struct pmctrack_cpu_model {
	unsigned int model;
	char* model_str;
};

/* Array of pmu_props_t for each PMU detected in the machine */
extern pmu_props_t pmu_props_cputype[PMC_CORE_TYPES];

/* Load extensions for the QuickIA prototype system */
#ifdef CONFIG_PMC_CORE_2_DUO
#define PMCTRACK_QUICKIA
#endif

extern int* coretype_cpu;
extern int  nr_core_types;

#ifdef SCHED_AMP
#include <linux/amp_core.h>
#elif defined(PMCTRACK_QUICKIA)
static int coretype_cpu_quickia[]= {0,0,1,1,0,0,1,1}; /* Test machine with 8 cores !! */
static inline int get_nr_coretypes(void)
{
	return 2;
}
static inline int get_coretype_cpu(int cpu)
{
	return coretype_cpu_quickia[cpu];
}
static inline int get_any_cpu_coretype(int coretype)
{
	return coretype==1?3:1;
}
#else /* Default implementation */
static inline int get_nr_coretypes(void)
{
	return nr_core_types;
}
static inline int get_coretype_cpu(int cpu)
{
	return coretype_cpu[cpu];
}
int get_any_cpu_coretype(int coretype);
#endif

/*
 * Returns the pmu_props_t associated to a given PMU
 * (PMU id a passed as parameter)
 */
static inline pmu_props_t* get_pmu_props_coretype(int coretype)
{
	return &pmu_props_cputype[coretype];
}

/*
 * Returns the pmu_props_t associated to a given CPU
 * (CPU id a passed as parameter)
 */
static inline pmu_props_t* get_pmu_props_cpu(int cpu)
{
	return &pmu_props_cputype[get_coretype_cpu(cpu)];
}

/* Get the total PMC count available in a certain core type (PMU id) */
static inline int get_nr_performance_counters(int coretype)
{
	pmu_props_t* props=&pmu_props_cputype[coretype];
	return props->nr_fixed_pmcs+props->nr_gp_pmcs;
}



#endif
