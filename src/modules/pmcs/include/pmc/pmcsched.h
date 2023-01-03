/*
 *  include/pmc/pmcsched.h
 *
 *  This is the header of pmcsched.c and it contains the structures
 *  and the skeleton with functions plugin should include in PMCSched
 *
 *  Copyright (c) 2022  Carlos Bilbao <cbilbao@ucm.es> and Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef SCHED_PROTO_H
#define SCHED_PROTO_H

#include <pmc/hl_events.h>
#include <pmc/cache_part.h>
#include <pmc/data_str/sized_list.h>
#include <pmc/monitoring_mod.h>
#include <pmc/sched_topology.h>
#include <pmc/intel_perf_metrics.h>
#include <linux/kfifo.h>
#include <linux/rwlock.h>
#include <linux/sched/task.h> /* for get_task_struct() / put_task_struct() */
#include <pmc/schedctl.h>

#define DELAY_KTHREAD  (pmcsched_config.sched_period_normal)
#define DELAY_KTHREAD_PROFILING (pmcsched_config.sched_period_profiling)

/* /proc/kallsyms */
//unsigned long (*exp_wait_task_inactive)
//          (struct task_struct *t, long match_state) = (void*)0xffffffffadca42e0;

struct sched_ops;


/* These are the possible task states (it can also be runnable or not) */
typedef enum {

	NO_QUEUE,              /* The task has no queue assigned              */
	STOP_QUEUE,            /* The task is in the stopped queue            */
	ACTIVE_QUEUE,          /* The task is in the active queue             */
	STOP_PENDING,          /* Stop signal pending                         */
	ACTIVE_PENDING,        /* Wake up signal pending                      */
	KILL_PENDING,          /* Kill this thread                            */
	TASK_KILLED,           /* To avoid NULL pointers with $ kill -s       */
	REACTIVATED,           /* Used on MEMORY plugin                       */
	NUM_STATES
} state_t;

#define wrong_state_th(elem) \
    (elem->state < NO_QUEUE || elem->state > NUM_STATES)

#define signal_pending_th(elem) \
    (elem->state >= STOP_PENDING && !wrong_state_th(elem))

/* Special label for the profiling of any thread */
#define NO_LABEL_P         0  /* Thread has just forked                       */
#define PENDING_PROFILING -1  /* Just like we manage signals, we add the thread
                               * to the list and mark it as pending */
#define memory_label_t    u32

#define profiling_pending(elem) \
    (elem->memory_profile == PENDING_PROFILING)

#define MAX_CACHE_WAYS 20 /* Intel Broadwell-EP Setting */


#define sched_plugin_highest (&dummy)

/* Supported Scheduling policies */
typedef enum {
	SCHED_DUMMY_MM=0,
	SCHED_GROUP_MM,
	SCHED_BUSYBCS_MM,
	NUM_SCHEDULERS
} sched_policy_mm_t;


extern struct sched_ops dummy_plugin;
extern struct sched_ops group_plugin;
extern struct sched_ops busybcs_plugin;


static __attribute__ ((unused)) struct sched_ops* available_schedulers[NUM_SCHEDULERS]= {
	&dummy_plugin,
	&group_plugin,
	&busybcs_plugin,
};

#define EBIT(nr)         (UL(1) << (nr))
#define PMCSCHED_GLOBAL_LOCK      EBIT(0)
#define PMCSCHED_CPUGROUP_LOCK    EBIT(1)
#define PMCSCHED_CUSTOM_LOCK      EBIT(2)
#define PMCSCHED_AMP_SCHED        EBIT(3)

/*
 * Highly encouraged to not use if don't need it, to speed up things.
 */
#define PMCSCHED_COSCHED_PLUGIN   EBIT(4)

/* Per-thread flags (t->flags) */
#define PMCSCHEDT_MIGRATION_COMPLETED EBIT(0)
#define PMCSCHEDT_IN_MIGRATION_LIST   EBIT(1)

/* Profiling mode alters certain aspects, such as the delay in the periodic
* kthread, avoiding overhead and other code paths.
*/
typedef enum {
	PROFILING,
	NORMAL
} scheduling_mode_t;

struct sched_app;
struct pmcsched_thread_data;

#define AMP_CORE_TYPES 2

/* Structure that represents a single- or multi- threaded application         */
typedef struct app_pmcsched {

	sized_list_t app_active_threads;
	struct list_head link_active_apps;
	/* atomic_t ref_counter; */

	/* --------------------------------------------------------------------
	 *  As we work with per-thread signals, state could be NO_QUEUE,
	 *  STOP_QUEUE, ACTIVE_QUEUE.
	 *  -------------------------------------------------------------------
	 */
	state_t state;

	sized_list_t app_stopped_threads;
	struct list_head link_stopped_apps;

	struct sched_app* sa; /* Backwards pointer */
	/* -- Intel CAT fields -- */
	app_t app_cache;
	/******** Definition of plugin-specific per-group per-app data here ****/
} app_t_pmcsched;

