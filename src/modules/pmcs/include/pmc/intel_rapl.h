#ifndef PMC_INTEL_RAPL_H
#define PMC_INTEL_RAPL_H
#include <linux/types.h>
#include <pmc/pmc_user.h>
#include <pmc/monitoring_mod.h>

/* Possible RAPL domains */
enum rapl_domains {RAPL_PP0_DOMAIN=0,RAPL_PP1_DOMAIN,RAPL_PKG_DOMAIN,RAPL_DRAM_DOMAIN, RAPL_NR_DOMAINS};

/* Structure to keep track of global INTEL RAPL parameters ç
	to be exposed to a client module*/
typedef struct {
	int nr_available_power_domains;
	unsigned char available_power_domains_mask;
	char* available_vcounters[MAX_VIRTUAL_COUNTERS]; /* Null terminated ... */
	unsigned int power_units;
	unsigned int energy_units;
	unsigned int dram_energy_units;
	unsigned int time_units;
} intel_rapl_support_t;

typedef struct {
	uint_t cur_domain_value[RAPL_NR_DOMAINS];
	uint64_t acum_power_domain[RAPL_NR_DOMAINS];
} intel_rapl_control_t;


/* Sample data (already scaled values) */
typedef struct {
	uint64_t energy_value[RAPL_NR_DOMAINS];
} intel_rapl_sample_t;


#if defined(CONFIG_PMC_AMD)

static inline int intel_rapl_probe(void)
{
	return 0;
}
static inline int intel_rapl_initialize(intel_rapl_support_t* rapl_support, int use_timer)
{
	return 0;
}
static inline int intel_rapl_release(intel_rapl_support_t* rapl_support)
{
	return 0;
}
static inline int intel_rapl_print_energy_units(char* str, intel_rapl_support_t* rapl_support)
{
	return 0;
}
static inline void intel_rapl_update_energy_values(intel_rapl_support_t* rapl_support, int acum) {}
static inline void intel_rapl_reset_energy_values(intel_rapl_support_t* rapl_support) {}
static inline void intel_rapl_get_energy_sample(intel_rapl_support_t* rapl_support, intel_rapl_sample_t* sample) {};
static inline intel_rapl_support_t* get_global_rapl_handler(void)
{
	return NULL;
}
static inline void intel_rapl_control_init(intel_rapl_control_t* rapl_control) {}
static inline void intel_rapl_update_energy_values_thread(intel_rapl_support_t* rapl_support,
        intel_rapl_control_t* rapl_control,
        int acum) {}
static inline void intel_rapl_get_energy_sample_thread(intel_rapl_support_t* rapl_support,
        intel_rapl_control_t* rapl_control,
        intel_rapl_sample_t* sample) {}
#else
/* Check if this processor does support Intel RAPL */
int intel_rapl_probe(void);

/* Initialize/Release RAPL resources */
int intel_rapl_initialize(intel_rapl_support_t* rapl_support, int use_timer);
int intel_rapl_release(intel_rapl_support_t* rapl_support);


/* For debugging purposes ... */
int intel_rapl_print_energy_units(char* str, intel_rapl_support_t* rapl_support);

/* Main functions to interact with RAPL facillities */
/* GLOBAL */
void intel_rapl_update_energy_values(intel_rapl_support_t* rapl_support, int acum);
void intel_rapl_reset_energy_values(intel_rapl_support_t* rapl_support);
void intel_rapl_get_energy_sample(intel_rapl_support_t* rapl_support, intel_rapl_sample_t* sample);
/* Only EDP */
intel_rapl_support_t* get_global_rapl_handler(void);

/* PER-THREAD */
void intel_rapl_control_init(intel_rapl_control_t* rapl_control);
void intel_rapl_update_energy_values_thread(intel_rapl_support_t* rapl_support,
        intel_rapl_control_t* rapl_control,
        int acum);
void intel_rapl_get_energy_sample_thread(intel_rapl_support_t* rapl_support,
        intel_rapl_control_t* rapl_control,
        intel_rapl_sample_t* sample);
#endif
#endif
