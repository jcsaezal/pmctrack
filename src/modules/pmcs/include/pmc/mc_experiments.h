/*
 *  include/pmc/mc_experiments.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 *
 *  Adaptation of the architecture-independent code for patchless PMCTrack
 *  by Lazaro Clemen and Juan Carlos Saez
 *  (C) 2021 Lazaro Clemen Palafox <lazarocl@ucm.es>
 *  (C) 2021 Juan Carlos Saez <jcsaezal@ucm.es>
 */

#ifndef MC_EXPERIMENTS_H
#define MC_EXPERIMENTS_H
#include <pmc/ll_events.h>
#include <pmc/pmu_config.h>
#if defined (__linux__)
#include <linux/types.h>
#else
#include <sys/types.h>
#endif
#include <linux/ktime.h>

#ifdef CONFIG_PMC_PERF
#include <linux/workqueue.h>
#endif

#ifndef CONFIG_PMCTRACK
#include <linux/list.h>
#include <linux/perf_event.h>

#define SAFETY_CODE 0x777
//#define SAFETY_CODE     ((unsigned char *)"0x777")
#endif

/* Divide in 32bit mode */
#if defined(__i386__) || defined(__arm__)
#define pmc_do_div(a,b) do_div(a,b)
#else
#define pmc_do_div(a,b) (a)=(a)/(b)
#endif

#define MAX_32_B 0xffffffff
#define AMP_MAX_EXP_CORETYPE	2
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
#include <linux/version.h>

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

typedef struct {
	pmc_usrcfg_t pmc_cfg[MAX_LL_EXPS];
	unsigned int used_pmcs;
	unsigned int nr_pmcs;
	int ebs_index;
	int coretype;
} pmc_config_set_t;

/*
 * Set of core_experiment_t structures.
 * This is the basic structure to support the event multiplexing feature:
 * several sets of HW events are monitored in a round-robin fashion.
 */
typedef struct {
	core_experiment_t* exps[AMP_MAX_EXP_CORETYPE];
	int nr_exps;
	int cur_exp;	/* Contador modular entre 0 y nr_exps */
#ifdef CONFIG_PMC_PERF
	pmc_config_set_t pmc_config[AMP_MAX_EXP_CORETYPE];
	int nr_configs;
#endif
} core_experiment_set_t;


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

#ifdef CONFIG_PMC_PERF
static inline void reset_overflow_status(void) {}
static inline void mc_clear_all_platform_counters(pmu_props_t* props_cpu) { }
/*
 * Enable perf counter asociated to perf_event given by parameter.
 */
void perf_enable_counters(core_experiment_t* exp);
/*
 * Disable perf counter asociated to perf_event given by parameter.
 */
void perf_disable_counters(core_experiment_t* exp);
#else
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
 * Perform a default initialization of all performance monitoring counters
 * in the current CPU. The PMU properties are passed as a parameter for
 * efficiency reasons
 */
void mc_clear_all_platform_counters(pmu_props_t* props_cpu);
#endif
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
int do_setup_pmcs(pmc_usrcfg_t* pmc_cfg, int used_pmcs_msk,core_experiment_t* exp, int cpu, int exp_idx, struct task_struct* p
                 );

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
 * Generate a summary string with the configuration of a hardware counter (lle)
 * in human-readable format. The string is stored in buf.
 */
int print_pmc_config(low_level_exp* lle, char* buf);

#ifdef DEBUG
int print_pmu_msr_values_debug(char* line_out);
#endif



/*** Platform-Independent datatypes of PMCTrack's kernel module */

