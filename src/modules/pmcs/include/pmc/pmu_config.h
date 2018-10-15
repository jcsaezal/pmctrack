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
#include <pmc/mc_experiments.h>

#define PMC_CORE_TYPES 2 				/* Max core types supported */
#define PMCTRACK_MODEL_STRING_LEN 30
#define PMCTRACK_MAX_LEN_RAW_PMC_STRING 256
#define PMCTRACK_MAX_PMU_FLAGS 15

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
	unsigned int cfg_evtsel;
	unsigned char cfg_usr;
	unsigned char cfg_os;
	uint64_t cfg_reset_value;
	unsigned char cfg_ebs_mode;
#if defined(CONFIG_PMC_CORE_2_DUO) || defined(CONFIG_PMC_AMD) || defined(CONFIG_PMC_CORE_I7) || defined(CONFIG_PMC_PHI)
	/* extra fields for x86 platforms */
	unsigned int cfg_umask;
	unsigned char cfg_edge;
	unsigned char cfg_inv;
	unsigned char cfg_any;
	unsigned int cfg_cmask;
#endif
} pmc_usrcfg_t;

#if defined(CONFIG_PMC_CORE_2_DUO) || defined(CONFIG_PMC_AMD) || defined(CONFIG_PMC_CORE_I7) || defined(CONFIG_PMC_PHI)

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


/**************************************************************************
 * Functions enabling the PMCTrack platform-independent core (mchw_core.c)
 * to interact with the platform-specific code.
 *
 * (The implementation of most of these functions is platform specific)
 **************************************************************************/

/* Initialize the PMUs of the various CPUs in the system  */
int init_pmu(void);

/* Stop PMUs and free up resources allocated by init_pmu() */
int pmu_shutdown(void);

/* Reset the PMC overflow register (if the platform is provided with such a register)
 * in the current CPU where the function is invoked.
 */
void reset_overflow_status(void);

/*
 * Return a bitmask specifiying which PMCs overflowed
 * This bitmask must be in a platform-independent "normalized" format.
 * bit(i,mask)==1 if pmc_i overflowed
 * (bear in mind that lower PMC ids are reserved for
 * fixed-function PMCs)
 */
unsigned int read_overflow_mask(void);

/*
 * This function gets invoked from the platform-specific PMU code
 * when a PMC overflow interrupt is being handled. The function
 * takes care of reading the performance counters and pushes a PMC sample
 * into the samples buffer when in EBS mode.
 */
void do_count_on_overflow(struct pt_regs *reg, unsigned int overflow_mask);

/*
 * Transform an array of platform-agnostic PMC counter configurations (pmc_cfg)
 * into a low level structure that holds the necessary data to configure hardware counters.
 */
void do_setup_pmcs(pmc_usrcfg_t* pmc_cfg, int used_pmcs_msk,core_experiment_t* exp, int cpu, int exp_idx);

/*
 * Fill the various fields in a pmc_cfg structure based on a PMC configuration string specified in raw format
 * (e.g., pmc0=0xc0,pmc1=0x76,...). The associated processing is platform specific and also provides
 * the caller with the number of pmcs used, a bitmask with the set of PMCs claimed by the
 * raw string, and other relevant information for mchw_core.c.
 *
 * This function returns 0 on success, and a negative value if errors were detected when
 * processing the raw-formatted string.
 */
int parse_pmcs_strconfig(const char *buf,
                         unsigned char ebs_allowed,
                         pmc_usrcfg_t* pmc_cfg,
                         unsigned int* used_pmcs_mask,
                         unsigned int* nr_pmcs,
                         int *ebs_index,
                         int *coretype);


/*
 * Perform a default initialization of all performance monitoring counters
 * in the current CPU. The PMU properties are passed as a parameter for
 * efficiency reasons
 */
void mc_clear_all_platform_counters(pmu_props_t* props_cpu);

/*
 * Generate a summary string with the configuration of a hardware counter (lle)
 * in human-readable format. The string is stored in buf.
 */
int print_pmc_config(low_level_exp* lle, char* buf);

#ifdef DEBUG
int print_pmu_msr_values_debug(char* line_out);
#endif







#endif
