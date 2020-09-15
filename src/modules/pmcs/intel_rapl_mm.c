/*
 *  intel_rapl_mm.c
 *
 * 	PMCTrack monitoring module enabling to measure energy consumption
 *  using Intel RAPL
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#include <pmc/hl_events.h>
#include <pmc/mc_experiments.h>
#include <pmc/monitoring_mod.h>
#include <pmc/intel_rapl.h>

#define INTEL_RAPL_MODULE_STR "PMCtrack module that supports Intel RAPL"


static intel_rapl_support_t rapl_support;

struct {
	unsigned char reset_on_cswitch;
} intel_rapl_config;


/* Per-thread private data for this monitoring module */
typedef struct {
	intel_rapl_control_t rapl_ctrl;
	int security_id;
	int first_time;
} intel_rapl_thread_data_t;

/* Initialize resources to support system-wide monitoring with Intel RAPL */
static int initialize_system_wide_rapl_structures(void);

/* Return the capabilities/properties of this monitoring module */
static void intel_rapl_module_counter_usage(monitoring_module_counter_usage_t* usage)
{
	int i;
	usage->hwpmc_mask=0;
	usage->nr_virtual_counters=rapl_support.nr_available_power_domains; // Three domains energy usage
	usage->nr_experiments=0;
	for (i=0; i<usage->nr_virtual_counters; i++)
		usage->vcounter_desc[i]=rapl_support.available_vcounters[i];
}


/* RAPL MM initialization */
static int intel_rapl_enable_module(void)
{
	int ret=0;

	if  ((ret=intel_rapl_initialize(&rapl_support, 0)))
		return ret;

	if ((ret=initialize_system_wide_rapl_structures())) {
		intel_rapl_release(&rapl_support);
		printk(KERN_INFO "Couldn't initialize system-wide RAPL structures");
		return ret;
	}

	intel_rapl_config.reset_on_cswitch=0;

	return 0;
}

/* RAPL MM cleanup function */
static void intel_rapl_disable_module(void)
{
	intel_rapl_release(&rapl_support);
	printk(KERN_ALERT "%s monitoring module unloaded!!\n",INTEL_RAPL_MODULE_STR);
}


/* Display RAPL properties of the current processor */
static int intel_rapl_on_read_config(char* str, unsigned int len)
{
	char* dst=str;

	dst+=intel_rapl_print_energy_units(dst,&rapl_support);

	return dst - str;
}

/* This MM does not export configuration parameters */
static int intel_rapl_on_write_config(const char *str, unsigned int len)
{
	return 0;
}

