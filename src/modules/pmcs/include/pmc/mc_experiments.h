/*
 *  include/pmc/mc_experiments.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef MC_EXPERIMENTS_H
#define MC_EXPERIMENTS_H
#include <pmc/ll_events.h>
#if defined (__linux__)
#include <linux/types.h>
#else
#include <sys/types.h>
#endif

/* Divide in 32bit mode */
#if defined(__i386__) || defined(__arm__)
#define pmc_do_div(a,b) do_div(a,b)
#else
#define pmc_do_div(a,b) (a)=(a)/(b)
#endif

#define MAX_32_B 0xffffffff
#define AMP_MAX_EXP_CORETYPE	5
#define AMP_MAX_CORETYPES	2
#define PMC_NEW_THREAD  (CLONE_FS| CLONE_FILES | CLONE_VM| \
CLONE_SIGHAND| CLONE_THREAD )
#define is_new_thread(flags) ((flags & PMC_NEW_THREAD)==PMC_NEW_THREAD)
#define TBS_TIMER

#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/slab.h> /* For kmalloc() */
#include <asm/atomic.h>
#include <pmc/pmc_user.h> /*For the data type */
#include <pmc/data_str/cbuffer.h>
#include <linux/spinlock.h>
#include <linux/timer.h>

/**************** Monitoring experiments ********************************/

/*
 * Structure to store platform-specific configuration for a set of hardware events
 */
typedef struct {
	low_level_exp	array[MAX_LL_EXPS];	/* HW counters opaque descriptor */
	unsigned int	size;				/* Number of HW counters used for this event */
	unsigned int	used_pmcs;			/* PMC mask used */
	int 		ebs_idx;				/* A -1 value means that ebs is disabled.
									 	 * Otherwise it stores the ll_exp ID
									 	 * for which ebs is enabled */
	unsigned char need_setup; 			/* Non-zero if a first-time PMCs configuration
										 * needs to be done */
	int exp_idx;						/* Exp index (for event multiplexing) */
	/* Structures to control PMC counter overflow in EBS mode */
	unsigned int log_to_phys[MAX_LL_EXPS];	/* Table to obtain the physical PMC id
											 * from a low-level_exp (logical) ID
											 */
	unsigned int phys_to_log[MAX_LL_EXPS];	/* Table to obtain the low-level_exp (logical) ID
											 * from a physical PMC id
											 */
	unsigned int nr_overflows[MAX_LL_EXPS];	/* Overflow counts for each low_level_exp */
}
core_experiment_t;

/*
 * Set of core_experiment_t structures.
 * This is the basic structure to support the event multiplexing feature:
 * several sets of HW events are monitored in a round-robin fashion.
 */
typedef struct {
	core_experiment_t* exps[AMP_MAX_EXP_CORETYPE];
	int nr_exps;
	int cur_exp;	/* Contador modular entre 0 y nr_exps */
} core_experiment_set_t;

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


/*** Platform-Independent datatypes of PMCTrack's kernel module */

typedef enum {
	TBS_SCHED_MODE,		/* Scheduler timed-based sampling */
	TBS_USER_MODE,		/* User-requested timed-based sampling */
	EBS_MODE			/* User-requested event-based sampling */
} pmc_profiling_mode_t;

/*
 * SMP-safe data structure to store
 * PMC samples and virtual counter values.
 *
 * The pmctrack program interacts with the kernel
 * by retrieving samples from this data structure.
 */
typedef struct {
	cbuffer_t* pmc_samples; 		/* Ring buffer */
	spinlock_t lock;				/* Spin lock to serialize accesses to this data structure */
	struct semaphore sem_queue;		/* Semaphore for blocking the monitor program */
	volatile int monitor_waiting;	/* Flag to indicate that the monitor is waiting
										for new samples */
	atomic_t ref_counter;			/*
									 * Reference counter for this object. It reflects
									 * the number of processes/threads that hold a
									 * reference to the same instance of the data structure.
									 *
									 * The kernel frees up the memory of the object when
									 * the ref counter reaches 0.
									 */
} pmc_samples_buffer_t;

/* Predeclaration for monitoring_module type */
struct monitoring_module;

/*
 * Per-thread data structure maintained
 * by PMCTrack's kernel module
 */
