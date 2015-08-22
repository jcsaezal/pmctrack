/*
 *  mchw_core.c
 *
 *  Architecture-independent PMCTrack core
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/cpumask.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/kdebug.h> 
#include <linux/notifier.h>
#include <linux/semaphore.h>
#include <pmc/pmu_config.h>
#include <linux/pmctrack.h>
#include <linux/vmalloc.h>
#include <asm-generic/errno.h>
#include <linux/mm.h>  /* mmap related stuff */
#include <pmc/monitoring_mod.h>
#include <pmc/syswide.h>

#define BUF_LEN_PMC_SAMPLES_EBS_KERNEL (((PAGE_SIZE)/sizeof(pmc_sample_t))*sizeof(pmc_sample_t))

/* 
 * Different scenarios where performance samples 
 * are generated.
 */
typedef enum {
	PMC_TICK_EVT,		/* TBS or EBS tick */
	PMC_SAVE_EVT,		/* TBS tick on save callback */
	PMC_MIGRATION_EVT,	/* PMC sample obtained when a thread was migrated to a different core type */
	PMC_SELF_EVT		/* Self-monitoring sampling (libpmctrack) */
} pmc_sampling_event_t;

/* Default per-CPU set of PMC events */
static DEFINE_PER_CPU(core_experiment_t, cpu_exp);

/*
 * Per-CPU structure to aid in the 
 * implementation of EBS monitoring mode
 */
DEFINE_PER_CPU(struct cpu_pmc_buffer, pmc_buffer);

/* Scheduler function exported by PMCTrack kernel patch */
extern struct task_struct* find_process_by_pid(pid_t pid);

/* Global PMCTrack configuration parameters */
typedef struct {
	uint_t pmon_nticks;             /* Default sampling interval for the
								     * scheduler-driven monitoring mode
								     */
	uint_t pmon_kernel_buffer_size;	 /* Default capacity for the kernel 
									  * buffer that stores PMC samples 
									  */
} pmon_config_t;
pmon_config_t pmcs_pmon_config;

/* Initialize global configuration parameters */
void init_pmon_config_t(void);

/* Key PMC counting function */
int do_count_mc_experiment(pmon_prof_t* prof,
                           core_experiment_t* core_experiment,
                           int update_acum);


/* Functions that implement the various operations in the pmc_ops_t interface */
static int   mod_alloc_per_thread_data(unsigned long clone_flags,struct task_struct* p);
static void  mod_save_callback(void* prof, int cpu);
static void  mod_restore_callback(void* prof, int cpu);
static void  mod_tbs_tick(void* p, int cpu);
static void  mod_exec_thread(struct task_struct* tsk);
static void  mod_free_per_thread_data(struct task_struct* tsk);
static void  mod_exit_thread(struct task_struct* tsk);
static int   mod_get_current_metric_value(struct task_struct* task, int key, uint64_t* value);


/*** PMCTrack's /proc/pmc/- callback functions **/

/* /proc/pmc/config */
static ssize_t proc_pmc_config_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);
static ssize_t proc_pmc_config_read(struct file *filp, char __user *buf, size_t len, loff_t *off);

static const struct file_operations proc_pmc_config_fops = {
	.read = proc_pmc_config_read,
	.write = proc_pmc_config_write,
	.open = proc_generic_open,
	.release = proc_generic_close,
};

/* /proc/pmc/enable */
static ssize_t proc_pmc_enable_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);
static ssize_t proc_pmc_enable_read(struct file *filp, char __user *buf, size_t len, loff_t *off);

static const struct file_operations proc_pmc_enable_fops = {
	.read = proc_pmc_enable_read,
	.write = proc_pmc_enable_write,
	.open = proc_generic_open,
	.release = proc_generic_close,
};

/* /proc/pmc/properties */
static ssize_t proc_pmc_properties_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);
static ssize_t proc_pmc_properties_read(struct file *filp, char __user *buf, size_t len, loff_t *off);

static const struct file_operations proc_pmc_properties_fops = {
	.read = proc_pmc_properties_read,
	.write = proc_pmc_properties_write,
	.open = proc_generic_open,
	.release = proc_generic_close,
};

/* /proc/pmc/info */
static ssize_t proc_pmc_info_read(struct file *filp, char __user *buf, size_t len, loff_t *off);

static const struct file_operations proc_pmc_info_fops = {
	.read = proc_pmc_info_read,
	.open = proc_generic_open,
	.release = proc_generic_close,
};

/* /proc/pmc/monitor */
static ssize_t proc_monitor_pmcs_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);
static ssize_t proc_monitor_pmcs_read (struct file *filp, char __user *buf, size_t len, loff_t *off);
static int proc_monitor_pmcs_mmap(struct file *filp, struct vm_area_struct *vma);

static const struct file_operations proc_monitor_pmcs_fops = {
	.read = proc_monitor_pmcs_read,
	.write = proc_monitor_pmcs_write,
	.mmap=proc_monitor_pmcs_mmap,
	.open = proc_generic_open,
	.release = proc_generic_close,
};

/* 
 * This function accepts a user-provided PMC configuration string (buf) in the raw format,
 * tranforms the configuration into a core_experiment_t structure (low-level representation of
 * a set of PMC events) and assigns the structure to a given process (p).
 */
static int configure_performance_counters_thread(const char *buf,struct task_struct* p, int system_wide);

/* 
 * This function accepts a user-provided virtual configuration string (buf) in the raw format,
 * and assigns the underlying configuration to a given process (p).
 */
static int configure_virtual_counters_thread(const char *buf,struct task_struct* p, int system_wide);

/* Initialization of platform-independent per-CPU structures */
static void init_percpu_structures(void);

/*
 * Read performance counters associated with the PMC configuration
 * described by core_experiment, and update counters
 * in the thread specific prof structure. 
 */
int do_count_mc_experiment(pmon_prof_t* prof,
                           core_experiment_t* core_experiment,
                           int update_acum)
{
	unsigned int i;
	uint64_t last_value;

	/* PMCS are just configured for the first time */
	if(core_experiment->need_setup) {
		mc_restart_all_counters(core_experiment);
		return 1;
	} else {

		for(i=0; i<core_experiment->size; i++) {
			low_level_exp* lle = &core_experiment->array[i];
			/* Gather PMC values (stop,read and reset) */
			__stop_count(lle);
			__read_count(lle);
			__restart_count(lle);

			/* get Last value */
			last_value = __get_last_value(lle);

			if(update_acum) {
				prof->pmc_values[i]+=last_value;
			}
		}
		reset_overflow_status();

		return 0;
	} 
}

/*
 * Read performance counters associated with the PMC configuration
 * described by core_experiment, and store gathered values in the 
 * "samples" array.
 * Unlike do_count_mc_experiment(), this function takes into account 
 * the fact that counters may have overflowed, since the last time
 * counters were reset. 
 */
int do_count_mc_experiment_buffer(core_experiment_t* core_experiment,
                                  pmu_props_t* pmu_props,
                                  uint64_t* samples)
{
	unsigned int i;

	/* PMCS are just configured for the first time */
	if(core_experiment->need_setup) {
		mc_restart_all_counters(core_experiment);
		return 1;
	} else {
		/*Monitoring Procedure*/
		for(i=0; i<core_experiment->size; i++) {
			low_level_exp* lle = &core_experiment->array[i];
			/* Gather PMC values (stop,read and reset) */
			__stop_count(lle);
			__read_count(lle);
			__restart_count(lle);

			/* get Last value */
			samples[i] = __get_last_value(lle);
			if (core_experiment->nr_overflows[i]) {
				/* Accumulate real value */
				samples[i]+=core_experiment->nr_overflows[i]*(pmu_props->pmc_width_mask+1);
				/* Clear overflow */
				core_experiment->nr_overflows[i]=0;
				trace_printk("Acumulating overflow data from phys_counter %d\n",
				             core_experiment->log_to_phys[i]);
			}
		}
		reset_overflow_status();
		return 0;
	} 
}

/* Actual implementation of the pmc_ops_t interface */
pmc_ops_t pmc_mc_prog = {
	mod_alloc_per_thread_data,
	mod_save_callback,
	mod_restore_callback,
	mod_tbs_tick,
	mod_exec_thread,
	mod_free_per_thread_data,
	mod_exit_thread,
	mod_get_current_metric_value
};

