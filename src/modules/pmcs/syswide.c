/*
 *  syswide.c
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#include <pmc/syswide.h>
#include <pmc/mc_experiments.h>
#include <linux/timer.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <pmc/pmu_config.h>
#include <linux/mm.h>  /* mmap related stuff */
#include <asm/uaccess.h>
#include <pmc/monitoring_mod.h>


#define SYSWIDE_MONITORING_DISABLED -3
#define SYSWIDE_MONITORING_STOPPING -2
#define SYSWIDE_MONITORING_STARTING -1


/*
 * Per-CPU structure to hold the necessary
 * information to implement the system-wide
 * monitoring mode
 */
typedef struct {
	core_experiment_set_t pmc_config_set;
	core_experiment_t* cur_config;
	uint_t virt_counter_mask;
	uint64_t pmc_values[MAX_LL_EXPS];
	pmc_sample_t last_sample;
	ktime_t ref_time;
	spinlock_t lock;
} cpu_syswide_t;

/* Global structure for the system-wide monitoring mode */
typedef struct {
	/* PID of the monitor (-1 means syswide disabled) */
	volatile pid_t syswide_monitor;
	/* Buffer shared between monitor and kernel timer.
		It must be big enough to store a sample per-CPU */
	pmc_samples_buffer_t* pmc_samples_buffer;
	/* kernel timer to engage collection of PMC events */
	struct timer_list syswide_timer;
	/* To serialize accesses the various fields.
		Note that pmc_samples_buffer has its own spinlock
	*/
	unsigned long syswide_timer_period; /* Inherit from monitor thread */
	unsigned int pause_syswide_monitor; /* Global pause flag */
	spinlock_t lock;
} syswide_ctl_t;



/* Global data
  Essential initialization to avoid races ...
*/
syswide_ctl_t syswide_ctl= {.syswide_monitor=SYSWIDE_MONITORING_DISABLED};
static DEFINE_PER_CPU(cpu_syswide_t, cpu_syswide);


/*
 * Read performance counters and update statistics
 * on the current CPU
 */
static int __refresh_counts_cpu(cpu_syswide_t* cpudata)
{
	core_experiment_t* core_experiment=cpudata->cur_config;
	int i=0;
	uint64_t last_value;

	/* Nothing to do if no counters have been configured */
	if (core_experiment) {
		for(i=0; i<core_experiment->size; i++) {
			low_level_exp* lle = &core_experiment->array[i];
			/* Gather PMC values (stop,read and reset) */
			__stop_count(lle);
			__read_count(lle);
			__restart_count(lle);

			/* get Last value */
			last_value = __get_last_value(lle);
			cpudata->pmc_values[i]+=last_value;
		}

		reset_overflow_status();
	}

	if (cpudata->virt_counter_mask)
		mm_on_syswide_refresh_monitor(smp_processor_id(),cpudata->virt_counter_mask);

	return !core_experiment;
}


/*
 * Read performance counters and update statistics
 * on the current CPU (SMP-safe version)
 */
static int refresh_counts_cpu(cpu_syswide_t* cpudata)
{
	unsigned long flags;
	int retval=0;

	/* Grab the spinlock to avoid races when updating "pmc_values" */
	spin_lock_irqsave(&cpudata->lock,flags);
	retval=__refresh_counts_cpu(cpudata);
	spin_unlock_irqrestore(&cpudata->lock,flags);
	return retval;
}


/*
 * Gather PMC and virtual-counter samples
 * on the current CPU
 */
static void syswide_monitoring_sample_cpu(void* dummy)
{
	int cpu=smp_processor_id();
	cpu_syswide_t* cur=&per_cpu(cpu_syswide, cpu);
	core_experiment_t* core_exp=cur->cur_config;
	core_experiment_t* next=NULL;
	int cur_coretype=get_coretype_cpu(cpu);
	pmc_sample_t* sample=&cur->last_sample;
	unsigned long flags=0;
	int i=0;
	ktime_t now;

	/* Grab the spinlock to avoid races when updating "pmc_values" */
	spin_lock_irqsave(&cur->lock,flags);

	__refresh_counts_cpu(cur);

	if (core_exp) {
		/* Copy and clear samples in prof */
		for(i=0; i<MAX_LL_EXPS; i++) {
			cur->last_sample.pmc_counts[i]=cur->pmc_values[i];
			cur->pmc_values[i]=0;
		}
	}

	now=ktime_get();

	/* Generate sample ... */
	sample->coretype=cur_coretype;
	sample->exp_idx=core_exp?core_exp->exp_idx:0;
	sample->pmc_mask=core_exp?core_exp->used_pmcs:0;
	sample->nr_counts=core_exp?core_exp->size:0;
	sample->virt_mask=0;
	sample->nr_virt_counts=0;
	sample->pid=cpu; /* In syswide mode -> this field is reused to store the CPU */
	sample->elapsed_time=raw_ktime(ktime_sub(now,cur->ref_time));
	cur->ref_time=now;

	/* Call the estimation module if the user requested virtual counters */
	if (cur->virt_counter_mask)
		mm_on_syswide_dump_virtual_counters(cpu,cur->virt_counter_mask,sample);

	/* Engage multiplexation */
	next=get_next_experiment_in_set(&cur->pmc_config_set);

	if (next && cur->cur_config!=next) {
		/* Clear all counters in the platform */
		mc_clear_all_platform_counters(get_pmu_props_coretype(cur_coretype));
		/* Update experiment set */
		cur->cur_config=next;

		/* Reconfigure counters as if it were the first time*/
		mc_restart_all_counters(cur->cur_config);
	}


	spin_unlock_irqrestore(&cur->lock,flags);
}

