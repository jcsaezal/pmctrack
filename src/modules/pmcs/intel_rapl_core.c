#include <pmc/intel_rapl.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#include <pmc/pmu_config.h>  /* For cpuid_regs_t */
#include <asm/processor.h>



/* MSR address for the various RAPL registers */
#define MSR_RAPL_POWER_UNIT_PMCTRACK		0x606
#define MSR_PKG_ENERGY_STATUS_PMCTRACK		0x611
#define MSR_PP0_ENERGY_STATUS_PMCTRACK		0x639
#define MSR_PP1_ENERGY_STATUS_PMCTRACK		0x641
#define MSR_DRAM_ENERGY_STATUS_PMCTRACK		0x619

#define MSR_ENERY_STATUS_BITMASK_PMCTRACK 0xffffffff
#define RAPL_TIMER_PERIOD 30*HZ

/*
 * Currently, we have just tested this monitoring modules
 * in supported Intel server processors. If your desktop
 * processor supports Intel RAPL, comment the line below
 * and rebuild/reload the module.
 */
#define SERVER_PROCESSOR


uint_t rapl_msr_regs_domains[RAPL_NR_DOMAINS]= {
	MSR_PP0_ENERGY_STATUS_PMCTRACK,
	MSR_PP1_ENERGY_STATUS_PMCTRACK,
	MSR_PKG_ENERGY_STATUS_PMCTRACK,
	MSR_DRAM_ENERGY_STATUS_PMCTRACK
};


/* Model ID for the Intel Haswell-EP processors */
#define HASWELL_EP_MODEL 63
#define SKYLAKE_MODEL 	85

/* Table of processor models that support Intel RAPL */
static struct pmctrack_cpu_model supported_models[]= {
	{42,"sandybridge"},{45,"sandybridge_ep"},
	{58,"ivybridge"},{62,"ivybridge-ep"},
	{60,"haswell"},{63,"haswell-ep"},
	{79,"broadwell-ep"},
	{86,"broadwell"},
	{85,"skylake"},
	{0,NULL} /* Marker */
};


typedef struct {
	intel_rapl_control_t gbl_ctrl;
	unsigned char use_timer;
	struct timer_list timer;
	spinlock_t lock;
	unsigned char in_use;
	unsigned char first_time;
	unsigned char supported;
	unsigned int processor_model;
	intel_rapl_support_t rapl_info;
} intel_rapl_struct_t;

intel_rapl_struct_t global_rapl_data= {.first_time=1};


static inline void do_read_energy_msrs(intel_rapl_control_t* rapl_control, int acum);

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





/* Check if this processor does support Intel RAPL */
int intel_rapl_probe(void)
{

	int model;

	if (!processor_is_supported(&model))
		return -ENOTSUPP;

	/* Record model */
	global_rapl_data.processor_model=model;

	return 0;
}

/* Function associated with the kernel timer used for TBS mode */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,15,0)
static void rapl_timer(unsigned long data)
#else
static void rapl_timer(struct timer_list *t)
#endif
{
	intel_rapl_update_energy_values(&global_rapl_data.rapl_info,1);
}

/* RAPL CORE initialization */
static int __intel_rapl_initialize(void)
{
	int retval=0;
	uint64_t result;
	intel_rapl_support_t* rapl_info=&global_rapl_data.rapl_info;

	spin_lock_init(&global_rapl_data.lock);

	global_rapl_data.first_time=0;


	if ((retval=intel_rapl_probe())) {
		global_rapl_data.supported=0;
		return retval;
	}

	global_rapl_data.supported=1;

	intel_rapl_control_init(&global_rapl_data.gbl_ctrl);

	/* Read for the first time */
	do_read_energy_msrs(&global_rapl_data.gbl_ctrl,0);

	global_rapl_data.in_use=0;
	global_rapl_data.use_timer=0; /* For now */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,15,0)
	/* Timer initialization (but timer is not added!!) */
	init_timer(&global_rapl_data.timer);
	global_rapl_data.timer.data=0;
	global_rapl_data.timer.function=rapl_timer;
	global_rapl_data.timer.expires=jiffies+RAPL_TIMER_PERIOD;  /* It does not matter for now */
#else
	timer_setup(&global_rapl_data.timer, rapl_timer, 0);