typedef struct sched_app {
	atomic_t ref_counter;
	rwlock_t app_lock; /* For global app fields */
	app_t_pmcsched pmc_sched_apps[MAX_GROUPS_PLATFORM];
	unsigned char is_multithreaded;
	/******** Definition of plugin-specific global per-app data here ****/
} sched_app_t;

#define PMCSCHED_PERIOD_DEFAULT 0
#define PMCSCHED_PERIOD_DISABLE -1

/* Global data structures for scheduling */
typedef struct sched_thread_group {

	sized_list_t active_threads;
	sized_list_t active_apps;

	sized_list_t stopped_threads;
	sized_list_t stopped_apps;

	sized_list_t all_threads;

	/* List of threads with pending signals */
	sized_list_t pending_signals_list;

	/* List of threads with pending profiling */
	sized_list_t pending_profile_list;

	/* Threads stopped during others' profiling stage */
	sized_list_t profiler_stopped_list;

	/* Threads to be migrated to a remote group or CPU */
	sized_list_t migration_list;

	/* Per-group list to update COS after cache-partition reconfiguration */
	sized_list_t clos_to_update;

	cpu_group_t* cpu_group; /* Topology of this CPU group */

	struct task_struct *kthread;    /* kWorker to be triggered periodically */

	struct timer_list timer;    /* Per-Group periodic scheduler activation */

	unsigned char activate_kthread;

	int timer_period; /* To be controlled by the scheduling plugin */

	void* private_data[NUM_SCHEDULERS];
	spinlock_t lock;

	scheduling_mode_t scheduling_mode;
	int disabling_mode; /* Avoid list corruption when disabling and killing */
	/* This pointer should be different from NULL in profiling mode */
	struct pmcsched_thread_data* profiled;

	unsigned long last_migration_thread_activation;
} sched_thread_group_t; /* Retrieve with get_cur_group_sched() */

/* Legacy global for backwards compatibility with PMCSched v0.5 plugins */
extern sched_thread_group_t* pmcsched_gbl;
extern sched_thread_group_t sched_thread_groups[MAX_GROUPS_PLATFORM];
#define SCHEDULING_MODE (pmcsched_gbl->scheduling_mode)
#define PROFILED (pmcsched_gbl->profiled)

typedef struct {
	/* Configurable parameters */
	unsigned long sched_period_normal;
	unsigned long sched_period_profiling;
	rmid_allocation_policy_t rmid_allocation_policy; /* Selected RMID allocation policy */
} pmcsched_config_t;

extern pmcsched_config_t pmcsched_config;




typedef enum  {
	MIGRATION_COMPLETED=0,    /* Thread is not in migration list */
	MIGRATION_REQUESTED,    /* The thread has been added to a migration list,
                                                        but the migration hasn't been started yet */
	MIGRATION_STARTED     /* The migration has been initiated */,
} migration_state_t;

typedef struct migration_data {
	int src_group;
	int src_cpu;
	int dst_group;
	int dst_cpu;
	cpumask_t dst_cpumask;
	volatile migration_state_t state;
} migration_data_t;

typedef struct pmcsched_thread_data {

	metric_experiment_set_t metric_set[AMP_CORE_TYPES];                 /* Two core types */
	unsigned int runnable;
	unsigned int actually_runnable;         /* For low-level tracing */

	state_t state; /* It COULD be running (runnable) but we might have
                        * stopped it. This is also used to determine the signal
                        * that should be sent if requested.        */

	sched_app_t* sched_app;          /* Application this thread belongs to   */
	app_t_pmcsched* app;          /* For compatibility with legacy code */
	sched_thread_group_t* cur_group;    /*
                                             * Pointer to current sched thread group
                                             * The scheduling plugin should maintain it
                                             */
	pmon_prof_t* prof;            /* Backwards pointer                    */

	struct list_head link_active_threads_gbl;
	struct list_head link_active_threads_apps;

	struct list_head link_active_threads_apps_cache;

	int security_id;

	struct list_head link_stopped_threads_gbl;
	struct list_head link_stopped_threads_apps;

	/* For CPU masking after periodic call to plugin   */
	struct list_head migration_links;           /* For local migration lists! */
	struct list_head global_migration_link;    /* For per group (global) migration list! */

	migration_data_t migration_data;

	/* To signal threads after plugin periodic call   */
	struct list_head signal_links;

	/* Check if the thread woke up/sleep because we sent a signal         */
	int signal_sent;

	/* To profile threads after plugin periodic call   */
	struct list_head profile_links;

	/* Mask to assign to the thread if added to the list */
	cpumask_t* mask;

	unsigned int first_time;
	intel_cmt_thread_struct_t *cmt_data; /* Pointer to per-app data */
	intel_cmt_thread_struct_t cmt_monitoring_data; /* For per-thread handling */


	struct list_head link_all_threads; /* Used in memory-aware plugin */
	struct list_head link_dino_threads; /*TODO do you need it ?*/
	int stopped_during_profiling;

	/* Tasklet for sending signal */
	int pending_signal;

	unsigned char force_per_thread;

#ifdef TASKLET_SIGNALS_PMCSCHED
	struct tasklet_struct signal_tasklet;
#endif
	struct list_head profiler_stopped_links;

	/* Active scheduler at the moment of forking. Important to use that pointer in the free callback */
	struct sched_ops* scheduler;

	/* General purpose flags field */
	unsigned long t_flags;

	/* Let's see what to do with this field... */
	memory_label_t memory_profile;

	schedctl_t* schedctl;
	/* Plugin specific fields */

} pmcsched_thread_data_t;