/* Main timer function for the syswide-monitoring mode */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,15,0)
static void fire_syswide_timer(unsigned long data)
#else
static void fire_syswide_timer(struct timer_list *t)
#endif
{
	cpu_syswide_t* cur=NULL;
	unsigned long flags;
	int cpu=0;

	if (!syswide_monitoring_enabled())
		return;

	/* Generate per-cpu samples in a distributed way */
	on_each_cpu(syswide_monitoring_sample_cpu, NULL, 1);

	/* Grab the global lock */
	spin_lock_irqsave(&syswide_ctl.lock,flags);

	if (!syswide_ctl.pause_syswide_monitor && syswide_monitoring_enabled() &&  syswide_ctl.pmc_samples_buffer) {
		spin_lock(&syswide_ctl.pmc_samples_buffer->lock);

		/* Dump the various samples */
		for_each_online_cpu(cpu) {
			cur=&per_cpu(cpu_syswide, cpu);
			__push_sample_cbuffer_nowakeup(syswide_ctl.pmc_samples_buffer,&cur->last_sample);
		}
		/* Wake up monitor ... */
		__wake_up_monitor_program(syswide_ctl.pmc_samples_buffer);
		spin_unlock(&syswide_ctl.pmc_samples_buffer->lock);
	}

	if (syswide_monitoring_enabled())
		mod_timer( &syswide_ctl.syswide_timer, jiffies + syswide_ctl.syswide_timer_period);

	spin_unlock_irqrestore(&syswide_ctl.lock,flags);
}


/* Free up the structure that stores PMC configurations for a CPU */
static inline void free_cpu_syswide_data(cpu_syswide_t* data)
{
	free_experiment_set(&data->pmc_config_set);
}

/* Reset per-CPU structure for system-wide monitoring */
static inline void reset_cpu_syswide_data(cpu_syswide_t* data, int init)
{
	int i=0;

	/* Empty experiment set ... */
	if (init)
		init_core_experiment_set_t(&data->pmc_config_set);
	else
		free_experiment_set(&data->pmc_config_set);

	if (init)
		spin_lock_init(&data->lock);

	for(i=0; i<MAX_LL_EXPS; i++)
		data->pmc_values[i]=0;

	/* Set NULL (no perf counters) */
	data->cur_config=NULL;

	data->virt_counter_mask=0;	// No virtual counters selected so far

	/* Clear sample */
	memset(&data->last_sample,0,sizeof(pmc_sample_t));
}

/* Setup PMC and virtual-counter configuration for a CPU */
static int setup_cpu_syswide_data(cpu_syswide_t* data,
                                  core_experiment_set_t* pmc_config_set,
                                  uint_t virt_counter_mask)
{
	reset_cpu_syswide_data(data,0);

	if (clone_core_experiment_set_t(&data->pmc_config_set,pmc_config_set))
		return -ENOMEM;

	/* Set up cur experiment */
	data->cur_config=get_cur_experiment_in_set(&data->pmc_config_set);

	/* Inherit virtual counter mask as well */
	data->virt_counter_mask=virt_counter_mask;

	return 0;
}


/*
 * Global initialization function for system-wide mode
 * (invoked when the module is loaded in the kernel)
 */
int syswide_monitoring_init(void)
{

	int cpu;
	cpu_syswide_t* cur;

	syswide_ctl.syswide_monitor=SYSWIDE_MONITORING_DISABLED;
	syswide_ctl.pmc_samples_buffer=NULL;
	spin_lock_init(&syswide_ctl.lock);

	/* Initialize timer fields but do not activate it yet */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,15,0)
	init_timer(&syswide_ctl.syswide_timer);
	syswide_ctl.syswide_timer.expires=0; /* Any default value will do here */
	syswide_ctl.syswide_timer.data=0;
	syswide_ctl.syswide_timer.function=fire_syswide_timer;