/* Invoked when forking a process/thread */ 
static int mod_alloc_per_thread_data(unsigned long clone_flags, struct task_struct* p)
{
	int i;
	int ret;

	/* Allocate memory to struct pmon_prof_t */
	pmon_prof_t* prof = (pmon_prof_t*) kmalloc(sizeof(pmon_prof_t), GFP_KERNEL);

	if(prof == NULL) {
		printk(KERN_INFO "Can't allocate memory for pmon_prof_t.\n");
		return -ENOMEM;
	}

	/* Basic prof_struct initialization */
	prof->pmc_ticks_counter = 0;
	prof->samples_counter = 0;

	prof->pmc_jiffies_interval=-1;	/* TBS disabled */

	prof->pmc_jiffies_timeout=jiffies+3000*250;	/* Just in case: make sure it doesn't expire soon */

	prof->this_tsk=p;

	for(i=0; i<MAX_LL_EXPS; i++)
		prof->pmc_values[i]=0;

	/* Set NULL */
	prof->pmcs_config=NULL;

	for(i=0; i<AMP_MAX_CORETYPES; i++)
		init_core_experiment_set_t(&prof->pmcs_multiplex_cfg[i]);

	/* This is good to detect migrations (do not update unless counters have been configured) */
	prof->last_cpu=-1;

	/* PMC Samples buffer for this thread */
	prof->pmc_samples_buffer=NULL;	/* Allocate on demand */

	prof->profiling_mode=TBS_SCHED_MODE;

	prof->context_switch_timestamp=jiffies;

	prof->flags=0;

	prof->virt_counter_mask=0;	/* No virtual counters selected */

	prof->pmc_user_samples=NULL;

	prof->pmc_kernel_samples=NULL;

	prof->nticks_sampling_period=pmcs_pmon_config.pmon_nticks;

	prof->kernel_buffer_size=pmcs_pmon_config.pmon_kernel_buffer_size;

	/* No per-thread private data by default */
	prof->monitoring_mod_priv_data = NULL;

	if (is_new_thread(clone_flags) && current->prof_enabled && current->pmc) {
		pmon_prof_t* par_prof = (pmon_prof_t*)current->pmc;

		if (!(par_prof->flags & PMC_SELF_MONITORING)) {
			prof->profiling_mode=par_prof->profiling_mode;
			if (par_prof->pmc_samples_buffer) {
				get_pmc_samples_buffer(par_prof->pmc_samples_buffer);
				prof->pmc_samples_buffer=par_prof->pmc_samples_buffer;
			}

			/* Clone pmcs */
			if (par_prof->pmcs_config) {
				for(i=0; i<2; i++)
					clone_core_experiment_set_t(&prof->pmcs_multiplex_cfg[i],&par_prof->pmcs_multiplex_cfg[i]);
				/* For now start with slow core events */
				prof->pmcs_config=get_cur_experiment_in_set(&prof->pmcs_multiplex_cfg[0]);
			}

			prof->virt_counter_mask=par_prof->virt_counter_mask;

			/* Inherit intervals from the parent process (sibling actually :-)) */
			prof->pmc_jiffies_interval=par_prof->pmc_jiffies_interval;
			prof->nticks_sampling_period=par_prof->nticks_sampling_period;
			prof->pmc_jiffies_timeout=jiffies+prof->pmc_jiffies_interval;
			p->prof_enabled=1;
		} else {
			/* Inherit buffer size */
			prof->kernel_buffer_size=par_prof->kernel_buffer_size;
		}

	}

	if ((ret=mm_on_fork(clone_flags,prof))) {
		kfree(prof);
		return ret;
	}

	p->pmc=prof;
	return 0;
}


/*
 * This function is invoked from the tick processing 
 * function and context-switch related callbacks 
 * when the Time-Based Sampling (TBS) mode is enabled
 * for the current process. The function takes care of
 * reading PMC and virtual counters and pushes a new PMC sample
 * into the samples buffer if it is due time (e.g., end of the sampling interval)
 */
static inline void sample_counters_user_tbs(pmon_prof_t* prof, core_experiment_t* core_exp, pmc_sampling_event_t event, int cpu)
{
	int i=0;
	unsigned long flags;
	pmc_sample_t sample;
	core_experiment_t* next;
	int cur_coretype=get_coretype_cpu(cpu);
#ifdef DEBUG
	char strout[256];
#endif
	int callback_flags=MM_TICK;

	if (event==PMC_SAVE_EVT || event==PMC_SELF_EVT) {
		do_count_mc_experiment(prof,core_exp,1);
		callback_flags|=MM_SAVE;
	}

	if (event==PMC_MIGRATION_EVT || event==PMC_SELF_EVT || (prof->pmc_jiffies_interval>0 && prof->pmc_jiffies_timeout <=jiffies)) {
		/* In tick() only read on demand */
		if (event==PMC_TICK_EVT) {
#ifdef DEBUG
			print_pmu_msr_values_debug(strout);
			printk (KERN_INFO "%s\n", strout);
#endif
			do_count_mc_experiment(prof,core_exp,1);
		}
	
		/* Prepare next timeout */
		prof->pmc_jiffies_timeout=jiffies+prof->pmc_jiffies_interval;

		/* Initialize sample*/
		switch(event) {
		case PMC_TICK_EVT:
		case PMC_SAVE_EVT:
			sample.type=PMC_TICK_SAMPLE;
			break;
		case PMC_MIGRATION_EVT:
			sample.type=PMC_MIGRATION_SAMPLE;
			break;
		default:
			sample.type=PMC_SELF_SAMPLE;
			break;
		}

		sample.coretype=cur_coretype;
		sample.exp_idx=core_exp->exp_idx;
		sample.pmc_mask=core_exp->used_pmcs;
		sample.nr_counts=core_exp->size;
		sample.virt_mask=0;
		sample.nr_virt_counts=0;
		sample.pid=prof->this_tsk->pid;

		/* Copy and clear samples in prof */
		for(i=0; i<MAX_LL_EXPS; i++) {
			sample.pmc_counts[i]=prof->pmc_values[i];
			prof->pmc_values[i]=0;
		}

		/* Call the monitoring module  */
		//if (prof->virt_counter_mask)
		mm_on_new_sample(prof,cpu,&sample,callback_flags,NULL);

		if (prof->pmc_samples_buffer) {
			/* Push current counter values into the buffer */
			spin_lock_irqsave(&prof->pmc_samples_buffer->lock,flags);
			__push_sample_cbuffer(prof->pmc_samples_buffer,&sample);
			spin_unlock_irqrestore(&prof->pmc_samples_buffer->lock,flags);
		}

		if (event==PMC_SAVE_EVT)
			mc_stop_all_counters(core_exp);

		/* Engage multiplexation */
		next=get_next_experiment_in_set(&prof->pmcs_multiplex_cfg[cur_coretype]);

		if (next && prof->pmcs_config!=next) {
			/* Clear all counters in the platform */
			mc_clear_all_platform_counters(get_pmu_props_coretype(cur_coretype));
			prof->pmcs_config=next;

			/* if not save callback => reconfigure counters as if it were the first time*/
			mc_restart_all_counters(prof->pmcs_config);
		}
	}
}

/*
 * This function is invoked from the tick processing 
 * function and context-switch related callbacks 
 * when the scheduler-driven TBS mode is enabled
 * for the current process. The function takes care of
 * reading PMC and virtual counters and pushes a new PMC sample
 * into the samples buffer if it is due time (e.g., end of the sampling interval)
 */
static inline void sample_counters_sched_tbs(pmon_prof_t* prof, core_experiment_t* core_exp, pmc_sampling_event_t event,  int cpu)
{
	int i=0;
	pmc_sample_t sample;
	int cur_coretype=get_coretype_cpu(cpu);

	switch (event) {
	case PMC_TICK_EVT:
		if(prof->pmc_ticks_counter >= (prof->nticks_sampling_period -1) ) {
			/* Read counters on the current cpu */
			do_count_mc_experiment(prof,core_exp,1);
			prof->samples_counter++;
			/*Sampling interval counter is reseted*/
			prof->pmc_ticks_counter = 0;

			/* Initialize sample*/
			sample.type=PMC_TICK_SAMPLE;
			sample.coretype=cur_coretype;
			sample.exp_idx=core_exp->exp_idx;
			sample.pmc_mask=core_exp->used_pmcs;
			sample.nr_counts=core_exp->size;
			sample.virt_mask=0;
			sample.nr_virt_counts=0;
			sample.pid=prof->this_tsk->pid;

			/* Copy and clear samples in prof */
			for(i=0; i<MAX_LL_EXPS; i++) {
				sample.pmc_counts[i]=prof->pmc_values[i];
				prof->pmc_values[i]=0;
			}

			/* Call the monitoring module (This one controls multiplexation if necessary) !! */
			mm_on_new_sample(prof,cpu,&sample,MM_TICK,NULL);

			/* Push sample if it's due time */
			push_sample_cbuffer(prof,&sample);
		} else {
			/*Performance tool sampling interval control sample is incremented*/
			prof->pmc_ticks_counter++;
		}
		break;
	case PMC_MIGRATION_EVT:
		/* Initialize sample*/
		sample.type=PMC_MIGRATION_SAMPLE;
		sample.coretype=cur_coretype;
		sample.exp_idx=core_exp->exp_idx;
		sample.pmc_mask=core_exp->used_pmcs;
		sample.nr_counts=core_exp->size;
		sample.virt_mask=0;
		sample.nr_virt_counts=0;
		sample.pid=prof->this_tsk->pid;

		/* Copy and clear samples in prof */
		for(i=0; i<MAX_LL_EXPS; i++) {
			sample.pmc_counts[i]=prof->pmc_values[i];
			prof->pmc_values[i]=0;
		}

		/* Call the monitoring module (This one controls multiplexation if necesary) !! */
		mm_on_new_sample(prof,cpu,&sample,MM_MIGRATION,&prof->pmc_ticks_counter);

		/* Push sample if it's due time */
		push_sample_cbuffer(prof,&sample);

		/*Sampling interval counter is reseted*/
		prof->pmc_ticks_counter = 0;
		break;
	default:
		break;
	}
}

