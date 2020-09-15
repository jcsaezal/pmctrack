#ifndef PMC_EDP_H
#define PMC_EDP_H
#include <pmc/mc_experiments.h>

#ifdef CONFIG_PMC_CORE_2_DUO /* For QuickIA*/
static inline int edp_initialize_environment(void)
{
	return 0;
}
static inline int edp_release_resources(void)
{
	return 0;
}
static inline void edp_pause_global_counts(void) {};
static inline void edp_resume_global_counts(void) {};
static inline void edp_reset_global_counters(void) {};
static inline void edp_update_global_instr_counter(uint64_t* thread_instr_counts) {};
static inline int edp_dump_global_counters(char* buf)
{
	return 0;
}

#else
int edp_initialize_environment(void);
int edp_release_resources(void);
void edp_pause_global_counts(void);
void edp_resume_global_counts(void);
void edp_reset_global_counters(void);
void edp_update_global_instr_counter(uint64_t* thread_instr_counts);
int edp_dump_global_counters(char* buf);
#endif


#endif