#else
	timer_setup(&syswide_ctl.syswide_timer, fire_syswide_timer, 0);
#endif
	syswide_ctl.syswide_timer_period=HZ;
	syswide_ctl.pause_syswide_monitor=0; /* Enabled by default */

	for_each_possible_cpu(cpu) {
		cur=&per_cpu(cpu_syswide, cpu);
		reset_cpu_syswide_data(cur,1);
	}

	return 0;
}

/*
 * Global cleanup function for system-wide mode
 * (invoked when the module is removed from the kernel)
 */
void syswide_monitoring_cleanup(void)
{
	int cpu;
	cpu_syswide_t* cur;

	for_each_possible_cpu(cpu) {
		cur=&per_cpu(cpu_syswide, cpu);
		free_cpu_syswide_data(cur);
	}
}

/* Return nozero if syswide_monitoring was actually disabled */
int syswide_monitoring_enabled(void)
{
	return syswide_ctl.syswide_monitor>SYSWIDE_MONITORING_STARTING;
}

/*
 *  This function gets invoked on every context switch
 *  in when syswide monitoring is enabled.
 */
int syswide_monitoring_switch_in(int cpu)
{
	/* Do nothing on switch in for now */
	return 0;
}

/*
 *  This function gets invoked on every context switch
 *  out when syswide monitoring is enabled.
 *
 *	Return non-zero if syswide_monitoring is not using PMCs
 *	- This enables to use virtual counters in system-wide mode
 *	  while using PMCs and other virtual counters
 *	  in any of the available per-thread modes
 */
int syswide_monitoring_switch_out(int cpu)
{
	cpu_syswide_t* cur=&per_cpu(cpu_syswide, cpu);

	if (!syswide_monitoring_enabled())
		return 1;

	return refresh_counts_cpu(cur);
}

/* Returns true if "p" is current syswide monitor process */
int is_syswide_monitor(struct task_struct* p)
{
	return syswide_ctl.syswide_monitor==p->pid;
}

/* Start syswide monitoring on this cpu */
static void syswide_monitoring_start_cpu(void* dummy)
{

	int cpu=smp_processor_id();
	cpu_syswide_t* cur=&per_cpu(cpu_syswide, cpu);

	/* Reprogram all PMCs safely from here */
	if (cur->cur_config) {
		mc_clear_all_counters(cur->cur_config);
		mc_restart_all_counters(cur->cur_config);
	}

	cur->ref_time=ktime_get();

	/* Tell the monitoring module to start syswide monitoring */
	if (cur->virt_counter_mask)
		mm_on_syswide_start_monitor(cpu, cur->virt_counter_mask);
}

/* Stop syswide monitoring on this cpu */
static void syswide_monitoring_stop_cpu(void* dummy)
{

	int cpu=smp_processor_id();
	cpu_syswide_t* cur=&per_cpu(cpu_syswide, smp_processor_id());

	/* Clear PMCs */
	if (cur->cur_config)
		mc_clear_all_counters(cur->cur_config);

	/* Tell the monitoring module to stop */
	if (cur->virt_counter_mask)
		mm_on_syswide_stop_monitor(cpu, cur->virt_counter_mask);
}