/* Invoked when a context switch out takes place */ 
static void mod_save_callback(void* v_prof, int cpu)
{
	pmon_prof_t* prof=(pmon_prof_t*)v_prof;
	core_experiment_t* core_exp;
	struct cpu_pmc_buffer* cpu_pmc_buffer;
	unsigned long flags;

	/* System-wide monitoring mode has a higher priority than per-thread modes */
	if (syswide_monitoring_enabled() && !syswide_monitoring_switch_out(cpu))
		return;

	if (!prof || !prof->this_tsk->prof_enabled)
		return;

	core_exp = prof->pmcs_config?prof->pmcs_config:&per_cpu(cpu_exp, cpu);
	cpu_pmc_buffer=&per_cpu(pmc_buffer, cpu);


	switch(prof->profiling_mode) {
	case EBS_MODE:
		/* Save counter values in task_struct */
		if (!core_exp->need_setup)	/* If first time ==> do this to avoid storing a different reset value !! */
			mc_save_all_counters(core_exp);
		spin_lock_irqsave(&cpu_pmc_buffer->ptr_lock,flags);
		cpu_pmc_buffer->buf_lock=&cpu_pmc_buffer->default_buf_lock;
		cpu_pmc_buffer->pmc_samples_buffer=NULL;
		cpu_pmc_buffer->prof=NULL;
		cpu_pmc_buffer->exp=&per_cpu(cpu_exp, cpu);
		spin_unlock_irqrestore(&cpu_pmc_buffer->ptr_lock,flags);
		mc_stop_all_counters(core_exp);
		break;
	case TBS_SCHED_MODE:
		/* Just increase counts!! (But do not clear timing counters)
		   - This function sets performance counters to the reset value
		*/
		do_count_mc_experiment(prof,core_exp,1);
		mc_stop_all_counters(core_exp);
		break;
	case TBS_USER_MODE:
		sample_counters_user_tbs(prof,core_exp,PMC_SAVE_EVT,cpu);
		break;
	}

	mm_on_switch_out(prof);

	/* Update last CPU if it's not the first time */
	if (prof->last_cpu!=-1)
		prof->last_cpu=cpu;
}

/* Invoked when a context switch in takes place */ 
static void mod_restore_callback(void* v_prof, int cpu)
{
	pmon_prof_t* prof=(pmon_prof_t*)v_prof;
	core_experiment_t* core_exp,*next;
	struct cpu_pmc_buffer* cpu_pmc_buffer;
	unsigned long flags;
	int this_coretype=0,prev_coretype=0;
	int migration=0;

	/* System-wide monitoring mode has a higher priority than per-thread modes */
	if (syswide_monitoring_enabled() && !syswide_monitoring_switch_in(cpu))
		return;

	if (!prof || !prof->this_tsk->prof_enabled)
		return;

	core_exp = prof->pmcs_config?prof->pmcs_config:&per_cpu(cpu_exp, cpu);
	cpu_pmc_buffer=&per_cpu(pmc_buffer, cpu);

	mm_on_switch_in(prof);

	/* Update prev and cur coretype in the event of a migration */
	if (prof->last_cpu!=cpu) {
		migration=1;
		if (prof->last_cpu==-1)
			prev_coretype=-1;
		else
			prev_coretype=get_coretype_cpu(prof->last_cpu);

		this_coretype=get_coretype_cpu(cpu);
	}

	switch(prof->profiling_mode) {
	case EBS_MODE:
		/* Restore cpu_buffer (to non-ebs mode) */
		spin_lock_irqsave(&cpu_pmc_buffer->ptr_lock,flags);
		cpu_pmc_buffer->buf_lock=prof->pmc_samples_buffer==NULL?&cpu_pmc_buffer->default_buf_lock:&prof->pmc_samples_buffer->lock;
		cpu_pmc_buffer->pmc_samples_buffer=prof->pmc_samples_buffer;
		cpu_pmc_buffer->exp=prof->pmcs_config;
		cpu_pmc_buffer->prof=prof;
		/* Restore counters to pick up the count where we left off */
		mc_restore_all_counters(core_exp);
		spin_unlock_irqrestore(&cpu_pmc_buffer->ptr_lock,flags);
		break;
	case TBS_SCHED_MODE:
		/* notify migration (to do whatever magic necesary) */
		if (migration && prof->pmcs_config && (prev_coretype!=this_coretype)) {
			if (prof->last_cpu!=-1) {
				sample_counters_sched_tbs(prof,core_exp,PMC_MIGRATION_EVT,prof->last_cpu);
				/* This one may handle multiplexation as necessary */
			}

			/* Point to the right set of experiments for this core type */
			next=rewind_experiments_in_set(&prof->pmcs_multiplex_cfg[this_coretype]);

			/* If configuration is enabled and there are events on the new core type */
			if (next && prof->pmcs_config!=next) {
				/* Clear all counters in the platform */
				mc_clear_all_platform_counters(get_pmu_props_coretype(this_coretype));
				prof->pmcs_config=next;
				/* reconfigure counters as if it were the first time*/
				mc_restart_all_counters(prof->pmcs_config);
			}
		} else {
			/* Reprogram all the counters safely from here */
			mc_clear_all_counters(core_exp);
			mc_restart_all_counters(core_exp);
		}

		if (migration)
			/* Notify the monitoring module (to make it possible to do hacks or whatever)
					- Note that last CPU will be -1 the first time */
			mm_on_migrate(prof, prof->last_cpu, cpu);

		break;
	case TBS_USER_MODE:
		if (migration && prof->pmcs_config && (prev_coretype!=this_coretype)) {

			/* Dump sample if its' not the first time */
			if (prof->last_cpu!=-1)
				sample_counters_user_tbs(prof,core_exp,PMC_MIGRATION_EVT,prof->last_cpu);

			/* Always engage multiplexation and clear counts (start from the beginning) */
			next=rewind_experiments_in_set(&prof->pmcs_multiplex_cfg[this_coretype]);

			/* If configuration is enabled */
			if (next && prof->pmcs_config!=next) {
				/* Clear all counters in the platform */
				mc_clear_all_platform_counters(get_pmu_props_coretype(this_coretype));
				prof->pmcs_config=next;
				/* reconfigure counters as if it were the first time*/
				mc_restart_all_counters(prof->pmcs_config);
			}
		} else {
			/* Reprogram all the counters safely from here */
			mc_clear_all_counters(core_exp);
			mc_restart_all_counters(core_exp);
		}
		break;
	}

	/* Update last context switch timestamp */
	prof->context_switch_timestamp=jiffies;
	prof->last_cpu=cpu; /* Update CPU */
}

/* Invoked from scheduler_tick() */ 
static void mod_tbs_tick(void* v_prof, int cpu)
{
	pmon_prof_t* prof=(pmon_prof_t*)v_prof;
	core_experiment_t* core_exp;

	if (!prof || !prof->this_tsk->prof_enabled)
		return;

	core_exp = prof->pmcs_config?prof->pmcs_config:&per_cpu(cpu_exp, cpu);

	switch(prof->profiling_mode) {
	case EBS_MODE: /* In EBS-mode there is nothing to be done on tick */
		break;
	case TBS_SCHED_MODE:
		sample_counters_sched_tbs(prof,core_exp,PMC_TICK_EVT,cpu);
		break;
	case TBS_USER_MODE:
		sample_counters_user_tbs(prof,core_exp,PMC_TICK_EVT,cpu);
		break;
	}
}

/* Invoked when a process calls exec() */ 
static void  mod_exec_thread(struct task_struct* tsk)
{
	pmon_prof_t *prof= (pmon_prof_t*)tsk->pmc;

	if(prof == NULL || !tsk->prof_enabled )
		return;

	/* Just notify the monitoring module */
	mm_on_exec(prof);
}

/* Invoked when a process calls exec() */ 
static void mod_exit_thread(struct task_struct* tsk)
{
	unsigned long flags;
	pmon_prof_t *prof= (pmon_prof_t*)tsk->pmc;
	core_experiment_t* core_exp;
	pmc_sample_t sample;
	int i=0;
	int cur_coretype=get_coretype_cpu(smp_processor_id());


	if(prof == NULL) return;

	core_exp = prof->pmcs_config?prof->pmcs_config:&per_cpu(cpu_exp, smp_processor_id());

	if (unlikely(is_syswide_monitor(tsk)))
		syswide_monitoring_stop();

	switch (prof->profiling_mode) {
	case TBS_SCHED_MODE:
	case TBS_USER_MODE:

		do_count_mc_experiment(prof,core_exp,1);

		/* Prepare next timeout (infinite hack) */
		prof->pmc_jiffies_timeout=jiffies+250*5000;

		/* Initialize sample*/
		sample.type=PMC_EXIT_SAMPLE;
		sample.coretype=cur_coretype;
		sample.exp_idx=core_exp->exp_idx;
		sample.pmc_mask=core_exp->used_pmcs;
		sample.nr_counts=core_exp->size;
		sample.virt_mask=0;
		sample.nr_virt_counts=0;
		sample.pid=prof->this_tsk->pid;

		/* Copy and clear samples in prof */
		for(i=0; i<MAX_LL_EXPS; i++) {
			sample.pmc_counts[i]=prof->pmc_values[i];
			prof->pmc_values[i]=0;
		}

		//if (prof->virt_counter_mask)
		mm_on_new_sample(prof,smp_processor_id(),&sample,MM_EXIT,NULL);


		if (prof->pmc_samples_buffer) {
			/* Push current counter values into the buffer */
			spin_lock_irqsave(&prof->pmc_samples_buffer->lock,flags);

			//Common for everything
			prof->flags|=PMC_EXITING;

			__push_sample_cbuffer(prof->pmc_samples_buffer,&sample);

			spin_unlock_irqrestore(&prof->pmc_samples_buffer->lock,flags);
		}

		break;

	case EBS_MODE:

		if (prof->pmc_samples_buffer) {
			/* Just Notify termination */
			spin_lock_irqsave(&prof->pmc_samples_buffer->lock,flags);

			//Common for everything
			prof->flags|=PMC_EXITING;

			__wake_up_monitor_program(prof->pmc_samples_buffer);

			spin_unlock_irqrestore(&prof->pmc_samples_buffer->lock,flags);
		}

		break;
	default:
		break;
	}
	/* Notify that thread is now exiting */
	mm_on_exit(prof);

	/* Deallocate memory from a previous assignment */
	if (prof->pmc_samples_buffer) {
		put_pmc_samples_buffer(prof->pmc_samples_buffer);
		prof->pmc_samples_buffer=NULL;
	}
}

