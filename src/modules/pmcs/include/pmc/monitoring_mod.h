/*
 *  include/pmc/monitoring_mod.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef MONITORING_MOD_H
#define MONITORING_MOD_H

#include <pmc/pmc_user.h>
#include <pmc/mc_experiments.h>
#include <linux/pmctrack.h>
#include <linux/list.h>
#include <linux/proc_fs.h>

#define MAX_CHARS_EST_MOD 55


/* Flags to pass to new_sample() */
#define MM_TICK             0x1
#define MM_MIGRATION        0x2
#define MM_EXIT		    0x4
#define MM_SAVE		    0x8
#define MM_NO_CUR_CPU	    0x10

/*
 * This structure specifies which hardware performance counters
 * are being used by the current monitoring module,
 * how many virtual counters it exports as well as
 * a description of every virtual counter supported.
*/
typedef struct {
	uint_t hwpmc_mask;
	uint_t nr_virtual_counters;
	uint_t nr_experiments;
	const char* vcounter_desc[MAX_VIRTUAL_COUNTERS];
} monitoring_module_counter_usage_t;

/* Interface for monitoring modules */
typedef struct monitoring_module {
	char info[MAX_CHARS_EST_MOD];	/* Description for the monitoring module */
	int id;	                        /* Monitoring module's internal ID.
									 * This value is set automatically by the mm_manager
									 */
	struct list_head    links;		/* Next/Prev pointer for the doubly linked list of monitoring modules  */
	int 	(*probe_module)(void);		/* Callback invoked upon loading the monitoring module.
										   Return a non-zero value if the mon. module is not
										   supported on the current platform. In that case
										   the module will not appear in the list of available
										   monitoring modules. */
	int		(*enable_module)(void);		/* Invoked when the administrator enables a monitoring module.
											The function returns 0 on success, and a non-zero value upon failure.
										*/
	void	(*disable_module)(void);	/* Invoked when the administrator disables a monitoring module. */
	int 	(*on_read_config)(char* s, unsigned int maxchars);	/* Invoked when a user reads configuration parameters
																   from /proc/pmc/config. The monitoring module must print
																   the value of configuration parameters using sprintf() on "s" (first parameter).
													   				The parameters must be printed using the "param_name=value" format.

													   				The function returns the number of characters written in s.
													   			*/
	int 	(*on_write_config)(const char *s, unsigned int len);	/* Invoked when the user writes a string in /proc/pmc/config.
													   				 If the monitoring module exports configuration parameters,
													   			 	this function must take care of parsing the values provided by the user
													   			 	in s ("param_name=value") and setting up the parameter values accordingly.

													   				The function returns len on success, and a negative value upon failure.
													   			*/
	int		(*on_fork)(unsigned long clone_flags,pmon_prof_t*);	/*	Invoked when a new process/thread is created.
																	This function takes care of allocating per-thread data
																	(if needed) and updating the "monitoring_mod_priv_data"
																	pointer in pmon_prof_t.

																	The function returns 0 on success, and a non-zero value upon failure (such as failing
																	to allocate memory for the private data per-thread structure) .
																*/
	void	(*on_exec)(pmon_prof_t*);	/*	Invoked when a process calls exec() */

	/*
	 * The on_new_sample() callback function gets invoked right after a PMC sample is collected
	 * by PMCTrack's kernel module for a given thread (prof). Note that this function may be invoked in different scenarios:
	 * TBS mode (on tick/on exit/on migration), scheduler mode or EBS mode.
	 *
	 * The function returns 0 on success, and a non-zero value upon failure
	 */
	int		(*on_new_sample)(	pmon_prof_t* prof,				/* Current thread's pmon structure */
	                            int cpu,pmc_sample_t* sample,	/* Array of freshly gathered PMC values */
	                            int flags,						/* Flags indicating the context where this sample has been taken */
	                            void* data);
	void	(*on_migrate)(pmon_prof_t* p, int prev_cpu, int new_cpu);	/*	Invoked when a process is migrated to a different CPU
																			Note that the first time this callback is invoked
																			prev_cpu==-1.
																		 */
	void	(*on_exit)(pmon_prof_t* p);			/*	Invoked when a process calls exec() */
	void	(*on_free_task)(pmon_prof_t* p);	/*	Invoked when the kernel is about to free up a process task structure */
	void	(*on_switch_in)(pmon_prof_t* p);	/*	Invoked when a context switch in takes place (p is the "incoming" thread ) */
	void	(*on_switch_out)(pmon_prof_t* p);	/*	Invoked when a context switch out takes place (p is the "outgoing" thread ) */
	/*
	 * get_current_metric_value() gets invoked from the scheduler code when
	 * a scheduling policy requests the value of a certain performance metric with ID=key.
	 * If the monitoring module exports a metric with such a key, the metric's value must be returned using the "value" parameter.
	 *
	 * The function returns 0 on success (the desired metric is available), and a non-zero value upon failure.
	 */
	int		(*get_current_metric_value)(pmon_prof_t* prof, int key, uint64_t* value);
	void	(*on_tick)(pmon_prof_t* p, int cpu);	/*	Invoked upon scheduler_tick() */
	/*
	 *	This callback function must be implemented by those monitoring modules
	 *	that either use PMCs for their own purpose or export virtual counters
	 *	for PMCTrack's user space components. In that case, the various fields in
	 * 	the usage structure must be appropriately filled up based on the
	 * 	monitoring module's characteristics.
	 */
	void 	(*module_counter_usage)(monitoring_module_counter_usage_t* usage);
	int 	(*on_syswide_start_monitor)(int cpu, unsigned int virtual_mask);	/*	Invoked on each CPU when
																					starting up system-wide monitoring mode */
	void 	(*on_syswide_stop_monitor)(int cpu, unsigned int virtual_mask);		/*	Invoked on each CPU when
																					stopping system-wide monitoring mode */
	void 	(*on_syswide_refresh_monitor)(int cpu, unsigned int virtual_mask);	/* 	Invoked on each CPU to update virtual-counter
																					counts in system-wide monitoring mode */
	/* 	Invoked on each CPU to dump virtual-counter values into a pmc_sample_t structure */
	void 	(*on_syswide_dump_virtual_counters)(int cpu, unsigned int virtual_mask, pmc_sample_t* sample);
} monitoring_module_t;