/* Start syswide_monitoring */
int syswide_monitoring_start(void)
{
	int retval=0;
	unsigned long flags=0;
	int coretype=0;
	int cpu;
	struct task_struct* p=current;
	pmon_prof_t* prof=(pmon_prof_t*)p->pmc;
	cpu_syswide_t* cur;
	core_experiment_t* experiment=NULL;

	if (!prof)
		return -EPERM;

	spin_lock_irqsave(&syswide_ctl.lock,flags);

	/* Make sure system wide is not already in use */
	if (syswide_ctl.syswide_monitor!=SYSWIDE_MONITORING_DISABLED) {
		retval=-EBUSY;
		printk(KERN_INFO "Attempting to enable system-wide mode while active\n");
		goto exit_unlock;
	}

	/* Inherit fields from monitor process */
	syswide_ctl.syswide_timer_period=prof->pmc_jiffies_interval;
	syswide_ctl.syswide_monitor=SYSWIDE_MONITORING_STARTING;
	smp_mb();

	/* Check for the availability of system-wide virtual counters
	   on the current CPU */
	if (prof->virt_counter_mask &&
	    (retval=mm_on_syswide_start_monitor(-1, prof->virt_counter_mask))) {
		printk(KERN_INFO "Virtual counters not available in system-wide mode\n");
		goto exit_unlock;
	}
	/* Propagate values on each CPU ... */
	for_each_possible_cpu(cpu) {

		coretype=get_coretype_cpu(cpu);
		cur=&per_cpu(cpu_syswide, cpu);

		/* Make sure there is a configuration for such a core type */
		if (!&prof->pmcs_multiplex_cfg[coretype]) {
			printk(KERN_INFO "No experiments were defined for this core type\n");
			retval=-ENOENT;
			goto exit_unlock;
		}

		experiment=get_cur_experiment_in_set(&prof->pmcs_multiplex_cfg[coretype]);
		/* Make sure ebs is not enabled */
		if (experiment && experiment->ebs_idx!=-1) {
			retval=-EINVAL;
			printk(KERN_INFO "EBS can't be used in system-wide mode\n");
			goto exit_unlock;
		}

		if ((retval=setup_cpu_syswide_data(cur,
		                                   &prof->pmcs_multiplex_cfg[coretype],
		                                   prof->virt_counter_mask))!=0) {
			printk(KERN_INFO "Can't setup per-CPU syswide data\n");
			goto exit_unlock;
		}
	}

	/* Share buffer ... */
	syswide_ctl.pmc_samples_buffer=prof->pmc_samples_buffer;
	/* Increase ref count */
	get_pmc_samples_buffer(syswide_ctl.pmc_samples_buffer);

	spin_unlock_irqrestore(&syswide_ctl.lock,flags);

	/* Initialize counters on each CPU with interrupts enabled */
	on_each_cpu(syswide_monitoring_start_cpu, NULL, 1);

	/* Enable system-wide monitoring and start up timer */
	spin_lock_irqsave(&syswide_ctl.lock,flags);
	syswide_ctl.syswide_monitor=p->pid;
	syswide_ctl.syswide_timer.expires=jiffies+syswide_ctl.syswide_timer_period;
	syswide_ctl.pause_syswide_monitor=0; /* Enabled by default */
	add_timer(&syswide_ctl.syswide_timer);
	spin_unlock_irqrestore(&syswide_ctl.lock,flags);

	return 0;
exit_unlock:
	spin_unlock_irqrestore(&syswide_ctl.lock,flags);
	return retval; /* TODO */
}

/* Stop syswide_monitoring */
int syswide_monitoring_stop(void)
{
	int retval=0;
	unsigned long flags=0;
	struct task_struct* p=current;

	spin_lock_irqsave(&syswide_ctl.lock,flags);

	/* Make sure the monitor is the only one invoking this function */
	if (syswide_ctl.syswide_monitor!=p->pid) {
		retval=-EPERM;
		goto exit_unlock_stop;
	}

	/* Clean up the various fields */
	syswide_ctl.syswide_timer_period=HZ;
	syswide_ctl.syswide_monitor=SYSWIDE_MONITORING_STOPPING;
	spin_unlock_irqrestore(&syswide_ctl.lock,flags);

	/* Cancel timer (Blocking function)*/
	del_timer_sync(&syswide_ctl.syswide_timer);
	/* Stop counters across CPUs */
	on_each_cpu(syswide_monitoring_stop_cpu, NULL, 1);

	/* Update global status */
	spin_lock_irqsave(&syswide_ctl.lock,flags);
	syswide_ctl.syswide_monitor=SYSWIDE_MONITORING_DISABLED;
	/* Now the timer is not around it's safe to release the buffer */
	/* Decrease ref count for the shared buffer and forget it ever existed */
	put_pmc_samples_buffer(syswide_ctl.pmc_samples_buffer);
	syswide_ctl.pmc_samples_buffer=NULL;
	spin_unlock_irqrestore(&syswide_ctl.lock,flags);

	return 0;
exit_unlock_stop:
	spin_lock_irqsave(&syswide_ctl.lock,flags);
	return retval;
}


/* Pause syswide_monitoring */
int syswide_monitoring_pause(void)
{
	unsigned long flags=0;
	int ret=0;

	spin_lock_irqsave(&syswide_ctl.lock,flags);
	if (!syswide_monitoring_enabled())
		ret=-EINVAL;
	else
		syswide_ctl.pause_syswide_monitor=1;
	spin_unlock_irqrestore(&syswide_ctl.lock,flags);
	return ret;
}

/* Resume syswide_monitoring */
int syswide_monitoring_resume(void)
{
	unsigned long flags=0;
	int ret=0;

	spin_lock_irqsave(&syswide_ctl.lock,flags);
	if (!syswide_monitoring_enabled())
		ret=-EINVAL;
	else
		syswide_ctl.pause_syswide_monitor=0;
	spin_unlock_irqrestore(&syswide_ctl.lock,flags);
	return ret;
}