/* 
 * Invoked from the code of experimental scheduling classes that leverage per-thread performance
 * counter data when making scheduling decisions. 
 */ 
static int mod_get_current_metric_value(struct task_struct* task, int key, uint64_t* value)
{
	pmon_prof_t *prof= (pmon_prof_t*)task->pmc;

	if(!prof)
		return -1;
	else
		return mm_get_current_metric_value(prof,key,value);
}

/* Invoked when the kernel frees up the process descriptor */ 
static void mod_free_per_thread_data(struct task_struct* tsk)
{
	pmon_prof_t *prof= (pmon_prof_t*)tsk->pmc;
	int i=0;

	if(prof == NULL) return;

	/* Notify the monitoring module */
	mm_on_free_task(prof);

	/* Disable profiling no matter what */
	tsk->prof_enabled = 0;

	/* Deallocate memory from thread-specific PMC data if any */
	if (prof->pmcs_config) {
		for (i=0; i<AMP_MAX_CORETYPES; i++)
			free_experiment_set(&prof->pmcs_multiplex_cfg[i]);
		prof->pmcs_config=NULL;
	}

	if (prof->pmc_user_samples) {
		kfree(prof->pmc_user_samples);
		prof->pmc_user_samples=NULL;
	}

	if (prof->pmc_kernel_samples) {
		/* Remember: this is a shared page */
		free_page((unsigned long)prof->pmc_kernel_samples);
		prof->pmc_kernel_samples=NULL;
	}

	kfree(tsk->pmc);
	tsk->pmc = NULL;
}


/*** Implementation of /proc/pmc/- callback functions **/

/* Write callback for /proc/pmc/config */
static ssize_t proc_pmc_config_write(struct file *filp, const char __user *buff, size_t len, loff_t *off)
{
	int val;
	char *kbuf;
	int ret=len;

	if (*off>0)
		return 0;

	if ((kbuf=vmalloc(len+1))==NULL)
		return -ENOMEM;

	if (copy_from_user(kbuf,buff,len)) {
		vfree(kbuf);
		return -EFAULT;
	}

	kbuf[len]='\0';

	if(sscanf(kbuf,"nticks %i",&val)==1 && val>0) {
		printk("--Setting nticks\n");
		pmcs_pmon_config.pmon_nticks = val;
	} else if(sscanf(kbuf,"kernel_buffer_size %i",&val)==1 && val>0) {
		unsigned int new_size=(val/sizeof(pmc_sample_t))*sizeof(pmc_sample_t);
		if (new_size == 0)
			ret=-EINVAL;
		else
			pmcs_pmon_config.pmon_kernel_buffer_size=new_size;
	} else if (strncmp(kbuf, "selfcfg pmc",11)==0) {
		/* For now configure for all cpus*/
		val=configure_performance_counters_thread(kbuf+8,current,0);
		if (val!=0)
			ret=val;
	} else if (strncmp(kbuf, "selfcfg virt",12)==0) {
		/* For now configure for all cpus*/
		val=configure_virtual_counters_thread(kbuf+8,current,0);
		if (val!=0)
			ret=val;

	} else if (strncmp(kbuf, "pmc",3)==0) {
		/* For now configure for all cpus*/
		val=configure_performance_counters_thread(kbuf,current,1);
		if (val!=0)
			ret=val;
	} else if (strncmp(kbuf, "virt",4)==0) {
		/* For now configure for all cpus*/
		val=configure_virtual_counters_thread(kbuf,current,1);
		if (val!=0)
			ret=val;

	} else if (sscanf(kbuf, "nticks_t %i",&val)==1 && val>0) {
		pmon_prof_t* prof=(pmon_prof_t*)current->pmc;

		if (prof) {
			prof->nticks_sampling_period=val;
		}
	} else if (sscanf(kbuf, "timeout %i",&val)==1 && val>0) {
		pmon_prof_t* prof=(pmon_prof_t*)current->pmc;
		int new_interval=(HZ*val)/1000;

		if (prof) {
			prof->pmc_jiffies_interval=new_interval;
		}
	} else if(sscanf(kbuf,"kernel_buffer_size_t %i",&val)==1 && val>0) {
		pmon_prof_t* prof=(pmon_prof_t*)current->pmc;

		if (prof) {
			unsigned int new_size=(val/sizeof(pmc_sample_t))*sizeof(pmc_sample_t);
			if (new_size == 0)
				ret=-EINVAL;
			else
				prof->kernel_buffer_size=new_size;
		}
	} else if ((val=mm_on_write_config(kbuf,len))!=0) {
		ret=val;
	} else
		ret=-EINVAL;

	if (ret>0)
		(*off)+=ret;

	vfree(kbuf);
	return ret;
}

/* Read callback for /proc/pmc/config */
static ssize_t proc_pmc_config_read(struct file *filp, char __user *buf, size_t mlen, loff_t *off)
{
	int len=0,err=0;
	char* kbuf;
	char* dst;

	if (*off>0)
		return 0;

	kbuf=vmalloc(PAGE_SIZE);

	if (!kbuf)
		return -ENOMEM;

	dst=kbuf;

	/* Configuration parameters first */
	dst+=sprintf(dst,"nticks = %d\n",
	             pmcs_pmon_config.pmon_nticks);
	dst+=sprintf(dst,"kernel_buffer_size = %u bytes (%lu samples)\n",
	             pmcs_pmon_config.pmon_kernel_buffer_size,
	             pmcs_pmon_config.pmon_kernel_buffer_size/sizeof(pmc_sample_t));

	err=mm_on_read_config(dst,PAGE_SIZE-(dst-kbuf-1));

	if (err<0)
		goto err_config_read;

	/* Add len */
	dst+=err;
	len=dst-kbuf;

	if (copy_to_user(buf,kbuf,len)) {
		err=-EFAULT;
		goto err_config_read;
	}

	(*off)+=len;
	return len;
err_config_read:
	if (kbuf)
		vfree(kbuf);
	return err;
}

#define MAX_STR_CONFIG_LEN 100

/* Write callback for /proc/pmc/monitor */
static ssize_t proc_monitor_pmcs_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
	int val;
	pid_t pid;
	struct task_struct* tsM;
	pmon_prof_t* monitor;
	pmon_prof_t* monitored;
	pmon_prof_t* prof;
	unsigned long flags;
	pmc_samples_buffer_t* pmc_buf=NULL;
	cpu_pmc_buffer_t*  cpu_buffer=this_cpu_ptr(&pmc_buffer);
	char kbuf[MAX_STR_CONFIG_LEN]="";

	if (len>=MAX_STR_CONFIG_LEN)
		return -ENOSPC;

	if (copy_from_user(kbuf,buf,len))
		return -EFAULT;

	kbuf[len]='\0';

	if(sscanf(kbuf,"pid_monitor %i", &val)==1 && val>0) {
		pid = (pid_t)val;
		rcu_read_lock();
		tsM = find_process_by_pid(pid);
		if(!tsM) {
			rcu_read_unlock();
			return -ESRCH;
		}
		/* Prevent tsM going away */
		get_task_struct(tsM);
		rcu_read_unlock();

		monitor= (pmon_prof_t*)current->pmc;
		monitored = (pmon_prof_t*)tsM->pmc;

		if (!monitored) {
			put_task_struct(tsM);
			return -ESRCH;
		} else {
			monitor->pmc_samples_buffer=monitored->pmc_samples_buffer;
			/* Increase ref count */
			get_pmc_samples_buffer(monitor->pmc_samples_buffer);
			put_task_struct(tsM);
		}
	} else if (strncmp(kbuf,"ON",2)==0) {
		pmon_prof_t* prof= (pmon_prof_t*)current->pmc;
		if (!prof->pmcs_config)
			return -1;

		/* Allocate memory for the buffer sample */
		if (!prof->pmc_samples_buffer) {
			pmc_buf=allocate_pmc_samples_buffer(prof->kernel_buffer_size);
			if (pmc_buf == NULL) {
				printk(KERN_INFO "Can't allocate memory to store buffer samples\n");
				return -1;
			}
		}

		spin_lock_irqsave(&cpu_buffer->ptr_lock,flags);
		/* Assign newly created data */
		if (!prof->pmc_samples_buffer)
			prof->pmc_samples_buffer=pmc_buf;

		prof->last_cpu=smp_processor_id();
		current->prof_enabled=1;
		prof->flags|=PMC_SELF_MONITORING;

		/* Set up jiffies interval if it wasn't set previously */
		if (prof->pmc_jiffies_interval<0)
			prof->pmc_jiffies_interval=HZ; /* Default one second */

		prof->pmc_jiffies_timeout=jiffies+prof->pmc_jiffies_interval;

		/* Reprogram all the counters safely from here */
		mc_clear_all_counters(prof->pmcs_config);
		mc_restart_all_counters(prof->pmcs_config);
		spin_unlock_irqrestore(&cpu_buffer->ptr_lock,flags);

	} else if (strncmp(kbuf,"OFF",3)==0) {
		prof= (pmon_prof_t*)current->pmc;
		spin_lock_irqsave(&cpu_buffer->ptr_lock,flags);
		sample_counters_user_tbs(prof,prof->pmcs_config,PMC_SELF_EVT,smp_processor_id());
		current->prof_enabled=0;
		spin_unlock_irqrestore(&cpu_buffer->ptr_lock,flags);
	}
	/* Syswide monitoring can be started/stopped using this /proc entry as well
		 to simplify libpmctrack implementation */
	else if (strcmp(kbuf,"syswide on")==0) {
		if ((val=syswide_monitoring_start()))
			return val;
	} else if (strcmp(kbuf,"syswide off")==0) {
		if ((val=syswide_monitoring_stop()))
			return val;
	} else
		return -EINVAL;
	return len;
}