/* To enable process termination via signals */
void pmctrack_terminate_process(struct task_struct* p);
/* Init monitoring module manager */
int init_mm_manager(struct proc_dir_entry* pmc_dir);
/* Free up resources allocated by the monitoring module manager */
void destroy_mm_manager(struct proc_dir_entry* pmc_dir);
/* Load a specific monitoring module */
int load_monitoring_module(monitoring_module_t* module);
/* Unload a monitoring module with associated ID */
int unload_monitoring_module(int module_id);
/* Get security code associated with current monitoring module */
int current_monitoring_module_security_id(void);
/* Get pointer to descriptor to current monitoring module */
monitoring_module_t* current_monitoring_module(void);
/* Wrapper functions for every operation in the monitoring_module_t interface */
int mm_on_read_config(char* str, unsigned int len);
int mm_on_write_config(const char *str, unsigned int len);
int mm_on_fork(unsigned long clone_flags, pmon_prof_t* prof);
void mm_on_exec(pmon_prof_t* prof);
int mm_on_new_sample(pmon_prof_t* prof,int cpu,pmc_sample_t* sample,int flags,void* data);
void mm_on_tick(pmon_prof_t* prof,int cpu);
void mm_on_migrate(pmon_prof_t* prof, int prev_cpu, int new_cpu);
void mm_on_exit(pmon_prof_t* prof);
void mm_on_free_task(pmon_prof_t* prof);
void mm_on_switch_in(pmon_prof_t* prof);
void mm_on_switch_out(pmon_prof_t* prof);
int mm_get_current_metric_value(pmon_prof_t* prof, int key, uint64_t* value);
void mm_module_counter_usage(monitoring_module_counter_usage_t* usage);
int mm_on_syswide_start_monitor(int cpu, unsigned int virtual_mask);
void mm_on_syswide_stop_monitor(int cpu, unsigned int virtual_mask);
void mm_on_syswide_refresh_monitor(int cpu, unsigned int virtual_mask);
void mm_on_syswide_dump_virtual_counters(int cpu, unsigned int virtual_mask,pmc_sample_t* sample);


/* Safe and generic open/close operations for /proc entries */
int proc_generic_open(struct inode *inode, struct file *filp);
int proc_generic_close(struct inode *inode, struct file *filp);

/* Extra IDs for high-level metrics (under SCHED_AMP) */
typedef enum {
#if defined(CONFIG_AMP_CONTENTION) || !defined(SCHED_AMP)
	MC_EFFICIENCY_FACTOR=MC_LLC_REQUESTS_PER_KINSTR+1,
	MC_RELATIVE_SLOWDOWN,
#else
	MC_RELATIVE_SLOWDOWN=MC_EFFICIENCY_FACTOR+1,
#endif
	MC_BTR_BIG,
	MC_BTR_SMALL,
	MC_BTR_HIGH,
	MC_BTR_LOW,
	MC_CACHE_SENSITIVE,
	/* ==== Events to control core disabling actions ===== */
	MC_RESET_MONITORING_INTERVAL,
	MC_MIGHT_BE_SUFFERING,
	MC_LIGHT_SHARING,
	MC_PHASE_HIT_RATE,
	MC_RESET_PHASE_STATISTICS,
} mc_metric_extra_key_t;

#endif