typedef enum {
	TBS_SCHED_MODE,		/* Scheduler timed-based sampling */
	TBS_USER_MODE,		/* User-requested timed-based sampling */
	EBS_MODE,			/* User-requested event-based sampling */
	EBS_SCHED_MODE		/* Scheduler-driven event-based sampling */
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
#ifndef CONFIG_PMCTRACK
	int safety_control;
	unsigned char prof_enabled;
	struct perf_event *event;
	struct list_head links; 			/* For the exited task list */
#endif
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
#ifdef CONFIG_PMC_PERF
	struct work_struct read_counters_task;			/* Work to queue */
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
	uint_t 	max_ebs_samples;				/* Max number of EBS samples to send kill signal to process */
	ktime_t	ref_time;		 			/* To add timestamps to the various samples */
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

/*
 * Given a null-terminated array of raw-formatted PMC configuration
 * string, store the associated low-level information into an array of core_experiment_set_t.
 */
int configure_performance_counters_set(const char* strconfig[], core_experiment_set_t pmcs_multiplex_cfg[], int nr_coretypes);


#ifdef CONFIG_PMC_PERF
static inline void mc_restart_all_counters(core_experiment_t* core_experiment) {}
static inline void mc_stop_all_counters(core_experiment_t* core_experiment) {}
static inline void mc_clear_all_counters(core_experiment_t* core_experiment) {}
static inline void mc_save_all_counters(core_experiment_t* core_experiment) {}
static inline void mc_restore_all_counters(core_experiment_t* core_experiment) {}
static inline  void restore_context_perfregs ( core_experiment_t* core_experiment) {}
#else

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
 * Monitor resets the Global PMU context (per CPU) ==> for 'old' events
 */
void restore_context_perfregs ( core_experiment_t* core_experiment);

#endif

/**** Operations on core experiment set_t ****/

/* Free up memory from a set of PMC experiments */
static inline void free_experiment_set(core_experiment_set_t* cset)
{
	int i=0;
#ifdef CONFIG_PMC_PERF
	int j=0;
#endif

	for (i=0; i<cset->nr_exps; i++) {
		if (!cset->exps[i])
			continue;

#ifdef CONFIG_PMC_PERF
		/* Recorrer contadores del low_level_exp */
		for(j=0; j<cset->exps[i]->size; ++j) {
			if (cset->exps[i]->array[j].event.event)
				perf_event_release_kernel(cset->exps[i]->array[j].event.event);
		}
#endif
		cset->exps[i]->size=0; /* set to zero */
		kfree(cset->exps[i]);
		cset->exps[i]=NULL;
	}

	cset->nr_exps=0;
}

/* Add one PMC experiment to a given experiment set */
static inline void add_experiment_to_set(core_experiment_set_t* cset, core_experiment_t* exp, pmc_config_set_t* config)
{
	/* When using the perf backend, a NULL value can be passed just to copy the low-level configuration structure */
	if (exp) {
		/* Modifies the exp_idx with the position in the vector !!! */
		exp->exp_idx=cset->nr_exps;
		cset->exps[cset->nr_exps++]=exp;
	}
#ifdef CONFIG_PMC_PERF
	memcpy(&cset->pmc_config[cset->nr_configs++],config,sizeof(pmc_config_set_t));
#endif
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
#ifdef CONFIG_PMC_PERF
	/* The actual pmc_config_set_t array requires no initialization */
	cset->nr_configs=0;
#endif
}

/* Copy the configuration of a experiment set into another */
int clone_core_experiment_set_t(core_experiment_set_t* dst,core_experiment_set_t* src, struct task_struct* p);

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

void pmc_samples_buffer_overflow(pmc_samples_buffer_t* sbuf);

/*
 * Pushes a sample (PMC counts and virtual-counter values) into the buffer and
 * notifies the userspace program if necessary.
 *
 * The function must be invoked with the buffer's lock held.
 */
static inline void __push_sample_cbuffer(pmc_samples_buffer_t* sbuf, pmc_sample_t* sample)
{
	if (is_full_cbuffer_t(sbuf->pmc_samples))
		pmc_samples_buffer_overflow(sbuf);

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


/* Get the CPU where the task ran last */
static inline int task_cpu_safe(struct task_struct *p)
{


#if !defined(CONFIG_THREAD_INFO_IN_TASK)
	struct thread_info* ti=task_thread_info(p);

	if (ti)
		return ti->cpu;
	else
		return -1;

#else
//(!defined(CONFIG_PMC_ARM) && (LINUX_VERSION_CODE >= KERNEL_VERSION(4,9,0)))
	return p->cpu;
#endif
}

/**
 * pmctrack_cpu_function_call - call a function on a given CPU
 * @p:		the task to evaluate
 * @cpu:	the CPU where to invoke the function
 * @func:	the function to be called
 * @info:	the function call argument
 *
 * The function might be on the current CPU, which just calls
 * the function directly
 *
 * returns: 0 when the process isn't running or it is sleeping
 *	    	-EAGAIN - when the process moved to a different CPU
 * 					while invoking this function
 */
int
pmct_cpu_function_call(struct task_struct *p, int cpu, int (*func) (void *info), void *info);


/* For compatibility with ktime_t representation */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,10,0)
#define raw_ktime(kt) (kt).tv64
#else
#define raw_ktime(kt) (kt)
#endif


/**** Operations on a task_struct ****/


#ifndef CONFIG_PMCTRACK

void init_prof_exited_tasks(void);
void add_prof_exited_task(pmon_prof_t *prof);
pmon_prof_t* get_prof_exited_task(struct task_struct* p);
void del_prof_exited_task(pmon_prof_t *prof);

/*
 * Retrieve profiling from the given task
 */
static inline pmon_prof_t* get_prof(struct task_struct* p)
{
	struct perf_event *event;
	pmon_prof_t* prof;


	if (p == NULL)
		return NULL;


	/* Ensure there is at least one element in the list */
	if (list_empty(&(p->perf_event_list))) {
		/* It could be a dead task ... */
		/* Look in global list*/
		if ((p->state & TASK_DEAD) != 0)
			return get_prof_exited_task(p);
		else
			return NULL;
	}

	/* General case */

	//mutex_lock(&p->perf_event_mutex);
	event = list_first_entry(&(p->perf_event_list), typeof(*event), owner_entry);
	//mutex_unlock(&p->perf_event_mutex);

	if (event != NULL) {
		prof = (pmon_prof_t*)(event->pmu_private);
	} else {
		printk(KERN_INFO "Failed to retrieve process perf_event\n");
		return NULL;
	}

	/* Ensure it is the right event */
	if (!prof || prof->safety_control != SAFETY_CODE) {
		printk(KERN_INFO "Failed perf_event safety check\n");
		return NULL;
	}

	return prof;
}

static inline unsigned char get_prof_enabled(pmon_prof_t *prof)
{
	if (prof) return prof->prof_enabled;
	else return 0;
}

static inline void set_prof_enabled(pmon_prof_t *prof, unsigned char enable)
{
	if (prof)
		prof->prof_enabled = enable;
}


#else

static inline void init_prof_exited_tasks(void) {}
static inline void add_prof_exited_task(pmon_prof_t *prof) { }
static inline pmon_prof_t* get_prof_exited_task(struct task_struct* p)
{
	return NULL;
}
static inline void del_prof_exited_task(pmon_prof_t *prof) {}

static inline pmon_prof_t* get_prof(struct task_struct* p)
{
	return (p == NULL ? NULL : (pmon_prof_t*) p->pmc);
}

static inline unsigned char get_prof_enabled(pmon_prof_t *prof)
{
	if (prof)
		return prof->this_tsk->prof_enabled;
	else return 0;
}

static inline void set_prof_enabled(pmon_prof_t *prof, unsigned char enable)
{
	if(prof)
		prof->this_tsk->prof_enabled = enable;
}

#endif


struct task_struct *pmctrack_find_process_by_pid(pid_t pid);

/** Code to guarantee portability in procfs handling **/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,8,0)
typedef struct proc_ops pmctrack_proc_ops_t;
#define PMCT_PROC_OPEN proc_open
#define PMCT_PROC_READ proc_read
#define PMCT_PROC_WRITE proc_write
#define PMCT_PROC_LSEEK proc_lseek
#define PMCT_PROC_RELEASE proc_release
#define PMCT_PROC_IOCTL proc_ioctl
#define PMCT_PROC_MMAP proc_mmap
#else
typedef struct file_operations pmctrack_proc_ops_t;
#define PMCT_PROC_OPEN open
#define PMCT_PROC_READ read
#define PMCT_PROC_WRITE write
#define PMCT_PROC_LSEEK lseek
#define PMCT_PROC_RELEASE release
#define PMCT_PROC_IOCTL ioctl
#define PMCT_PROC_MMAP mmap
#endif

#endif