/* Read callback for /proc/pmc/monitor */
static ssize_t proc_monitor_pmcs_read (struct file *filp, char __user *buf, size_t len, loff_t *off)
{
	int lentotal;
	pmon_prof_t *prof_mon;
	unsigned long flags;
	pmc_samples_buffer_t* pmcbuf;
	pmc_sample_t* dst_buffer=NULL;
	unsigned int dst_buffer_size=len;


	lentotal=0;
	prof_mon = (pmon_prof_t*)current->pmc;

	if(prof_mon == NULL)
		return 0;

	if ((pmcbuf=prof_mon->pmc_samples_buffer)==NULL)
		return -ENOENT;

	/* 
	 * Use shared buffer between kernel and userspace if provided...
	 * (The user must pass it as a parameter to the read call) 
	 */
	if (prof_mon->pmc_kernel_samples) {
		dst_buffer=prof_mon->pmc_kernel_samples;
		dst_buffer_size=PAGE_SIZE; /* This buffer is as big as a page */
	}
	/* Allocate memory the first time (if no dst_buffer available) */
	if (!dst_buffer || !prof_mon->pmc_user_samples) {
		prof_mon->pmc_user_samples=kmalloc(len,GFP_KERNEL);
		if (!prof_mon->pmc_user_samples)
			return -ENOMEM;
	}

	if (!dst_buffer)
		dst_buffer=prof_mon->pmc_user_samples;

	/* Restrict max size */
	if (dst_buffer_size>len)
		dst_buffer_size=len;

	/* Prevent the perf interrupt to kick in when trying to do this */
	spin_lock_irqsave(&pmcbuf->lock,flags);

	if (prof_mon->flags & PMC_SELF_MONITORING) {
		prof_mon->flags&=~PMC_SELF_MONITORING;
		goto read_buffer_now;
	}

	/* EOF if all threads actually finished */
	if (get_pmc_samples_buffer_refs(pmcbuf)<=1 && is_empty_cbuffer_t(pmcbuf->pmc_samples)) {
		spin_unlock_irqrestore(&pmcbuf->lock,flags);
		return 0;
	}

	while (is_empty_cbuffer_t(pmcbuf->pmc_samples)) {
		pmcbuf->monitor_waiting=1;

		spin_unlock_irqrestore(&pmcbuf->lock,flags);

		/* Wait until there are samples here */
		if (down_interruptible(&pmcbuf->sem_queue)) {
			pmcbuf->monitor_waiting=0;
			return EINTR;
		}

		spin_lock_irqsave(&pmcbuf->lock,flags);

		/* EOF if all threads actually finished */
		if (get_pmc_samples_buffer_refs(pmcbuf)<=1 && is_empty_cbuffer_t(pmcbuf->pmc_samples)) {
			spin_unlock_irqrestore(&pmcbuf->lock,flags);
			return 0;
		}
	}

read_buffer_now:
	/* Bytes to be copied to the user buffer */
	lentotal=remove_cbuffer_t_batch(pmcbuf->pmc_samples,(char*)dst_buffer,dst_buffer_size);

	spin_unlock_irqrestore(&pmcbuf->lock,flags);

	/* Invoke copy to user if necessary */
	if (!prof_mon->pmc_kernel_samples && copy_to_user(buf,dst_buffer,lentotal)) {
		return -EFAULT;
	}
#ifdef DEBUG
	{
		int n=lentotal/sizeof(pmc_sample_t);
		pmc_sample_t* cur=prof_mon->pmc_user_samples;

		printk (KERN_INFO "Samples read: %d\n",n);
		while(n--) {
			printk (KERN_INFO "IDX: %d\n",cur->exp_idx);
			cur++;
		}
	}
#endif
	return lentotal;
}

/* 
 * Operations to allocate a shared page between 
 * the monitor process (user-space program) and the 
 * kernel module.
 *
 * To obtain a reference to the shared page, the monitor process must 
 * open the /proc/pmc/monitor file and attempt to mmap() the file.
 * Upon success, the pointer returned by mmap is a virtual
 * address to access the shared page where PMC and virtual
 * counter values will be stored.
 */
static void mmap_open(struct vm_area_struct *vma) { }

static void mmap_close(struct vm_area_struct *vma) { }

static int mmap_nopage(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	pgoff_t offset = vmf->pgoff;

	if (offset > vma->vm_end) {
		printk(KERN_ALERT "invalid address\n");
		return VM_FAULT_SIGBUS;
	}

	/* Make sure data is there */
	if(!vma->vm_private_data) {
		printk(KERN_ALERT "No shared data in here\n");
		return VM_FAULT_SIGBUS;
	}

	/* Get physical address from virtual page */
	vmf->page = virt_to_page(vma->vm_private_data);
	/* Bring page to main memory */
	get_page(vmf->page);

	return 0;
}

/* Instantiation of the VMOPS interface */
static struct vm_operations_struct mmap_vm_ops = {
	.open =    mmap_open,
	.close =   mmap_close,
	.fault =   mmap_nopage,
};

/* mmap() operation for /proc/pmc/monitor */
static int proc_monitor_pmcs_mmap(struct file *filp, struct vm_area_struct *vma)
{
	pmc_sample_t *handler;
	pmon_prof_t* prof=(pmon_prof_t*)current->pmc;

	if (!prof || prof->pmc_kernel_samples) /* NULL or shared page already reserved */
		return -EINVAL;

	vma->vm_ops = &mmap_vm_ops;	/* Set up callbacks for this entry*/
#ifdef CONFIG_PMC_PHI /* Older kernel */
	vma->vm_flags |= VM_RESERVED|VM_MAYREAD|VM_MAYSHARE|VM_READ|VM_SHARED; /* Initialize flags*/
#else
	vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP |VM_MAYREAD|VM_MAYSHARE|VM_READ|VM_SHARED; /* Initialize flags*/
#endif
	/* Allocate zero-filled page as the shared memory region */
	if ((handler = (pmc_sample_t *)get_zeroed_page(GFP_KERNEL))==NULL) {
		printk(KERN_ALERT "Can't allocate shared memory region");
		return -ENOMEM;
	}

	/* Assign shared memory to monitor process and to vm structure */
	prof->pmc_kernel_samples=handler;
	vma->vm_private_data = handler;

	mmap_open(vma);	/* Force open shared memory region (defined above) */
#ifdef DEBUG	
	trace_printk("monitor_mmap: Invoking mmap %p\n",handler);
#endif	
	return 0;
}

/* Write callback for /proc/pmc/enable */
static ssize_t proc_pmc_enable_write(struct file *filp, const char __user *buff, size_t len, loff_t *off)
{
	pmon_prof_t* prof=(pmon_prof_t*)current->pmc;
	char kbuf[MAX_STR_CONFIG_LEN];
	pmc_samples_buffer_t* pmc_buf=NULL;
	cpu_pmc_buffer_t*  cpu_buffer=this_cpu_ptr(&pmc_buffer);
	unsigned long flags=0;
	int error=0;

	if (len>=MAX_STR_CONFIG_LEN || copy_from_user(kbuf,buff,len))
		return -EFAULT;

	kbuf[len]='\0';

	if (strcmp(kbuf,"ON")==0 && prof!=NULL) {

		/* Allocate memory for the buffer sample if necessary */
		if (!prof->pmc_samples_buffer) {
			pmc_buf=allocate_pmc_samples_buffer(prof->kernel_buffer_size);
			if (pmc_buf == NULL) {
				printk(KERN_INFO "Can't allocate memory to store buffer samples\n");
				return -1;
			}
		}

		/* Prevent the perf interrupt to kick in when trying to do this */
		spin_lock_irqsave(&cpu_buffer->ptr_lock,flags);

		/* Assign newly created data */
		if (!prof->pmc_samples_buffer)
			prof->pmc_samples_buffer=pmc_buf;

		/* Reprogram all the counters safely from here */
		if (prof->pmcs_config) {
			mc_clear_all_counters(prof->pmcs_config);
			mc_restart_all_counters(prof->pmcs_config);
		}
		spin_unlock_irqrestore(&cpu_buffer->ptr_lock,flags);

		/* Set up jiffies interval if it wasn't set previously */
		if (prof->pmc_jiffies_interval<0)
			prof->pmc_jiffies_interval=250; /* One second (for now) */

		prof->pmc_jiffies_timeout=jiffies+prof->pmc_jiffies_interval;

		current->prof_enabled=1;
	} else if (strcmp(kbuf,"OFF")==0) {
		printk("--Disabling profiling for PID %d. (It was %d)\n",current->pid,(int)current->prof_enabled);
		current->prof_enabled=0;
	} else if (strcmp(kbuf,"syswide on")==0) {
		if ((error=syswide_monitoring_start()))
			return error;
	} else if (strcmp(kbuf,"syswide off")==0) {
		if ((error=syswide_monitoring_stop()))
			return error;
	} else if (strcmp(kbuf,"syswide pause")==0) {
		if ((error=syswide_monitoring_pause()))
			return error;
	} else if (strcmp(kbuf,"syswide resume")==0) {
		if ((error=syswide_monitoring_resume()))
			return error;
	}
#ifdef SCHED_AMP
	else if (strcmp(kbuf,"ON_SF")==0 && prof!=NULL) {
		prof->flags|=PMCTRACK_SF_NOTIFICATIONS;
		current->prof_enabled=1;
	} else
		return -EINVAL;
#endif
	return len;
}