/* on fork() callback */
static int intel_rapl_on_fork(unsigned long clone_flags, pmon_prof_t* prof)
{
	intel_rapl_thread_data_t*  data= NULL;

	if (prof->monitoring_mod_priv_data!=NULL)
		return 0;


	data= kmalloc(sizeof (intel_rapl_thread_data_t), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	intel_rapl_control_init(&data->rapl_ctrl);

	data->security_id=current_monitoring_module_security_id();

	if (!intel_rapl_config.reset_on_cswitch)
		intel_rapl_update_energy_values_thread(&rapl_support,&data->rapl_ctrl,0);

	data->first_time=1;
	prof->monitoring_mod_priv_data = data;
	return 0;
}

/* on exec() callback */
static void intel_rapl_on_exec(pmon_prof_t* prof)
{
#ifdef DEBUG
	printk(KERN_INFO "A thread executed exec() !!!\n" );
#endif
}


/*
 * Update cumulative energy counters in the thread structure and
 * set the associated virtual counts in the PMC sample structure
 */
static int intel_rapl_on_new_sample(pmon_prof_t* prof, int cpu, pmc_sample_t* sample,
                                    int flags,void* data)
{
	intel_rapl_thread_data_t* tdata=prof->monitoring_mod_priv_data;
	intel_rapl_sample_t rapl_sample;
	int i=0;
	int cnt_virt=0;
	int active_domains=0;

	if (!tdata)
		return 0;

	if (!intel_rapl_config.reset_on_cswitch || !(flags & MM_NO_CUR_CPU))
		intel_rapl_update_energy_values_thread(&rapl_support,&tdata->rapl_ctrl,1);

	intel_rapl_get_energy_sample_thread(&rapl_support,&tdata->rapl_ctrl,&rapl_sample);

	/* Embed virtual counter information so that the user can see what's going on */
	for (i=0; i<RAPL_NR_DOMAINS; i++) {
		if (rapl_support.available_power_domains_mask & (1<<i)) {
			if ((prof->virt_counter_mask & (1<<active_domains)) ) { // Just one virtual counter
				sample->virt_mask|=(1<<active_domains);
				sample->nr_virt_counts++;
				sample->virtual_counts[cnt_virt]=rapl_sample.energy_value[i];
				cnt_virt++;
			}
			active_domains++;
		}
	}

	return 0;
}

/* Free up private data */
static void intel_rapl_on_free_task(pmon_prof_t* prof)
{
	if (prof->monitoring_mod_priv_data)
		kfree(prof->monitoring_mod_priv_data);
}

/* on switch_in callback */
void intel_rapl_on_switch_in(pmon_prof_t* prof)
{
	intel_rapl_thread_data_t* data=(intel_rapl_thread_data_t*)prof->monitoring_mod_priv_data;

	if (!data || data->security_id!=current_monitoring_module_security_id() )
		return;

	/* Update prev counts */
	if (intel_rapl_config.reset_on_cswitch)
		intel_rapl_update_energy_values_thread(&rapl_support,&data->rapl_ctrl,0);

	if (data->first_time)
		data->first_time=0;
}

/* on switch_out callback */
void intel_rapl_on_switch_out(pmon_prof_t* prof)
{
	intel_rapl_thread_data_t* data=(intel_rapl_thread_data_t*)prof->monitoring_mod_priv_data;

	if (!data || data->security_id!=current_monitoring_module_security_id())
		return;

	/* Accumulate energy readings */
	if (!data->first_time && intel_rapl_config.reset_on_cswitch)
		intel_rapl_update_energy_values_thread(&rapl_support,&data->rapl_ctrl,1);
}

/* Modify this function if necessary to expose energy readings to the OS scheduler */
static int intel_rapl_get_current_metric_value(pmon_prof_t* prof, int key, uint64_t* value)
{
	return -1;
}

/* Support for system-wide power measurement (Reuse per-thread info as is) */
static DEFINE_PER_CPU(intel_rapl_control_t, cpu_syswide);

/* Initialize resources to support system-wide monitoring with Intel RAPL */
static int initialize_system_wide_rapl_structures(void)
{
	int cpu;
	intel_rapl_control_t* data;

	for_each_possible_cpu(cpu) {
		data=&per_cpu(cpu_syswide, cpu);
		intel_rapl_control_init(data);
	}

	return 0;
}

/*	Invoked on each CPU when starting up system-wide monitoring mode */
static int intel_rapl_on_syswide_start_monitor(int cpu, unsigned int virtual_mask)
{
	intel_rapl_control_t* data;


	/* Probe only */
	if (cpu==-1) {
		/* Make sure virtual_mask only has 1s in the right bits
			- This can be checked easily
				and( virtual_mask,not(2^nr_available_vcounts - 1)) == 0
		*/
		if (virtual_mask & ~((1<<rapl_support.nr_available_power_domains)-1))
			return -EINVAL;
		else
			return 0;
	}

	data=&per_cpu(cpu_syswide, cpu);
	intel_rapl_control_init(data);
	/* Update prev counts */

	intel_rapl_update_energy_values_thread(&rapl_support, data, 0);

	return 0;
}

/*	Invoked on each CPU when stopping system-wide monitoring mode */
static void intel_rapl_on_syswide_refresh_monitor(int cpu, unsigned int virtual_mask)
{
	intel_rapl_control_t* data=&per_cpu(cpu_syswide, cpu);

	/* Accumulate energy readings */
	intel_rapl_update_energy_values_thread(&rapl_support, data, 1);
}

/* 	Dump virtual-counter values for this CPU */
static void intel_rapl_on_syswide_dump_virtual_counters(int cpu, unsigned int virtual_mask,pmc_sample_t* sample)
{
	intel_rapl_control_t* data=&per_cpu(cpu_syswide, cpu);
	intel_rapl_sample_t rapl_sample;
	int i=0;
	int cnt_virt=0;
	int active_domains=0;

	if (!virtual_mask)
		return;

	intel_rapl_get_energy_sample_thread(&rapl_support,data,&rapl_sample);

	/* Embed virtual counter information so that the user can see what's going on */
	for (i=0; i<RAPL_NR_DOMAINS; i++) {
		if (rapl_support.available_power_domains_mask & (1<<i)) {
			if ((virtual_mask & (1<<active_domains)) ) { // Just one virtual counter
				sample->virt_mask|=(1<<active_domains);
				sample->nr_virt_counts++;
				sample->virtual_counts[cnt_virt]=rapl_sample.energy_value[i];
				cnt_virt++;
			}
			active_domains++;
		}
	}

}

/* Implementation of the monitoring_module_t interface */
monitoring_module_t intel_rapl_mm= {
	.info=INTEL_RAPL_MODULE_STR,
	.id=-1,
	.probe_module=intel_rapl_probe,
	.enable_module=intel_rapl_enable_module,
	.disable_module=intel_rapl_disable_module,
	.on_read_config=intel_rapl_on_read_config,
	.on_write_config=intel_rapl_on_write_config,
	.on_fork=intel_rapl_on_fork,
	.on_exec=intel_rapl_on_exec,
	.on_new_sample=intel_rapl_on_new_sample,
	.on_free_task=intel_rapl_on_free_task,
	.on_switch_in=intel_rapl_on_switch_in,
	.on_switch_out=intel_rapl_on_switch_out,
	.get_current_metric_value=intel_rapl_get_current_metric_value,
	.module_counter_usage=intel_rapl_module_counter_usage,
	.on_syswide_start_monitor=intel_rapl_on_syswide_start_monitor,
	.on_syswide_refresh_monitor=intel_rapl_on_syswide_refresh_monitor,
	.on_syswide_dump_virtual_counters=intel_rapl_on_syswide_dump_virtual_counters
};