extern intel_cmt_support_t pmcs_cmt_support;
extern intel_cat_support_t pmcs_cat_support;
extern unsigned char pmcsched_rdt_capable;
extern unsigned char pmcsched_ehfi_capable;

/*
 * =========================================================
 * ======= PMCSCHED'S HELPER FUNCTIONS AND DATATYPES =======
 * =========================================================
 **/
extern unsigned int active_scheduler_verbose; /* Turn it on via /proc/pmc/config for
                                                 more informative dmesg logs  */

extern int disabling_mode;/* Avoid list corruptions when disabling and killing */

typedef struct pmcsched_counter_config {
	/* Counter config to expose configuration for PMCTrack */
	monitoring_module_counter_usage_t counter_usage;
	core_experiment_set_t* pmcs_descr;
	metric_experiment_set_t* metric_descr[AMP_CORE_TYPES];
	pmc_profiling_mode_t profiling_mode;
} pmcsched_counter_config_t;

/**
 * A linked list of scheduler policies is maintained .
 * These functions assume that the proper GBL lock has
 * been acquired except for the k-thread.
*/
typedef struct sched_ops {
	sched_policy_mm_t policy;
	struct list_head link_schedulers;
	char *description;
	pmcsched_counter_config_t* counter_config;
	unsigned long flags; /* To control locking scheme */
	int (*probe_plugin) (void);
	int (*init_plugin) (void);

	/* The plugin might free some structures */
	void (*destroy_plugin) (void);

	/* Callbacks to capture basic scheduling events */
	void (*on_exec_thread) (pmon_prof_t* prof);
	void (*on_active_thread) (pmcsched_thread_data_t* t);
	void (*on_inactive_thread)(pmcsched_thread_data_t* t);
	int (*on_fork_thread) (pmcsched_thread_data_t* t, unsigned char is_new_app);
	void (*on_free_thread) (pmcsched_thread_data_t* t, unsigned char is_last_thread);
	void (*on_exit_thread) (pmcsched_thread_data_t* t);

	/**
	 * Callbacks for per-core-group periodic scheduling
	 * from process context and interrupt context respectively
	 **/
	void  (*sched_kthread_periodic)
	(sized_list_t* migration_list);
	void  (*sched_timer_periodic)(void);

	/**
	 * Callback invoked when performance counters are sampled.
	 */
	int (*on_new_sample) (pmon_prof_t* prof, int cpu,
	                      pmc_sample_t* sample,int flags,void* data);

	/* Read and write plugin's configurable parameters */
	int (*on_read_plugin) (char *aux);
	int (*on_write_plugin) (char *line);

	/**
	 * Callbacks for controlling
	 * low-level scheduling events in plugins
	 **/
	void (*on_switch_in_thread)(pmon_prof_t* prof,
	                            pmcsched_thread_data_t* t, unsigned char prof_enabled);

	void (*on_switch_out_thread)(pmon_prof_t* prof,
	                             pmcsched_thread_data_t* t, unsigned char prof_enabled);

	void (*on_migrate_thread)(pmcsched_thread_data_t* t, int prev_cpu, int new_cpu);

	void (*on_tick_thread)(pmcsched_thread_data_t* t, int cpu);

	/**
	 * Specific callbacks for the plugins relying
	 * on system-wide profiling (Deprecated)
	 **/
	void (*light_weight_scenario) (pmcsched_thread_data_t* t);
	int (*profile_thread) (pmcsched_thread_data_t* t);
	void (*active_during_profile) (pmcsched_thread_data_t* t);
} sched_ops_t;

#define dead_task(elem) \
 (elem->state == TASK_KILLED || get_task_state(elem->prof->this_tsk) == TASK_DEAD)