/* Read callback for /proc/pmc/enable */
static ssize_t proc_pmc_enable_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
	int nbytes;
	char kbuf[50]="";

	if (*off>0)
		return 0;

	if(current->prof_enabled)
		nbytes=sprintf(kbuf,"profiling is ON\n");
	else
		nbytes=sprintf(kbuf,"profiling is OFF\n");

	if (len<nbytes)
		return -ENOMEM;

	if (copy_to_user(buf,kbuf,nbytes))
		return -EFAULT;

	(*off)+=nbytes;

	return nbytes;
}


/*
 * Parse a virtual-counter configuration string in the RAW 
 * format and return the number of virtual counters used, a
 * bitmask with the virtual counters used, 
 * as well as the highest virtual counter ID referenced in the RAW string.
*/
static int parse_virtual_strconfig(const char *buf,
                                   unsigned int* used_pmcs_mask,
                                   unsigned int* nr_pmcs,
                                   unsigned int* highest_virt_pmc)
{
	int read_tokens=0;
	int idx;
	char* strconfig=(char*)buf;
	char* flag;
	int curr_flag=0;
	unsigned int used_pmcs=0; /* Mask to indicate which counters are actually used*/
	int error=0;
	unsigned int pmc_count=0;
	int max_id=0;

	while((flag = strsep(&strconfig, ","))!=NULL) {
		if((read_tokens=sscanf(flag,"virt%i", &idx))>0) {
			if ((idx<0 || idx>=MAX_VIRTUAL_COUNTERS)) {
				error=1;
				break;
			}
			used_pmcs|=(0x1<<idx);
			pmc_count++;

			if (idx>max_id)
				max_id=idx;

		} else {
			error=1;
			break;
		}

		curr_flag++;
	}

	if (error) {
		printk("Unrecognized format in virtual flag %s\n",flag);
		return curr_flag+1;
	} else {
		(*used_pmcs_mask)=used_pmcs;
		(*nr_pmcs)=pmc_count;
		(*highest_virt_pmc)=max_id;
		return 0;
	}
}


/* 
 * This function accepts a user-provided PMC configuration string (buf) in the raw format,
 * tranforms the configuration into a core_experiment_t structure (low-level representation of
 * a set of PMC events) and assigns the structure to a given process (p).
 */
static int configure_virtual_counters_thread(const char *buf,struct task_struct* p, int system_wide)
{
	unsigned int used_virt=0;
	int error=0;
	unsigned int nr_virt_pmcs=0;
	pmon_prof_t *prof= (pmon_prof_t*)p->pmc;
	pmc_samples_buffer_t* pmc_buf=NULL;
	unsigned long flags=0;
	unsigned int highest_virt_pmc=0;
	monitoring_module_counter_usage_t usage;
	cpu_pmc_buffer_t*  cpu_buffer=this_cpu_ptr(&pmc_buffer);

	/* Make sure prof structure exists for this thread */
	if(prof == NULL)
		return -1;

	/* Check user-provided configuration */
	error=parse_virtual_strconfig(buf,&used_virt,&nr_virt_pmcs,&highest_virt_pmc);

	if (error) {
		return error;
	}

	/* Figure out virtual counters available */
	mm_module_counter_usage(&usage);

	/* No such virtual counter */
	if (highest_virt_pmc>=usage.nr_virtual_counters) {
		printk("No such virtual counter: %u",highest_virt_pmc);
		return -1;
	}

	/* Allocate memory for the buffer sample if necessary */
	if (!prof->pmc_samples_buffer) {

		if (system_wide)
			prof->kernel_buffer_size=sizeof(pmc_sample_t)*nr_cpu_ids; /* Number of possible CPUs */

		pmc_buf=allocate_pmc_samples_buffer(prof->kernel_buffer_size);
		if (pmc_buf == NULL) {
			printk(KERN_INFO "Can't allocate memory to store buffer samples\n");
			return -1;
		}
	}

	/* Prevent the perf interrupt to kick in when trying to do this */
	spin_lock_irqsave(&cpu_buffer->ptr_lock,flags);

	/* If the user only specified virtual counters and
	    the monitoring module is not using performance counters at all
		(default TBS_SCHED_MODE was not changed)
		 the TBS mode must be selected  */
	if (prof->profiling_mode==TBS_SCHED_MODE && !usage.hwpmc_mask)
		prof->profiling_mode=TBS_USER_MODE;

	/* Assign newly created data if necessary */
	if (!prof->pmc_samples_buffer)
		prof->pmc_samples_buffer=pmc_buf;

	prof->virt_counter_mask=used_virt;

	spin_unlock_irqrestore(&cpu_buffer->ptr_lock,flags);

	return 0;
}

/* 
 * This function accepts a user-provided virtual configuration string (buf) in the raw format,
 * and assigns the underlying configuration to a given process (p).
 */
static int configure_performance_counters_thread(const char *buf,struct task_struct* p, int system_wide)
{
	pmc_usrcfg_t pmc_cfg[MAX_LL_EXPS];
	unsigned int used_pmcs=0;
	int error=0;
	unsigned int nr_pmcs=0;
	pmon_prof_t *prof= (pmon_prof_t*)p->pmc;
	core_experiment_t* exp[AMP_MAX_CORETYPES];
	pmc_samples_buffer_t* pmc_buf=NULL;
	unsigned long flags=0;
	int ebs_index=0;
	int coretype=0;
	int i=0,j=0;
	monitoring_module_counter_usage_t usage;
	cpu_pmc_buffer_t*  cpu_buffer=this_cpu_ptr(&pmc_buffer);

	/* Make sure prof structure exists for this thread */
	if(prof == NULL)
		return -1;

	/* Figure out virtual counters available */
	mm_module_counter_usage(&usage);

	if (usage.hwpmc_mask) {
		printk(KERN_INFO "The current monitoring module is using performance counters!!\n");
		return -1;
	}

	/* Check user-provided configuration */
	error=parse_pmcs_strconfig(buf,1,pmc_cfg,&used_pmcs,&nr_pmcs,&ebs_index,&coretype);

	if (error)
		return error;

	if (system_wide && ebs_index!=-1) {
		printk(KERN_INFO "EBS is not supported in system-wide mode\n");
		return -EINVAL;
	}

	/* Make sure that multiplexation is only enabled when possible and EBS encompasses just one event set */
	if ((prof->profiling_mode==EBS_MODE) ||
	    (prof->profiling_mode==TBS_USER_MODE && ebs_index!=-1)
	   ) {
		printk(KERN_INFO "EBS is only supported with one event set\n");
		return -EINVAL;
	}

	/* Allocate memory for the buffer sample */
	if (!prof->pmc_samples_buffer) {

		if (system_wide)
			prof->kernel_buffer_size=sizeof(pmc_sample_t)*nr_cpu_ids; /* Number of possible CPUs */

		pmc_buf=allocate_pmc_samples_buffer(prof->kernel_buffer_size);
		if (pmc_buf == NULL) {
			printk(KERN_INFO "Can't allocate memory to store buffer samples\n");
			return -1;
		}
	}

	/* Configured by a monitoring module !!! ==> Undo config */
	if (prof->profiling_mode==TBS_SCHED_MODE && prof->pmcs_config) {
		for (i=0; i<AMP_MAX_CORETYPES; i++) {
			free_experiment_set(&prof->pmcs_multiplex_cfg[i]);
		}
		prof->pmcs_config=NULL;
	}

	/* Allocate memory for PMCs/EVTSELs ...  */
	if (coretype!=-1) {
		exp[0]= (core_experiment_t*) kmalloc(sizeof(core_experiment_t), GFP_KERNEL);
		if(exp[0] == NULL) {
			printk(KERN_INFO "Can't allocate memory to store all pmcs configuration\n");
			put_pmc_samples_buffer(pmc_buf);
			return -1;
		}

		/*  Initialize structure for just one coretype */
		do_setup_pmcs(pmc_cfg,used_pmcs,exp[0],smp_processor_id(),0);

	} else {

		for (i=0; i<AMP_MAX_CORETYPES; i++) {
			exp[i]= (core_experiment_t*) kmalloc(sizeof(core_experiment_t), GFP_KERNEL);

			/* Free memory in case of failure */
			if(exp[i] == NULL) {
				printk(KERN_INFO "Can't allocate memory to store all pmcs configuration\n");
				for (j=0; j<i; j++)
					kfree(exp[j]);
				put_pmc_samples_buffer(pmc_buf);
				return -1;
			}
		}

		/*  Initialize structure for just one coretype */
		do_setup_pmcs(pmc_cfg,used_pmcs,exp[0],smp_processor_id(),0);

		/* Replicate for all */
		for (i=1; i<AMP_MAX_CORETYPES; i++)
			memcpy(exp[i],exp[0],sizeof(core_experiment_t));
	}

	/* Prevent the perf interrupt to kick in when trying to do this */
	spin_lock_irqsave(&cpu_buffer->ptr_lock,flags);

	/* Set up profiling mode (even in system-wide) */
	if (ebs_index==-1)
		prof->profiling_mode=TBS_USER_MODE;
	else
		prof->profiling_mode=EBS_MODE;

	/* Add to set */
	if (coretype==-1) {
		for (i=0; i<AMP_MAX_CORETYPES; i++)
			add_experiment_to_set(&prof->pmcs_multiplex_cfg[i],exp[i]);
	} else {
		add_experiment_to_set(&prof->pmcs_multiplex_cfg[coretype],exp[0]);
	}

	/* Assign newly created data */
	if (!prof->pmc_samples_buffer)
		prof->pmc_samples_buffer=pmc_buf;
	if (!prof->pmcs_config)
		prof->pmcs_config=exp[0];

	spin_unlock_irqrestore(&cpu_buffer->ptr_lock,flags);

#ifdef DEBUG
	{
		char log[256];
		char *dst=log;
		int i=0;
		/* Print thread-specific config*/
		for(i=0; i<prof->pmcs_config->size; i++) {
			low_level_exp* lle = &prof->pmcs_config->array[i];
			dst+=print_pmc_config(lle,dst);
		}
		printk(KERN_INFO "%s",log);
	}
#endif
	return 0;
}

