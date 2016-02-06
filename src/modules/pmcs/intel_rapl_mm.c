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
#include <pmc/pmu_config.h>  /* For cpuid_regs_t */
#include <asm/processor.h>

#include <linux/random.h>
#include <linux/spinlock.h>
#define INTEL_RAPL_MODULE_STR "PMCtrack module that supports Intel RAPL"


/* MSR address for the various RAPL registers */
#define MSR_RAPL_POWER_UNIT_PMCTRACK		0x606
#define MSR_PKG_ENERGY_STATUS_PMCTRACK		0x611
#define MSR_PP0_ENERGY_STATUS_PMCTRACK		0x639
#define MSR_PP1_ENERGY_STATUS_PMCTRACK		0x641
#define MSR_DRAM_ENERGY_STATUS_PMCTRACK		0x619

#define MSR_ENERY_STATUS_BITMASK_PMCTRACK 0xffffffff

/* Possible RAPL domains */
enum rapl_domains {RAPL_PP0_DOMAIN=0,RAPL_PP1_DOMAIN,RAPL_PKG_DOMAIN,RAPL_DRAM_DOMAIN, RAPL_NR_DOMAINS};

uint_t rapl_msr_regs_domains[RAPL_NR_DOMAINS]= {
	MSR_PP0_ENERGY_STATUS_PMCTRACK,
	MSR_PP1_ENERGY_STATUS_PMCTRACK,
	MSR_PKG_ENERGY_STATUS_PMCTRACK,
	MSR_DRAM_ENERGY_STATUS_PMCTRACK
};

/* Model ID for the Intel Haswell-EP processors */
#define HASWELL_EP_MODEL 63

/* Table of processor models that support Intel RAPL */
static struct pmctrack_cpu_model supported_models[]= {
	{42,"sandybridge"},{45,"sandybridge_ep"},
	{58,"ivybridge"},{62,"ivybridge-ep"},
	{60,"haswell"},{63,"haswell-ep"},
	{86,"broadwell"},
	{0,NULL} /* Marker */
};

/*
 * Obtain model information from the current processor
 * and figure out if it supports RAPL or not
 */
static int processor_is_supported(int* processor_model)
{
	int i=0;
	cpuid_regs_t rv;
	unsigned int this_model=0;

	if (boot_cpu_data.x86_vendor!=X86_VENDOR_INTEL)
		return 0;

	/* Figure out CPU */
	rv.eax=0x1;
	rv.ebx=rv.edx=rv.ecx=0x0;
	run_cpuid(rv);
	this_model= (rv.eax & 0xF0) >> 4;
	this_model += ((rv.eax >> 16) & 0xf) << 4;

	while(supported_models[i].model!=0 && supported_models[i].model!=this_model)
		i++;

	/* Not found in table */
	if (supported_models[i].model==0)
		return 0;

	(*processor_model)=this_model;
	return 1;
}

/*
 * Currently, we have just tested this monitoring modules
 * in supported Intel server processors. If your desktop
 * processor supports Intel RAPL, comment the line below
 * and rebuild/reload the module.
 */
#define SERVER_PROCESSOR

/* Number of available power domains in most RAPL-enabled Intel processors */
unsigned int nr_available_power_domains=3;

#ifdef SERVER_PROCESSOR
/* Configuration for server processors */
unsigned char available_power_domains_mask=(1 << RAPL_PP0_DOMAIN) | (1 << RAPL_PKG_DOMAIN) | (1 << RAPL_DRAM_DOMAIN);
static char* available_vcounters[]= {"energy_core","energy_pkg","energy_dram"};
#else
/* Configuration for desktop processors */
unsigned char available_power_domains_mask=(1 << RAPL_PP0_DOMAIN) | (1 << RAPL_PKG_DOMAIN) | (1 << RAPL_PP1_DOMAIN);
static char* available_vcounters[]= {"energy_core","energy_pkg","energy_pp1"};
#endif

#define CLOSE_CONTEXT_SWITCH 0x1

/* Per-thread private data for this monitoring module */
typedef struct {
	uint_t cur_domain_value[RAPL_NR_DOMAINS];
	uint64_t acum_power_domain[RAPL_NR_DOMAINS];
	int security_id;
	int first_time;
} intel_rapl_thread_data_t;

unsigned int power_units,energy_units,time_units;

/* Initialize resources to support system-wide monitoring with Intel RAPL */
static int initialize_system_wide_rapl_structures(void);