#endif

	/* Init RAPL INFO */
	rdmsrl(MSR_RAPL_POWER_UNIT_PMCTRACK,result);

	rapl_info->power_units=result&0xf;
	rapl_info->energy_units=((result>>8)&0x1f);
	if (global_rapl_data.processor_model==SKYLAKE_MODEL)
		rapl_info->dram_energy_units=16;
	else
		rapl_info->dram_energy_units=rapl_info->energy_units;

	rapl_info->time_units=((result>>16)&0xf);


	/* In the Haswell-EP processor, energy_core is not implemented */
	if (global_rapl_data.processor_model==HASWELL_EP_MODEL) {
		rapl_info->nr_available_power_domains=2;
		rapl_info->available_power_domains_mask=(1 << RAPL_PKG_DOMAIN) | (1 << RAPL_DRAM_DOMAIN);
		rapl_info->available_vcounters[0]="energy_pkg";
		rapl_info->available_vcounters[1]="energy_dram";
		rapl_info->available_vcounters[2]=NULL;
	} else {
		/* Number of available power domains in most RAPL-enabled Intel processors */
		rapl_info->nr_available_power_domains=3;
#ifdef SERVER_PROCESSOR
		/* Configuration for server processors */
		rapl_info-> available_power_domains_mask=(1 << RAPL_PP0_DOMAIN) | (1 << RAPL_PKG_DOMAIN) | (1 << RAPL_DRAM_DOMAIN);
		rapl_info->available_vcounters[0]="energy_core";
		rapl_info->available_vcounters[1]="energy_pkg";
		rapl_info->available_vcounters[2]="energy_dram";
#else
		/* Configuration for desktop processors */
		rapl_info->available_power_domains_mask=(1 << RAPL_PP0_DOMAIN) | (1 << RAPL_PKG_DOMAIN) | (1 << RAPL_PP1_DOMAIN);
		rapl_info->available_vcounters[0]="energy_core";
		rapl_info->available_vcounters[1]="energy_pkg";
		rapl_info->available_vcounters[2]="energy_pp1";
#endif
	}


	return 0;
}


/* Initialize/Release RAPL resources */
int intel_rapl_initialize(intel_rapl_support_t* rapl_support, int use_timer)
{

	int retval=0;
	if (global_rapl_data.first_time)
		__intel_rapl_initialize();

	spin_lock(&global_rapl_data.lock);


	if (!global_rapl_data.supported) {
		retval=-ENOTSUPP;
		goto do_unlock;
	}

	if (global_rapl_data.in_use) {
		retval=-EBUSY;
		goto do_unlock;
	}

	global_rapl_data.in_use=1;
	memcpy(rapl_support,&global_rapl_data.rapl_info,sizeof(intel_rapl_support_t));

	if (use_timer) {
		global_rapl_data.use_timer=1;
		mod_timer( &global_rapl_data.timer, jiffies+RAPL_TIMER_PERIOD);
	}

do_unlock:
	spin_unlock(&global_rapl_data.lock);

	return retval;
}


int intel_rapl_release(intel_rapl_support_t* rapl_support)
{

	spin_lock(&global_rapl_data.lock);

	if (global_rapl_data.use_timer) {
		del_timer_sync(&global_rapl_data.timer);
		global_rapl_data.use_timer=0;
	}

	global_rapl_data.in_use=0;

	spin_unlock(&global_rapl_data.lock);

	return 0;
}


/* For debugging purposes ... */
int intel_rapl_print_energy_units(char* str, intel_rapl_support_t* rapl_support)
{
	intel_rapl_support_t* rapl_info=&global_rapl_data.rapl_info;
	char* dst=str;

	dst+=sprintf(dst,"Power units = %d\n",rapl_info->power_units);
	dst+=sprintf(dst,"Energy units = %d\n",rapl_info->energy_units);
	dst+=sprintf(dst,"DRAM Energy units = %d\n",rapl_info->dram_energy_units);
	dst+=sprintf(dst,"Time units = %d\n",rapl_info->time_units);

	return dst - str;
}


/*
 * Read RAPL energy registers and update cumulative counters in the
 * private thread structure.
 */