/* 
 * Given a null-terminated array of raw-formatted PMC configuration 
 * string, store the associated low-level information into an array of core_experiment_set_t.  
 */
int configure_performance_counters_set(const char* strconfig[], core_experiment_set_t core_exp_set[], int nr_coretypes)
{
	int i=0,j=0,k=0;
	int nr_experiments=0;
	pmc_usrcfg_t pmc_cfg[AMP_MAX_EXP_CORETYPE][MAX_LL_EXPS];
	core_experiment_t* exp[AMP_MAX_CORETYPES];
	struct {
		unsigned int used_pmcs;
		unsigned int nr_pmcs;
		int ebs_index;
		int coretype;
	} aux_pmc_info[AMP_MAX_EXP_CORETYPE];
	int error=0;

	/* Initialize everything just in case*/
	for (i=0; i<nr_coretypes; i++)
		init_core_experiment_set_t(&core_exp_set[i]);

	i=0;
	/* Make sure strconfig is Okay !! */
	while (i<AMP_MAX_EXP_CORETYPE && strconfig[i]!=NULL) {
		/* Check provided configuration */
		error=parse_pmcs_strconfig(strconfig[i],1,pmc_cfg[i],&aux_pmc_info[i].used_pmcs,
		                           &aux_pmc_info[i].nr_pmcs,&aux_pmc_info[i].ebs_index,
		                           &aux_pmc_info[i].coretype);

		if (error)
			return error;

		if (aux_pmc_info[i].ebs_index!=-1) /* Not supported here */
			return -EINVAL;

		i++;
	}

	printk(KERN_ALERT "The string-based PMC configuration seems to be OK\n" );

	nr_experiments=i;

	for (i=0; i<nr_experiments; i++) {
		if (aux_pmc_info[i].coretype!=-1) {
			j=aux_pmc_info[i].coretype;
			exp[j]= (core_experiment_t*) kmalloc(sizeof(core_experiment_t), GFP_KERNEL);

			if(exp[j] == NULL) {
				printk(KERN_INFO "Can't allocate memory to store all pmcs configuration\n");
				for (k=0; k<nr_coretypes; k++)
					free_experiment_set(&core_exp_set[k]);
				return -ENOMEM;
			}

			/*  Initialize structure for just one coretype */
			do_setup_pmcs(pmc_cfg[i],aux_pmc_info[i].used_pmcs,exp[j],get_any_cpu_coretype(j),0);

			/* Add to set */
			add_experiment_to_set(&core_exp_set[j],exp[j]);
		} else {

			for (j=0; j<nr_coretypes; j++) {
				exp[j]= (core_experiment_t*) kmalloc(sizeof(core_experiment_t), GFP_KERNEL);

				/* Free memory in case of failure */
				if(exp[j] == NULL) {
					printk(KERN_INFO "Can't allocate memory to store all pmcs configuration\n");
					for (k=0; k<j; k++)
						kfree(exp[k]);
					for (k=0; k<nr_coretypes; k++)
						free_experiment_set(&core_exp_set[k]);
					return -ENOMEM;
				}
			}

			/*  Initialize structure for just one coretype */
			do_setup_pmcs(pmc_cfg[i],aux_pmc_info[i].used_pmcs,exp[0],get_any_cpu_coretype(0),0);

			/* Replicate for all */
			for (j=1; j<nr_coretypes; j++)
				memcpy(exp[j],exp[0],sizeof(core_experiment_t));

			/* Add experiments to set */
			for (j=0; j<nr_coretypes; j++)
				add_experiment_to_set(&core_exp_set[j],exp[j]);
		}
	}

	return 0;
}


/* Initialize global parameters of the kernel module */
void init_pmon_config_t(void)
{
#ifdef SCHED_AMP
	pmcs_pmon_config.pmon_nticks = HZ/5; /* 200ms when the scheduler is using the counters */
#else
	pmcs_pmon_config.pmon_nticks = HZ; /* Set superhigh for testing purposes (one second) */
#endif
	pmcs_pmon_config.pmon_kernel_buffer_size=BUF_LEN_PMC_SAMPLES_EBS_KERNEL;
}


static char* pmc_supported_properties[]= {"cpumask","pmcmask","nr_pmcs","nr_experiments",NULL};

/* Write callback for /proc/pmc/properties */
static ssize_t proc_pmc_properties_write(struct file *filp, const char __user *buff, size_t len, loff_t *off)
{
#define MAX_PROP_SIZE 50
	char kbuf[MAX_PROP_SIZE+1];
	char user_prop[MAX_PROP_SIZE+1];
	char* prop;
	int i=0;

	if (len>MAX_PROP_SIZE)
		return -ENOSPC;

	if (copy_from_user(kbuf,buff,len))
		return -EFAULT;

	kbuf[len]='\0';

	/* Read property by writing its name here and reading afterwards */
	if(sscanf(kbuf,"get %s",user_prop)==1) {
		/* Search property */
		while ((prop=pmc_supported_properties[i++])!=NULL && strncmp(user_prop,prop,strlen(prop))!=0) {}

		if (prop!=NULL)
			filp->private_data=prop;

		return len;
	}
	return 0;
}

/* Read callback for /proc/pmc/properties */
static ssize_t proc_pmc_properties_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
	char kbuf[60];
	char* dst=kbuf;
	uint64_t cpumaskval=0;
	monitoring_module_counter_usage_t usage;
	int nr_counters_used=0;
	int tmp_mask=0;
	int cpu=0;
	char* cur_property=filp->private_data;

	if (*off>0)
		return 0;

	if (cur_property==NULL || strcmp(cur_property,"cpumask")==0) {
		/* Configuration parameters first */
		/* setup actual counters in the various cpus*/
		for_each_cpu(cpu,cpu_online_mask) {
			cpumaskval|=(0x1ULL<<cpu);
		}
		dst+=sprintf(dst,"cpumask=0x%012llX\n\n",cpumaskval);

	} else if (strcmp(cur_property,"pmcmask")==0) {
		mm_module_counter_usage(&usage);
		dst+=sprintf(dst,"pmcmask=0x%x\n",usage.hwpmc_mask);
	} else if (strcmp(cur_property,"nr_pmcs")==0) {
		mm_module_counter_usage(&usage);
		tmp_mask=usage.hwpmc_mask;
		//Figure out nr_counters_from_mask
		while (tmp_mask) {
			if (tmp_mask & (0x1))
				nr_counters_used++;
			tmp_mask=tmp_mask>>1;
		}
		dst+=sprintf(dst,"nr_pmcs=%d\n",nr_counters_used);
	} else if (strcmp(cur_property,"nr_experiments")==0) {
		mm_module_counter_usage(&usage);
		dst+=sprintf(dst,"nr_experiments=%d\n",usage.nr_experiments);
	}

	if (copy_to_user(buf,kbuf,dst-kbuf))
		return -EFAULT;

	(*off)=dst-kbuf;
	return dst-kbuf;
}

/* Read callback for /proc/pmc/info */
static ssize_t proc_pmc_info_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
	char* kbuf;
	pmu_props_t* props;
	monitoring_module_counter_usage_t usage;
	int coretype=0;
	int nbytes=0;
	char* dst;
	int i=0;

	if (*off>0)
		return 0;

	if ((kbuf=vmalloc(500))==NULL)
		return -ENOMEM;

	dst=kbuf;

	mm_module_counter_usage(&usage);

	dst+=sprintf(dst,"*** PMU Info ***\n");
	dst+=sprintf(dst,"nr_core_types=%d\n",nr_core_types);

	for (coretype=0; coretype<get_nr_coretypes(); coretype++) {
		props=&pmu_props_cputype[coretype];
		dst+=sprintf(dst,"[PMU coretype%d]\n",coretype);
		dst+=sprintf(dst,"pmu_model=%s\n", props->arch_string);
		dst+=sprintf(dst,"nr_gp_pmcs=%d\n",props->nr_gp_pmcs);
		dst+=sprintf(dst,"nr_ff_pmcs=%d\n", props->nr_fixed_pmcs);
		dst+=sprintf(dst,"pmc_bitwidth=%d\n", props->pmc_width);
	}

	dst+=sprintf(dst,"***************\n");
	dst+=sprintf(dst,"*** Monitoring Module ***\n");
	dst+=sprintf(dst,"counter_used_mask=0x%x\n",usage.hwpmc_mask);
	dst+=sprintf(dst,"nr_experiments=%d\n",usage.nr_experiments);
	dst+=sprintf(dst,"nr_virtual_counters=%d\n",usage.nr_virtual_counters);

	if (usage.nr_virtual_counters) {
		dst+=sprintf(dst,"*** Virtual counters ***\n");
		for (i = 0; i < usage.nr_virtual_counters; i++)
			dst+=sprintf(dst,"virt%d=%s\n",i,usage.vcounter_desc[i]==NULL?"unknown":usage.vcounter_desc[i]);
	}
	dst+=sprintf(dst,"***************\n");


	nbytes=dst-kbuf;

	if (nbytes>len) {
		vfree(kbuf);
		return -ENOSPC;
	}

	if (copy_to_user(buf,kbuf,nbytes)) {
		vfree(kbuf);
		return -EINVAL;
	}

	(*off)+=(nbytes);
	vfree(kbuf);
	return nbytes;
}