/* Return the capabilities/properties of this monitoring module */
static void intel_rapl_module_counter_usage(monitoring_module_counter_usage_t* usage)
{
	int i;
	usage->hwpmc_mask=0;
	usage->nr_virtual_counters=nr_available_power_domains; // Three domains energy usage
	usage->nr_experiments=0;
	for (i=0; i<usage->nr_virtual_counters; i++)
		usage->vcounter_desc[i]=available_vcounters[i];
}

/* probe callback for this monitoring module */
int intel_rapl_probe(void)
{
	int model;

	if (!processor_is_supported(&model))
		return -ENOTSUPP;

	/* In the Haswell-EP processor, energy_core is not implemented */
	if (model==HASWELL_EP_MODEL) {
		nr_available_power_domains=2;
		available_power_domains_mask=(1 << RAPL_PKG_DOMAIN) | (1 << RAPL_DRAM_DOMAIN);
		available_vcounters[0]="energy_pkg";
		available_vcounters[1]="energy_dram";
		available_vcounters[2]=NULL;
	}

	return 0;
}

/* RAPL MM initialization */
static int intel_rapl_enable_module(void)
{
	int retval=0;
	uint64_t result;

	rdmsrl(MSR_RAPL_POWER_UNIT_PMCTRACK,result);

	power_units=result&0xf;
	energy_units=((result>>8)&0x1f);
	time_units=((result>>16)&0xf);

	if ((retval=initialize_system_wide_rapl_structures())) {
		printk(KERN_INFO "Couldn't initialize system-wide RAPL structures");
		return retval;
	}

	return 0;
}

/* RAPL MM cleanup function */
static void intel_rapl_disable_module(void)
{
	printk(KERN_ALERT "%s monitoring module unloaded!!\n",INTEL_RAPL_MODULE_STR);
}