static inline void do_read_energy_msrs(intel_rapl_control_t* rapl_control, int acum)
{

	int i=0;
	uint_t old_value,foo, delta;
	intel_rapl_support_t* rapl_info=&global_rapl_data.rapl_info;

	if (!acum) {
		/* Read energy count from msrs */
		for (i=0; i<RAPL_NR_DOMAINS; i++) {
			if (rapl_info->available_power_domains_mask & (1<<i)) {
				rdmsr(rapl_msr_regs_domains[i],rapl_control->cur_domain_value[i],foo);
			}
		}
	} else {
		for (i=0; i<RAPL_NR_DOMAINS; i++) {
			if (rapl_info->available_power_domains_mask & (1<<i)) {
				/* Save old value */
				old_value=rapl_control->cur_domain_value[i];

				/* Read new value */
				rdmsr(rapl_msr_regs_domains[i],rapl_control->cur_domain_value[i],foo);

#ifdef DEBUG
				if (i==0)
					printk(KERN_ALERT "0x%x \n",rapl_control->cur_domain_value[i]);
#endif
				/* Check Overflow */
				if (rapl_control->cur_domain_value[i] < old_value) {
					printk(KERN_ALERT "Overflow detected ...\n");
					delta=MSR_ENERY_STATUS_BITMASK_PMCTRACK-old_value;
					delta+=rapl_control->cur_domain_value[i]+1;
				} else
					delta=rapl_control->cur_domain_value[i]-old_value;

				rapl_control->acum_power_domain[i]+=delta;
			}
		}
	}
}


/* Main functions to interact with RAPL facillities */
void intel_rapl_update_energy_values(intel_rapl_support_t* rapl_support, int acum)
{

	unsigned long flags;

	if (global_rapl_data.use_timer) {
		spin_lock_irqsave(&global_rapl_data.lock,flags);
		do_read_energy_msrs(&global_rapl_data.gbl_ctrl,acum);
		/* Postpone timer ... */
		mod_timer(&global_rapl_data.timer, jiffies+RAPL_TIMER_PERIOD);
		spin_unlock_irqrestore(&global_rapl_data.lock,flags);
	} else {
		/* Lockfree version */
		do_read_energy_msrs(&global_rapl_data.gbl_ctrl,acum);
	}
}


static inline void __intel_rapl_get_energy_sample_thread(intel_rapl_support_t* rapl_info,
        intel_rapl_control_t* rapl_control,
        intel_rapl_sample_t* sample,
        unsigned char clear_acum)
{

	int i;

	for (i=0; i<RAPL_NR_DOMAINS; i++) {
		if (rapl_info->available_power_domains_mask & (1<<i)) {

			if (i==RAPL_DRAM_DOMAIN)
				sample->energy_value[i]=(rapl_control->acum_power_domain[i]*1000000)>>rapl_info->dram_energy_units;
			else
				sample->energy_value[i]=(rapl_control->acum_power_domain[i]*1000000)>>rapl_info->energy_units;
		}

		if (clear_acum)
			rapl_control->acum_power_domain[i]=0;

	}

}

void intel_rapl_get_energy_sample(intel_rapl_support_t* rapl_support, intel_rapl_sample_t* sample)
{
	__intel_rapl_get_energy_sample_thread(&global_rapl_data.rapl_info,
	                                      &global_rapl_data.gbl_ctrl,
	                                      sample,0);
}

void intel_rapl_reset_energy_values(intel_rapl_support_t* rapl_support)
{
	int i=0;

	for (i=0; i<RAPL_NR_DOMAINS; i++)
		global_rapl_data.gbl_ctrl.acum_power_domain[i]=0;
}

void intel_rapl_control_init(intel_rapl_control_t* rapl_control)
{
	int i;

	for (i=0; i<RAPL_NR_DOMAINS; i++) {
		rapl_control->cur_domain_value[i]=0;
		rapl_control->acum_power_domain[i]=0;
	}
}

void intel_rapl_update_energy_values_thread(intel_rapl_support_t* rapl_support,
        intel_rapl_control_t* rapl_control,
        int acum)
{

	do_read_energy_msrs(rapl_control,acum);
}

void intel_rapl_get_energy_sample_thread(intel_rapl_support_t* rapl_support,
        intel_rapl_control_t* rapl_control,
        intel_rapl_sample_t* sample)
{

	__intel_rapl_get_energy_sample_thread(&global_rapl_data.rapl_info,
	                                      rapl_control,
	                                      sample,1);

}