extern sched_ops_t * active_scheduler;

/* ----------------------AUXILIARY FUNCTIONS ---------------------------------*/

/*  This is an auxiliary function to switch members between lists. It assumes
    all the necessary locks have been acquired.
*/
void switch_queues (sized_list_t *queue_1, sized_list_t *queue_2, void *val);

/* This auxiliary function flags as zero the cores with running threads launched
 * with pmc-launch and returns the number of busy cores.
*/
int mark_busy_cores(int *free_cores, int num_cores);

int next_free_cpu(int *free_cores, int max);

void check_active_app(struct app_pmcsched* app);

void check_inactive_app(struct app_pmcsched *app);

void __activate_stopped_by_profiling(sized_list_t* stopped_list,
                                     sized_list_t* signal_list);

int __give_rmid(pmcsched_thread_data_t* t);

int __euthanasia(pmcsched_thread_data_t* t);

void assign_cpu(pmcsched_thread_data_t* elem, int core,
                sized_list_t* migration_list);

void __compute_profiling_time(int init);

void make_active_if_stopped(pmcsched_thread_data_t* activated,
                            sized_list_t* signal_list);

void set_clos_cpu_data(void* data);

void update_clos_app_unlocked(sized_list_t* list);

int compare_fitness(void* t_1, void* t_2);

int compare_threads_mem(void* t_1, void* t_2);

void update_threads_progress_relabel(void);

extern sized_list_t* clos_to_update;

void pmcsched_set_state(pmcsched_thread_data_t *t, state_t state);

/* API for scalable scheduling plugins */
sched_thread_group_t* get_cur_group_sched(void);
sched_thread_group_t* get_cpu_group_sched(int cpu);
sched_thread_group_t* get_group_sched_by_id(int id);

static inline app_t_pmcsched* get_cur_app_cpu(pmcsched_thread_data_t* t, int cpu)
{
	int group_id=get_group_id_cpu(cpu);
	return &t->sched_app->pmc_sched_apps[group_id];
}

static inline app_t_pmcsched* get_group_app_cpu(pmcsched_thread_data_t* t, int group_id)
{
	return &t->sched_app->pmc_sched_apps[group_id];
}


static inline app_t_pmcsched* get_cur_app(pmcsched_thread_data_t* t)
{
	return get_cur_app_cpu(t,smp_processor_id());
}

static inline void double_group_sched_lock(sched_thread_group_t* g1,
        sched_thread_group_t* g2, unsigned long* flags)
{

	unsigned long lflags;

	if (g1>g2) {
		spin_lock_irqsave(&g1->lock,lflags);
		spin_lock(&g2->lock);
	} else {
		spin_lock_irqsave(&g2->lock,lflags);
		if (g2!=g1)
			spin_lock(&g1->lock);
	}

	(*flags)=lflags;
}

static inline void double_group_sched_unlock(sched_thread_group_t* g1,
        sched_thread_group_t* g2, unsigned long flags)
{

	if (g1>g2) {
		spin_lock(&g2->lock);
		spin_unlock_irqrestore(&g1->lock,flags);
	} else {
		if (g2!=g1)
			spin_lock(&g1->lock);
		spin_unlock_irqrestore(&g2->lock,flags);
	}
}

/* Return per-group private data of the corresponding policy */
static inline void* get_cur_sched_priv_data(sched_thread_group_t* group)
{
	return group->private_data[active_scheduler->policy];
}

static inline void set_cur_sched_priv_data(sched_thread_group_t* group,void *pdata)
{
	group->private_data[active_scheduler->policy]=pdata;
}

/* Helper call to tell wether an individual thread belongs to a multithreaded program */
static inline int is_part_of_multithreaded_program(pmcsched_thread_data_t* t)
{
	return t->sched_app->is_multithreaded;
}

/* For LFOC-Multi*/
void set_group_timer_expiration(sched_thread_group_t* group, unsigned long new_period);

/* Retrieve pmcsched_app from a pointer to cache-related app */
static inline struct app_pmcsched *get_app_pmcsched(app_t *app)
{
	return app ? container_of(app, struct app_pmcsched, app_cache) : NULL;
}

/*
 * Function to update clos of applications that were moved from their
 * associated partition
 */
void populate_clos_to_update_list(sched_thread_group_t* group, cache_part_set_t* part_set);

/*
 * Static trace points for AMP systems
 */
void trace_amp_exit(struct task_struct* p, unsigned int tid, unsigned long ticks_big, unsigned long ticks_small);
void trace_td_thread_exit(struct task_struct* p,
                          unsigned long noclass,
                          unsigned long class0,
                          unsigned long class1,
                          unsigned long class2,
                          unsigned long class3);
#endif /* SCHED_PROTO_H */