/* Display RAPL properties of the current processor */
static int intel_rapl_on_read_config(char* str, unsigned int len)
{
	char* dst=str;

	dst+=sprintf(dst,"Power units = %d\n",power_units);
	dst+=sprintf(dst,"Energy units = %d\n",energy_units);
	dst+=sprintf(dst,"Time units = %d\n",time_units);

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
	int i=0;
	intel_rapl_thread_data_t*  data= NULL;

	if (prof->monitoring_mod_priv_data!=NULL)
		return 0;


	data= kmalloc(sizeof (intel_rapl_thread_data_t), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	for (i=0; i<RAPL_NR_DOMAINS; i++) {
		data->cur_domain_value[i]=0;
		data->acum_power_domain[i]=0;
	}

	data->security_id=current_monitoring_module_security_id();

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
 * Read RAPL energy registers and update cumulative counters in the
 * private thread structure.
 */
static inline void do_read_energy_msrs(intel_rapl_thread_data_t* tdata, int acum, int remote_core)
{
	int i=0;
	uint_t old_value,foo, delta;

	if (!acum) {
		/* Read energy count from msrs */
		for (i=0; i<RAPL_NR_DOMAINS; i++) {
			if (remote_core && i==RAPL_PP0_DOMAIN)
				continue;
			if (available_power_domains_mask & (1<<i)) {
				rdmsr(rapl_msr_regs_domains[i],tdata->cur_domain_value[i],foo);
			}
		}
	} else {
		for (i=0; i<RAPL_NR_DOMAINS; i++) {
			if (remote_core && i==RAPL_PP0_DOMAIN)
				continue;
			if (available_power_domains_mask & (1<<i)) {
				/* Save old value */
				old_value=tdata->cur_domain_value[i];

				/* Read new value */
				rdmsr(rapl_msr_regs_domains[i],tdata->cur_domain_value[i],foo);

#ifdef DEBUG
				if (i==0)
					printk(KERN_ALERT "0x%x \n",tdata->cur_domain_value[i]);
#endif
				/* Check Overflow */
				if (tdata->cur_domain_value[i] < old_value) {
					printk(KERN_ALERT "Overflow detected ...\n");
					delta=MSR_ENERY_STATUS_BITMASK_PMCTRACK-old_value;
					delta+=tdata->cur_domain_value[i]+1;
				} else
					delta=tdata->cur_domain_value[i]-old_value;

				tdata->acum_power_domain[i]+=delta;
			}
		}
	}
}

/*
 * Update cumulative energy counters in the thread structure and
 * set the associated virtual counts in the PMC sample structure
 */
static int intel_rapl_on_new_sample(pmon_prof_t* prof,int cpu,pmc_sample_t* sample,int flags,void* data)
{
	intel_rapl_thread_data_t* tdata=prof->monitoring_mod_priv_data;
	int i=0;
	int cnt_virt=0;
	int active_domains=0;

	if (tdata!=NULL && prof->virt_counter_mask) {

		do_read_energy_msrs(tdata,1,flags & MM_NO_CUR_CPU);

		/* Embed virtual counter information so that the user can see what's going on */

		for (i=0; i<RAPL_NR_DOMAINS; i++) {
			if (available_power_domains_mask & (1<<i)) {
				if ((prof->virt_counter_mask & (1<<active_domains)) ) { // Just one virtual counter
					sample->virt_mask|=(1<<active_domains);
					sample->nr_virt_counts++;
					sample->virtual_counts[cnt_virt]=(tdata->acum_power_domain[i]*1000000)>>energy_units;
					cnt_virt++;
				}
				active_domains++;
				/* Reset no matter what */
				tdata->acum_power_domain[i]=0;
			}
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
	do_read_energy_msrs(data,0,0);

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
	if (!data->first_time)
		do_read_energy_msrs(data,1,0);
}

/* Modify this function if necessary to expose energy readings to the OS scheduler */
static int intel_rapl_get_current_metric_value(pmon_prof_t* prof, int key, uint64_t* value)
{
	return -1;
}

/* Support for system-wide power measurement (Reuse per-thread info as is) */
static DEFINE_PER_CPU(intel_rapl_thread_data_t, cpu_syswide);

/* Initialize resources to support system-wide monitoring with Intel RAPL */
static int initialize_system_wide_rapl_structures(void)
{
	int cpu,i;
	intel_rapl_thread_data_t* data;

	for_each_possible_cpu(cpu) {
		data=&per_cpu(cpu_syswide, cpu);

		for (i=0; i<RAPL_NR_DOMAINS; i++) {
			data->cur_domain_value[i]=0;
			data->acum_power_domain[i]=0;
		}

		/* These two fields are not used by the system-wide monitor
			Nevertheless we initialize them both just in case...
		*/
		data->security_id=current_monitoring_module_security_id();
		data->first_time=1;
	}

	return 0;
}

/*	Invoked on each CPU when starting up system-wide monitoring mode */
static int intel_rapl_on_syswide_start_monitor(int cpu, unsigned int virtual_mask)
{
	intel_rapl_thread_data_t* data;
	int i=0;

	/* Probe only */
	if (cpu==-1) {
		/* Make sure virtual_mask only has 1s in the right bits
			- This can be checked easily
				and( virtual_mask,not(2^nr_available_vcounts - 1)) == 0
		*/
		if (virtual_mask & ~((1<<nr_available_power_domains)-1))
			return -EINVAL;
		else
			return 0;
	}

	data=&per_cpu(cpu_syswide, cpu);

	for (i=0; i<RAPL_NR_DOMAINS; i++) {
		data->cur_domain_value[i]=0;
		data->acum_power_domain[i]=0;
	}

	/* Update prev counts */
	do_read_energy_msrs(data,0,0);

	return 0;
}

/*	Invoked on each CPU when stopping system-wide monitoring mode */
static void intel_rapl_on_syswide_refresh_monitor(int cpu, unsigned int virtual_mask)
{
	intel_rapl_thread_data_t* data=&per_cpu(cpu_syswide, cpu);

	/* Accumulate energy readings */
	do_read_energy_msrs(data,1,0);
}

/* 	Dump virtual-counter values for this CPU */
static void intel_rapl_on_syswide_dump_virtual_counters(int cpu, unsigned int virtual_mask,pmc_sample_t* sample)
{
	intel_rapl_thread_data_t* data=&per_cpu(cpu_syswide, cpu);
	int i=0;
	int cnt_virt=0;
	int active_domains=0;

	if (!virtual_mask)
		return;

	/* Embed virtual counter information so that the user can see what's going on */
	for (i=0; i<RAPL_NR_DOMAINS; i++) {
		if (available_power_domains_mask & (1<<i)) {
			if ((virtual_mask & (1<<active_domains)) ) {
				sample->virt_mask|=(1<<active_domains);
				sample->nr_virt_counts++;
				sample->virtual_counts[cnt_virt]=(data->acum_power_domain[i]*1000000)>>energy_units; /* Just PP0 for now */
				cnt_virt++;
			}
			active_domains++;
			// Reset no matter what
			data->acum_power_domain[i]=0;
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
