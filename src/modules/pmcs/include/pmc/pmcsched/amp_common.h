#ifndef AMP_COMMON_H
#define AMP_COMMON_H

#include <pmc/pmu_config.h>
#include <linux/rwlock.h>
#include <pmc/data_str/phase_table.h>
#include <linux/kfifo.h>
#include <pmc/intel_ehfi.h>

#ifndef AMP_CORE_TYPES
#define AMP_CORE_TYPES 2
#endif
#ifndef AMP_FAST_CORE
#define AMP_FAST_CORE 1
#endif
#ifndef AMP_SLOW_CORE
#define AMP_SLOW_CORE 0
#endif
#ifndef AMP_CORE_NONE
#define AMP_CORE_NONE -1
#endif

struct sched_app;
struct pmcsched_thread_data;
typedef int amp_coretype_t;
/* AMP pflags */
#define AMP_OMP_MASTER_THREAD	0x0010
#define AMP_FIXED_SF_ENV	0x0200
#define AMP_FIXED_EF_ENV	0x0400
#define AMP_SPINNING_THREAD 0x0001
#define AMP_SPINNING_LISTED_THREAD 0x0002	/* Spinning and enumerated on the list !! */
#define AMP_DEFAULT_SPEEDUP_FACTOR    50	// All applications get a 2x when running on a FC
#define AMP_DEFAULT_SPEEDUP_FACTOR_NORM  1500	// All applications get a 2x when running on a FC
#define AMP_DEFAULT_EFFICIENCY_FACTOR	1000 /* 1000 => 1x */
#define AMP_LBOLT  jiffies
#define AMP_TICK_VALUE_FC_HCFS	100
#define AMP_HET_DEFAULT_WEIGHT       100

// User-specified Application Types
typedef enum {
	AMP_REGULAR_APP=0,
	AMP_OPENMP_APP,
	AMP_TASKED_APP,
	AMP_BUSY_FCS_APP,
	AMP_USR_APP_MODES
} amp_usr_app_type_t;

typedef enum {
	/* slow-to-Fast candidates */
	AMP_TD_FORCED_SC=0,
	AMP_REGULAR_THREAD_SC,
	AMP_SPINNING_SC,
	AMP_MIGRATING_FROM_SC,
	/* Fast-to-Slow candidates */
	AMP_SPINNING_FC,
	AMP_REGULAR_THREAD_FC,
	AMP_TD_FORCED_FC,
	AMP_MIGRATING_FROM_FC,
	AMP_NO_CANDIDATE,
	AMP_NUM_CANDIDATE_TYPES
} amp_candidate_type;

typedef enum {
	AMP_PROP_SPEEDUP=0,
	AMP_PROP_EFFICIENCY,
	AMP_NR_PROPERTIES
} amp_property_types;

typedef struct ampproc {
	int amp_core_type; /* Core type the thread is assigned to
										(matches Sched group ID) */
	unsigned long ticks_per_coretype[AMP_CORE_TYPES]; /* Ticks consumed by the thread
															on each core type */
	amp_candidate_type	amp_candidate_type; /* Candidate type to control migrations */
	struct list_head candidate_thr_list_node; /* Link for candidates */
	struct list_head threads_in_app_coretype; /* Link for threads in app per coretype */

	unsigned int amp_pflags;	/* Custom per-thread flags field
									(must be lock protected!) */
	int t_tid;					/* Thread tid for AID */
	uint64_t het_vruntime;		/* Per thread progress counter
										(normalized to big core)*/
	unsigned long inactive_timestamp;	/* To track by how much a thread has been asleep */

	/* Speedup/Efficiency field */
	int amp_sf;	/* Instantaneous speedup factor of the thread */
	int amp_estimated_speedup; /* Instantaneous speedup (considering thread count) */
	int amp_ef;	/* Instantaneous efficiency factor of the thread (amp_estimated_ef) */
	int nthreads_app;			/* Field to keep track of the nthreads
									of an application (might change upon tick only) */
	/* Average-Smoothing value */
	unsigned int amp_avg_properties[AMP_NR_PROPERTIES];
	unsigned int amp_acum_properties[AMP_NR_PROPERTIES]; /* Cumulative counters for average */
	unsigned int amp_nr_estimates;		/* Accumulated values for the averages */
	unsigned long amp_last_avg_update; 	/* Timestamp average prediction */

	/* Phase-management fields */
	phase_table_t* phase_table;
	unsigned int phase_table_misses[AMP_CORE_TYPES];
	unsigned int phase_table_accesses[AMP_CORE_TYPES];

	/* For TD management */
	unsigned int amp_acum_td_properties[AMP_NR_PROPERTIES];
	unsigned int amp_projected_properties[AMP_NR_PROPERTIES];
	unsigned int amp_td_nr_values;		/* Accumulated values for the averages */
	struct ehfi_thread_info last_td_info; 	/* Last TD info */
	/* To control frequency of activations/deactivations in TD */
	int nr_td_samples_remaining;
	unsigned long last_time_forced_td;
} ampproc_t;

typedef struct ampapp_global {
	/* For OpenMP runtime */
	ampproc_t* 	ampt_omp_master_thread;	/* Pointer to the master thread  */
	/* For updating master thread field */
	atomic_t nr_runnable_threads; /* For lock free access to current thread count */
	rwlock_t* app_lock; /* Pointer to per-app lock for convenience */
	sized_list_t threads_per_coretype[AMP_CORE_TYPES];
	struct list_head active_applications;
	amp_usr_app_type_t ampt_usr_app_type;  /* User-defined application type */
	uint32_t ampt_het_weight_static;	/* Static weight (priority) */
	unsigned long ampt_envflags;	/* To force the SF/EEF of certain applications */
	int	ampt_static_properties[AMP_NR_PROPERTIES];	/* User-provided SF and EEF */
	int	ampt_average_sf;	/* For HSP_MT */
	phase_table_t* phase_table;	/* Per application phase table (for multithreaded apps) */
} amp_global_app_t;




#endif