typedef struct {
	uint64_t pmc_values[MAX_LL_EXPS]; 	/* Accumulator for PMC values */
	struct task_struct *this_tsk;		/* Backwards pointer to the task struct of this thread */
	unsigned int pmc_ticks_counter; 	/* Sampling interval control field for TBS.
	                                     * Saturated counter (no reset on context switch).
	                                     */
	unsigned int samples_counter;   	/* The number of PMC samples collected for the thread */
	int pmc_jiffies_interval;			/* TBS sampling period length (in jiffies) */
	unsigned long pmc_jiffies_timeout;	/* Timestamp to read performance and virtual counters */
	core_experiment_t* pmcs_config;		/* Current PMC configuration in use */
	core_experiment_set_t pmcs_multiplex_cfg[AMP_MAX_CORETYPES]; /* Per-thread PMCs configuration
																  * (May include many experiments -> support for event multiplexing
																  */
	int last_cpu;							/* Field to detect migrations */
	pmc_profiling_mode_t profiling_mode; 	/* Sampling mode selected for the current thread */
	unsigned long context_switch_timestamp; /* Timestamp of the last context switch */
	unsigned long flags;					/* Per-process flags (defined right below) */
	uint_t virt_counter_mask;				/* Virtual counter mask */
#ifdef TBS_TIMER
	struct timer_list timer;				/* Timer used in TBS mode */
#endif
	spinlock_t lock;					/* Lock for PMC experiments */
	pid_t pid_monitor;					/* PID of the monitor process */
	pmc_sample_t* pmc_user_samples;			/* Intermediate buffer to transfer data from kernel space
	 								         * to the virtual address space of the monitor process
	 								         * (Allocated on first use)
	 								         */
	pmc_sample_t* pmc_kernel_samples;		/* Shared memory region between user and kernel space!! */
	pmc_samples_buffer_t* pmc_samples_buffer; /* Buffer shared between monitor process and threads being monitored */
	uint_t nticks_sampling_period;			/* Scheduler-mode tick-based sampling period */
	uint_t  kernel_buffer_size;				/* Max capacity (in bytes) of the ring buffer in "pmc_samples_buffer" */
	struct monitoring_module* task_mod;		/* Pointer to the monitoring module assigned to this task */
	void* 	monitoring_mod_priv_data;		/* Per-thread private data for current monitoring module */
} pmon_prof_t;

/** Various flag values for the "flags" field in pmon_prof_t ***/
#define PMC_EXITING	0x1
#define PMC_SELF_MONITORING	0x2
#define PMCTRACK_SF_NOTIFICATIONS 0x4
#define PMC_PREPARE_MULTIPLEXING	0x8
#define PMC_READ_SELF_MONITORING 0x10

/** Operations on core_experiment_t **/
/* Initialize core_experiment_t structure */
void init_core_experiment_t(core_experiment_t* core_experiment, int exp_idx);

/* Restart PMCs used by a core_experiment_t */
void mc_restart_all_counters(core_experiment_t* core_experiment);

/* Stop PMCs used by a core_experiment_t */
void mc_stop_all_counters(core_experiment_t* core_experiment);

/* Clear PMCs used by a core_experiment_t */
void mc_clear_all_counters(core_experiment_t* core_experiment);

/* Save context associated with PMCs (context switch out) */
void mc_save_all_counters(core_experiment_t* core_experiment);

/* Restore context associated with PMCs (context switch in) */
void mc_restore_all_counters(core_experiment_t* core_experiment);

/*
 * Given a null-terminated array of raw-formatted PMC configuration
 * string, store the associated low-level information into an array of core_experiment_set_t.
 */
int configure_performance_counters_set(const char* strconfig[], core_experiment_set_t pmcs_multiplex_cfg[], int nr_coretypes);


/*
 * Monitor resets the Global PMU context (per CPU) ==> for 'old' events
 */
void restore_context_perfregs ( core_experiment_t* core_experiment);


/**** Operations on core experiment set_t ****/

/* Free up memory from a set of PMC experiments */
static inline void free_experiment_set(core_experiment_set_t* cset)
{
	int i=0;

	for (i=0; i<cset->nr_exps; i++) {
		kfree(cset->exps[i]);
		cset->exps[i]=NULL;
	}

	cset->nr_exps=0;
}

/* Add one PMC experiment to a given experiment set */
static inline void add_experiment_to_set(core_experiment_set_t* cset, core_experiment_t* exp)
{
	/* Modifies the exp_idx with the position in the vector !!! */
	exp->exp_idx=cset->nr_exps;
	cset->exps[cset->nr_exps++]=exp;
}

/* Rewind the pointer for event multiplexing */
static inline core_experiment_t* rewind_experiments_in_set(core_experiment_set_t* cset)
{
	cset->cur_exp=0;
	return cset->exps[0];
}

/* Select the next PMC experiment from the multiplexing event set */
static inline core_experiment_t* get_next_experiment_in_set(core_experiment_set_t* cset)
{
	if (cset->nr_exps>1) {
		cset->cur_exp++;

		if (cset->cur_exp==cset->nr_exps)
			cset->cur_exp=0;
	}
	return cset->exps[cset->cur_exp];
}

/* Return PMC configuration of the current event set being monitored */
static inline core_experiment_t* get_cur_experiment_in_set(core_experiment_set_t* cset)
{
	return cset->exps[cset->cur_exp];
}

/* Initialize a set of multiplexing experiments (empty set by default) */
static inline void init_core_experiment_set_t(core_experiment_set_t* cset)
{
	int i=0;

	for (i=0; i<AMP_MAX_EXP_CORETYPE; i++)
		cset->exps[i]=NULL;

	cset->nr_exps=0;
	cset->cur_exp=0;
}

