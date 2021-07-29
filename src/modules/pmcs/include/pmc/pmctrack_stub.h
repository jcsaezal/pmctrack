
/*
 *  include/linux/pmctrack.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
*/
/*
 *   Written by Juan Carlos Saez with help from
 * 	 Guillermo Martinez Fernandez,
 *	 Sergio Sanchez Gordo and Sofia Dronda Merino
 *
*/

#ifndef PMCTRACK_STUB_H
#define PMCTRACK_STUB_H

#ifdef CONFIG_PMCTRACK
#include <linux/pmctrack.h>
#else

#include <linux/types.h>
#include <linux/device.h>
#include <pmc/mc_experiments.h>

/*** Interface to interact with monitoring modules ***/

/* Predefined high-level metrics */
typedef enum {
	MC_SPEEDUP_FACTOR,
	MC_INSTR_PER_CYCLE,
	MC_LLC_MISSES_PER_MINSTR,
	MC_LLC_REQUESTS_PER_KINSTR
} mc_metric_key_t;

int pmcs_get_current_metric_value(struct task_struct* task, int key, uint64_t* value);
/******************************************************************/

/* Interface for PMCTrack kernel module */
typedef struct __pmc_ops {
	int 	(*pmcs_alloc_per_thread_data)(unsigned long,struct task_struct*);
	void	(*pmcs_save_callback)(void* prof, int);
	void	(*pmcs_restore_callback)(void* prof, int);
	void 	(*pmcs_tbs_tick)(void* p, int);
	void	(*pmcs_exec_thread)(struct task_struct*);
	void	(*pmcs_free_per_thread_data)(struct task_struct*);
	void	(*pmcs_exit_thread)(struct task_struct*);
	int		(*pmcs_get_current_metric_value)(struct task_struct* task, int key, uint64_t* value);
} pmc_ops_t;

/* Register/Unregister implementation */
int register_pmc_module(pmc_ops_t* pmc_ops_module, struct module* module);
int unregister_pmc_module(pmc_ops_t* pmc_ops_module, struct module* module);

/* PMCTrack kernel API */
int pmcs_alloc_per_thread_data(unsigned long clone_flags, struct task_struct *p);
void pmcs_save_callback(struct task_struct* tsk, int cpu);
void pmcs_restore_callback(struct task_struct* tsk, int cpu);
void pmcs_tbs_tick(struct task_struct* tsk, int cpu);
void pmcs_exec_thread(struct task_struct* tsk);
void pmcs_free_per_thread_data(struct task_struct* tsk);
void pmcs_exit_thread(struct task_struct* tsk);

/* PMCTrack module setup */
int pmctrack_init(void);
void pmctrack_exit(void);

#endif
#endif