/* Descriptors for the various /proc entries */
static struct proc_dir_entry *enablepmcs_entry=NULL;
static struct proc_dir_entry *pmcconfig_entry=NULL;
static struct proc_dir_entry *monitorpmcs_entry=NULL;
static struct proc_dir_entry *properties_entry=NULL;
static struct proc_dir_entry *info_entry=NULL;
struct proc_dir_entry *pmc_dir=NULL;

/* Remove entries created in /proc/pmc */
static int destroy_proc_entries(void)
{
	if (enablepmcs_entry)
		remove_proc_entry("enable", pmc_dir);
	if (monitorpmcs_entry)
		remove_proc_entry("monitor", pmc_dir);
	if (pmcconfig_entry)
		remove_proc_entry("config", pmc_dir);
	if (properties_entry)
		remove_proc_entry("properties", pmc_dir);
	if (info_entry)
		remove_proc_entry("info", pmc_dir);
	return 0;
}

/* Create proc entries in /proc/pmc */
static int create_proc_entries(void)
{
	enablepmcs_entry = proc_create_data( "enable", 0666, pmc_dir, &proc_pmc_enable_fops, NULL);
	if(enablepmcs_entry == NULL) {
		printk(KERN_INFO "Couldn't create 'enable' proc entry\n");
		goto out_error;
	}

	pmcconfig_entry = proc_create_data("config", 0666, pmc_dir, &proc_pmc_config_fops, NULL );
	if((pmcconfig_entry == NULL)) {
		printk(KERN_INFO "Couldn't create 'config' proc entry\n");
		goto out_error;
	}
	monitorpmcs_entry = proc_create_data("monitor", 0666, pmc_dir, &proc_monitor_pmcs_fops, NULL);
	if((monitorpmcs_entry  == NULL)) {
		printk(KERN_INFO "Couldn't create 'monitor' proc entry\n");
		goto out_error;
	}


	properties_entry= proc_create_data("properties", 0666, pmc_dir, &proc_pmc_properties_fops, NULL);
	if((properties_entry  == NULL)) {
		printk(KERN_INFO "Couldn't create 'properties' proc entry\n");
		goto out_error;
	}

	info_entry= proc_create_data("info", 0666, pmc_dir, &proc_pmc_info_fops, NULL);
	if((info_entry  == NULL)) {
		printk(KERN_INFO "Couldn't create 'info' proc entry\n");
		goto out_error;
	}

	return 0;
out_error:
	destroy_proc_entries();
	return -ENOMEM;
}


/*********** Platform-independent code that takes care of PMC overflow *****************/

/* 
 * This function detects which PMC different from the EBS counter actually overflowed.
 * The function returns a non-zero value if the EBS counter is among those which overflowed.
 */
static unsigned int update_overflow_status_non_ebs_pmcs(core_experiment_t* exp,unsigned int overflow_mask)
{
	/* On AMD overflow masks are unreliable
		since no overflow status exists as such
		Assume overlows are always associated with the EBS counter */
#ifdef CONFIG_PMC_AMD
	if (exp->ebs_idx==-1)
		return 0;
	else
		return (0x1<<exp->log_to_phys[exp->ebs_idx]);
#else
	int phys_ebs_index=-1;
	int log_pmc_index=0;
	unsigned int filtered_mask=0;

	int i=0;

	if (exp->ebs_idx==-1) {
		filtered_mask=overflow_mask;
	} else {
		phys_ebs_index=exp->log_to_phys[exp->ebs_idx];
		filtered_mask=overflow_mask & ~(0x1<<phys_ebs_index);
	}
	/* Increment overflow counter for non-EBS events */
	for (i=0; i<MAX_LL_EXPS && filtered_mask; i++) {
		if ((filtered_mask & (0x1 << i)) &&
		    (log_pmc_index=exp->phys_to_log[i])!=-1) {
			trace_printk("Increment overflow counter of log event %d\n",log_pmc_index);
			exp->nr_overflows[log_pmc_index]++;
		}

		filtered_mask&=~(0x1 << i);
	}

	if  (phys_ebs_index!=-1)
		return (overflow_mask & (0x1<<phys_ebs_index));
	else
		return 0;
#endif
}

/*
 * This function gets invoked from the platform-specific PMU code
 * when a PMC overflow interrupt is being handled. The function
 * takes care of reading the performance counters and pushes a PMC sample
 * into the samples buffer when in EBS mode. 
 */
void do_count_on_overflow(struct pt_regs *regs, struct cpu_pmc_buffer* cur, unsigned int overflow_mask)
{

	int read_ok=0;
	pmc_sample_t sample;
	unsigned int ebs_idx=0;
	unsigned int this_cpu=smp_processor_id();
	pmu_props_t* props=get_pmu_props_cpu(this_cpu);
	unsigned int filtered_mask=0;

	/* There is no buffer !! */
	if (cur->pmc_samples_buffer==NULL) {
		// Stop counters to avoid spurious interrupts
		mc_stop_all_counters(cur->exp);
	} else {
		spin_lock(&cur->ptr_lock);
		spin_lock(cur->buf_lock);

		filtered_mask=update_overflow_status_non_ebs_pmcs(cur->exp,overflow_mask);

		if (!filtered_mask)
			goto exit_unlock;

		/* Initialize sample*/
		sample.type=PMC_EBS_SAMPLE;
		sample.coretype=0;
		sample.exp_idx=0;
		sample.pmc_mask=cur->exp->used_pmcs;
		sample.nr_counts=cur->exp->size;
		sample.virt_mask=0;
		sample.nr_virt_counts=0;
		sample.pid=cur->prof->this_tsk->pid;

		/* Read counters !! */
		read_ok=!do_count_mc_experiment_buffer(cur->exp,
		                                       props,
		                                       sample.pmc_counts);

		/* Insert into the thread's buffer (if its not null) */
		if (read_ok && cur->pmc_samples_buffer) {
			ebs_idx=cur->exp->ebs_idx;

			if (ebs_idx!=-1) {
				uint64_t reset_value=__get_reset_value(&(cur->exp->array[ebs_idx]));
				sample.pmc_counts[ebs_idx]+=( (-reset_value) & props->pmc_width_mask);
			}

			/* Call the monitoring module if the user requested virtual counters */
			if (cur->prof && cur->prof->virt_counter_mask)
				mm_on_new_sample(cur->prof,this_cpu,&sample,MM_TICK,NULL);

			__push_sample_cbuffer(cur->pmc_samples_buffer,&sample);
		}

exit_unlock:
		spin_unlock(cur->buf_lock);
		spin_unlock(&cur->ptr_lock);
	}
}


/* Initialization of platform-independent per-CPU structures */
static void init_percpu_structures(void)
{
	int cpu;
	struct cpu_pmc_buffer* cur;
	core_experiment_t* exp;

	for_each_possible_cpu(cpu) {
		exp=&per_cpu(cpu_exp, cpu);
		init_core_experiment_t(exp,0);
		cur=&per_cpu(pmc_buffer, cpu);
		cur->pmc_samples_buffer = NULL;
		cur->prof=NULL;
		cur->exp = exp; /* Point to msr config on this cpu */
		spin_lock_init(&cur->ptr_lock);
		spin_lock_init(&cur->default_buf_lock);
		cur->buf_lock=&cur->default_buf_lock;
	}
}


/* Module initialization function */
static int __init pmctrack_module_init(void)
{
	int ret=0;
	int error_mm_manager=0;
	int error_proc_entries=0;

	if ((ret=init_pmu())!=0) {
		printk("Can't Init PMU");
		return ret;
	}

	init_pmon_config_t();
	init_percpu_structures();

	if((ret = register_pmc_module(&pmc_mc_prog,THIS_MODULE)) != 0) {
		printk("Can't load pmc module");
		return ret;
	}

	/* Create /proc/pmc directory */
	pmc_dir=proc_mkdir("pmc",NULL);
	if (pmc_dir == NULL) {
		printk(KERN_INFO "Couldn't create proc entry pmc\n");
		ret=-ENOMEM;
		goto out_error;
	}

	/* Initialize monitoring-module manager */
	if ((ret=init_mm_manager(pmc_dir))!=0) {
		printk("Can't Initialize MM manager");
		error_mm_manager=ret;
		goto out_error;
	}

	/* Create basic /proc/entries */
	if ( (ret=create_proc_entries())!=0) {
		printk("Can't create proc entries ");
		error_proc_entries=ret;
		goto out_error;
	}

	/* Initialize system-wide mode structures */
	if ( (ret=syswide_monitoring_init())!=0) {
		printk("Can't initialize system-wide monitoring");
		goto out_error;
	}

	printk(KERN_INFO "PMCTrack module loaded\n");
	return 0;
out_error:
	if (error_proc_entries)
		destroy_proc_entries();
	if (error_mm_manager) /* Unload monitoring module manager */
		destroy_mm_manager(pmc_dir);
	if (pmc_dir) {
		pmc_dir=NULL;
		remove_proc_entry("pmc", NULL);
	}
	unregister_pmc_module(&pmc_mc_prog,THIS_MODULE);
	return ret;
}

/* Module cleanup function */
static void __exit pmctrack_module_exit(void)
{
	if(!unregister_pmc_module(&pmc_mc_prog,THIS_MODULE)) {
		/* Restore the APIC and stuff */
		pmu_shutdown();
		/* Unload monitoring module manager */
		destroy_mm_manager(pmc_dir);
		destroy_proc_entries();
		syswide_monitoring_cleanup();
		if (pmc_dir)
			remove_proc_entry("pmc", NULL);
		printk(KERN_INFO "Module PMCs unloaded.\n");
	} else {
		printk(KERN_INFO "Module PMCs not unloaded.\n");
	}
}

module_init(pmctrack_module_init);
module_exit(pmctrack_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PMCTrack kernel module");
MODULE_AUTHOR("Juan Carlos Saez");