/* Copy the configuration of a experiment set into another */
static inline int clone_core_experiment_set_t(core_experiment_set_t* dst,core_experiment_set_t* src)
{
	int i=0,j=0;
	core_experiment_t* exp=NULL;

	dst->nr_exps=0;
	dst->cur_exp=0;

	for (i=0; i<src->nr_exps; i++) {
		if (src->exps[i]!=NULL) {
			exp= (core_experiment_t*) kmalloc(sizeof(core_experiment_t), GFP_KERNEL);
			if (!exp)
				goto free_allocated_experiments;
			memcpy(exp,src->exps[i],sizeof(core_experiment_t));
			/* Do not inherit overflow counters */
			for (j=0; j<MAX_LL_EXPS; j++)
				exp->nr_overflows[j]=0;
			dst->exps[i]=exp;
			dst->nr_exps++;
		}

	}

	return 0;
free_allocated_experiments:
	for (i=0; i<dst->nr_exps; i++) {
		kfree(dst->exps[i]);
		dst->exps[i]=NULL;
	}
	return -ENOMEM;
}

/* Copy the configuration of a experiment set into another */
static inline int clone_core_experiment_set_t_noalloc(core_experiment_set_t* dst,core_experiment_set_t* src)
{
	int i=0,j=0;

	dst->nr_exps=0;
	dst->cur_exp=0;

	for (i=0; i<src->nr_exps; i++) {
		if (src->exps[i]!=NULL) {
			/* Just copy the pointer ... */
			dst->exps[i]=src->exps[i];
			/* Do not inherit overflow counters */
			for (j=0; j<MAX_LL_EXPS; j++)
				dst->exps[i]->nr_overflows[j]=0;
			dst->nr_exps++;
		}

	}
	return 0;
}


/**** Operations on a PMC sample buffer ****/

/*
 * Allocate a buffer with capacity 'size_bytes'
 * The function returns a non-null value on success.
 */
pmc_samples_buffer_t* allocate_pmc_samples_buffer(unsigned int size_bytes);

/* Increment the buffer's reference counter */
static inline void get_pmc_samples_buffer(pmc_samples_buffer_t* sbuf)
{
	atomic_inc(&sbuf->ref_counter);
}

/* Return the buffer's reference counter */
static inline int get_pmc_samples_buffer_refs(pmc_samples_buffer_t* sbuf)
{
	return atomic_read(&sbuf->ref_counter);
}

/* Decrement the buffer's reference counter */
static inline void put_pmc_samples_buffer(pmc_samples_buffer_t* sbuf)
{
	if (atomic_dec_and_test(&sbuf->ref_counter)) {
		destroy_cbuffer_t(sbuf->pmc_samples);
		sbuf->pmc_samples=NULL;
		kfree(sbuf);
	}
}

/*
 * Pushes a sample (PMC counts and virtual-counter values) into the buffer and
 * notifies the userspace program if necessary.
 *
 * The function must be invoked with the buffer's lock held.
 */
static inline void __push_sample_cbuffer(pmc_samples_buffer_t* sbuf, pmc_sample_t* sample)
{

	insert_items_cbuffer_t (sbuf->pmc_samples, sample, sizeof(pmc_sample_t));

	if (sbuf->monitor_waiting) {
		sbuf->monitor_waiting=0;
		up(&sbuf->sem_queue);
	}
}

/*
 * Pushes a sample (PMC counts and virtual-counter values) into the buffer.
 *
 * The function must be invoked with the buffer's lock held.
 */
static inline void __push_sample_cbuffer_nowakeup(pmc_samples_buffer_t* sbuf, pmc_sample_t* sample)
{
	insert_items_cbuffer_t (sbuf->pmc_samples, (const char*) sample, sizeof(pmc_sample_t));
}

/*
 * Wake up the userspace monitor process so that it can retrieve
 * values from the buffer of samples.
 *
 * The function must be invoked with the buffer's lock held.
 */
static inline void __wake_up_monitor_program(pmc_samples_buffer_t* sbuf)
{
	if (sbuf->monitor_waiting) {
		sbuf->monitor_waiting=0;
		up(&sbuf->sem_queue);
	}
}

/* SMP-safe version of __push_sample_cbuffer() */
static inline void push_sample_cbuffer(pmon_prof_t* prof,pmc_sample_t* sample)
{

	unsigned long flags;

	/* Make sure that the user allocated a buffer */
	if (!prof->pmc_samples_buffer)
		return;

	/* Push current counter values into the buffer */
	spin_lock_irqsave(&prof->pmc_samples_buffer->lock,flags);
	__push_sample_cbuffer(prof->pmc_samples_buffer,sample);
	spin_unlock_irqrestore(&prof->pmc_samples_buffer->lock,flags);
}


#endif
