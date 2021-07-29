
/*
 *  pmu_config_perf.c
 *
 *  Configuration code for Performance Monitoring Units (PMUs)
 *  using Perf Event's kernel API
 *
 *  Copyright (C) 2020 Jaime Saez de Buruaga <jsaezdeb@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 *
 *  Generalization of the Perf backend for Intel, AMD, ARMv7
 *  and ARMv8 by Lazaro Clemen and Juan Carlos Saez
 *  (C) 2021 Lazaro Clemen Palafox <lazarocl@ucm.es>
 *  (C) 2021 Juan Carlos Saez <jcsaezal@ucm.es>
 */
/*
 * Part of this code is based on pmu_config_x86.c
 */

#include <pmc/mc_experiments.h>
#include <pmc/pmu_config.h>
#if defined(__aarch64__) || defined (__arm__)
#define IS_INTEL 0
#if defined (__aarch64__)
#include <pmc/arm64/pmc_bit_layout.h>
#else
#include <pmc/arm/pmc_bit_layout.h>
#endif
#else
#define IS_INTEL (boot_cpu_data.x86_vendor==X86_VENDOR_INTEL)
#include <pmc/amd/pmc_bit_layout.h> /* Added Intel and AMD layout */
#include <asm/apic.h>
#endif
#include <asm/processor.h>
#include <linux/printk.h>
#include <linux/cpu.h>
#include <linux/ftrace.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
#include <linux/cpuhotplug.h>
#endif

/* Variables to aid in detecting the various PMUs in the system */
int coretype_cpu_static[NR_CPUS];
int* coretype_cpu=coretype_cpu_static;
int  nr_core_types=1;

/* Global definition of prop coretype */
pmu_props_t pmu_props_cputype[PMC_CORE_TYPES];
pmu_props_t pmu_props_cpu[NR_CPUS];


/*
 * Code to list and detect supported
 * Intel processors
 */
#if defined(__i386__) || defined (__amd64__) || defined(_x86_64_)

/* Supported Intel Processors */
static struct pmctrack_cpu_model cpu_models[]= {
	{28,"atom"},{23,"core2"},{26,"nehalem"},
	{42,"sandybridge"},{45,"sandybridge_ep"},
	{58,"ivybridge"},{62,"ivybridge-ep"},
	{60,"haswell"},{63,"haswell-ep"},
	{79,"broadwell-ep"},
	{86,"broadwell"},
	{85,"skylake"},
	{0,NULL} /* Marker */
};

void fill_model_string(int model, char* model_str)
{
	int i=0;
	while(cpu_models[i].model!=0 && cpu_models[i].model!=model)
		i++;
	if (cpu_models[i].model==0)
		strcpy(model_str,"x86_intel-core.unknown");
	else
		sprintf(model_str,"x86_intel-core.%s",cpu_models[i].model_str);
}
#endif


#if defined(__aarch64__) || defined (__arm__)
/*
 * Code to list and detect fully-supported ARM Cortex processors
 */
static struct pmctrack_cpu_model cpu_models[]= {
#if defined(__aarch64__)
	{0xd03,"cortex_a53"},{0xd07,"cortex_a57"},{0xd09,"cortex_a73"},
#else
	{0xc0f,"cortex_a15"},{0xc07,"cortex_a7"},
#endif
	{0,NULL} /* Marker */
};

static void fill_model_string(int model, char* model_str)
{
	int i=0;
	char* model_prefix
#if defined(__aarch64__)
	    ="armv8";
#else
	    ="armv7";
#endif

	while(cpu_models[i].model!=0 && cpu_models[i].model!=model)
		i++;
	if (cpu_models[i].model==0)
		sprintf(model_str,"%s.unknown",model_prefix);
	else
		sprintf(model_str,"%s.%s",model_prefix,cpu_models[i].model_str);
}

/* Initialize PMU properties of the current CPU  */
static void init_pmu_props_cpu(void* dummy)
{
	int this_cpu=smp_processor_id();
	pmcr_t reg;
	int i=0;
	u32 cpuid;
	u32 model;
	pmu_props_t* props=&pmu_props_cpu[this_cpu];

	init_pmcr(&reg);

	/* Read the nb of CNTx counters supported from PMNC */
#ifdef __aarch64__
	props->nr_gp_pmcs=get_bit_field32(&reg.m_n) & ARMV8_PMCR_N_MASK ;
#else
	props->nr_gp_pmcs=get_bit_field32(&reg.m_n) & ARMV7_PMNC_N_MASK ;
#endif
	props->nr_fixed_pmcs=1; //Cycle counter
	props->pmc_width=32;

	/* Mask */
	props->pmc_width_mask=0;
	for (i=0; i<props->pmc_width; i++)
		props->pmc_width_mask|=(1ULL<<i);

	/* Read PMU ID*/
	props->processor_model=reg.m_value;

	/* Hack extracted from c_show() in arch/arm/kernel/setup.c */
	cpuid=read_cpuid_id();

	if ((cpuid & 0x0008f000) == 0x00000000) {
		/* pre-ARM7 */
		model=cpuid >> 4;
	} else {
		model=(cpuid >> 4) & 0xfff;
	}
	fill_model_string(model,props->arch_string);
}

#ifdef DEBUG
static void print_pmu_info_cpu(int cpu)
{
	pmu_props_t* props=&pmu_props_cpu[cpu];

	printk("*** CPU %d***\n",cpu);
	printk(KERN_INFO "Version Id:: 0x%lx\n", props->processor_model);
	printk("GP Counter per Logical Processor:: %d\n",props->nr_gp_pmcs);
	printk("Number of fixed func. counters:: %d\n", props->nr_fixed_pmcs);
	printk("Bit width of the PMC:: %d\n", props->pmc_width);
	printk("***************\n");
}
#endif

/* Detect PMUs available in the system  */
void init_pmu_props(void)
{
	int coretype=0;
	int cpu=0;
	int model_cpu=0;
	pmu_props_t* props;
	int i=0;
	static pmu_flag_t pmu_flags[]= {
		{"pmc",8,0},
		{"usr",1,0},
		{"os",1,0},
		{"ebs",32,0},
		{"pmu",8,0},
		{"systemwide",1,0},
		{"coretype",1,1},
		{NULL,0,0}
	};
#define MAX_CORETYPES 2
	int processor_model[MAX_CORETYPES];

	on_each_cpu(init_pmu_props_cpu, NULL, 1);

	nr_core_types=0;

	for (cpu=0; cpu<num_present_cpus(); cpu++) {
		model_cpu=pmu_props_cpu[cpu].processor_model;
		coretype=0;
		// Search model type in the list
		while (coretype<nr_core_types && model_cpu!=processor_model[coretype])
			coretype++;

		if (coretype>=nr_core_types) { /* Model not found */
			/* Allocate core type */
			processor_model[nr_core_types]=model_cpu;
			coretype_cpu[cpu]=nr_core_types;
			pmu_props_cpu[cpu].coretype=nr_core_types;
			nr_core_types++;
		} else  {/* Model found */
			coretype_cpu[cpu]=coretype;
			pmu_props_cpu[cpu].coretype=coretype;
		}
	}

	printk("*** PMU Info ***\n");
	printk("Number of core types detected:: %d\n",nr_core_types);

	for (coretype=0; coretype<nr_core_types; coretype++) {
		cpu=get_any_cpu_coretype(coretype);
		props=&pmu_props_cpu[cpu];
		pmu_props_cputype[coretype]=(*props);
		props=&pmu_props_cputype[coretype];
		/* Add flags */
		props->nr_flags=0;
		for (i=0; pmu_flags[i].name!=NULL; i++) {
			props->flags[i]=pmu_flags[i];
			props->nr_flags++;
		}
		printk("[PMU coretype%d]\n",coretype);
		printk(KERN_INFO "Version Id:: 0x%lx\n", props->processor_model);
		printk("GP Counter per Logical Processor:: %d\n",props->nr_gp_pmcs);
		printk("Number of fixed func. counters:: %d\n", props->nr_fixed_pmcs);
		printk("Bit width of the PMC:: %d\n", props->pmc_width);
	}
	printk("***************\n");

#ifdef DEBUG
	for (cpu=0; cpu<num_present_cpus(); cpu++)
		print_pmu_info_cpu(cpu);
#endif
}
#else
/* Initialize PMU properties of the current CPU  */
static void init_pmu_props_cpu(void* dummy)
{
	int this_cpu=smp_processor_id();
	struct cpuinfo_x86 *cpudata = &cpu_data(this_cpu);
	int i=0;
	pmu_props_t* props=&pmu_props_cpu[this_cpu];
	cpuid_regs_t rv;
	int nr_gp_pmcs;
	unsigned int model=0;

	if (cpudata->x86_vendor == X86_VENDOR_INTEL) {
		rv.eax=0x0A;
		rv.ebx=rv.edx=rv.ecx=0x0;
		run_cpuid(rv);
		nr_gp_pmcs=(rv.eax&0xFF00)>>8;
		props->processor_model=rv.eax&0xFF;
#ifdef DEBUG
		if (this_cpu==0) {
			printk("*** PMU Info ***\n");
			printk("Version Id:: 0x%x\n", rv.eax&0xFF);
			printk("GP Counter per Logical Processor:: %d\n",nr_gp_pmcs);
			printk("Bit width of the PMC:: %d\n", (rv.eax&0xFF0000)>>16);

			if ((rv.eax&0xFF) > 1) {
				printk("Number of fixed func. counters:: %d\n", rv.edx&0x1F);
				printk("Bit width of fixed func. counters:: %d\n", (rv.edx&0x1FE0)>>5);
			}
			printk("***************\n");
		}
#endif
		/* In some processors, cpuid does not display the number of fixed pmcs correctly ==> force it!! */
		props->nr_fixed_pmcs=3; //(rv.edx&0x1F);
		props->nr_gp_pmcs=nr_gp_pmcs;
		props->pmc_width=(rv.eax&0xFF0000)>>16; //This is for ffunction (rv.edx&0x1FE0)>>5;
		/* Figure out CPU */
		rv.eax=0x1;
		run_cpuid(rv);
		model= (rv.eax & 0xF0) >> 4;
		/* Hack extracted from get_x86_model()
		   arch/x86/kernel/cpu/microcode/intel_early.c
		 */
		model += ((rv.eax >> 16) & 0xf) << 4;
		fill_model_string(model,props->arch_string);
	}   else {
		props->nr_fixed_pmcs=0;
		props->pmc_width=48; /* 48-bit wide */

		switch(cpudata->x86) {
		case 0x17:
		case 0x19:
			props->nr_gp_pmcs=6;
			strcpy(props->arch_string,"x86_amd.epyc");
			break;
		default:
			props->nr_gp_pmcs=4;
			/* Forced for now */
			strcpy(props->arch_string,"x86_amd.opteron");
		}
	}

	/* Mask */
	props->pmc_width_mask=0;
	for (i=0; i<props->pmc_width; i++)
		props->pmc_width_mask|=(1ULL<<i);

	coretype_cpu_static[this_cpu]=0;
	props->coretype=0; /* For now we do nothing fancy here*/
}


#ifdef DEBUG
static void print_pmu_info_cpu(int cpu)
{
	pmu_props_t* props=&pmu_props_cpu[cpu];

	printk("*** CPU %d***\n",cpu);
	printk(KERN_INFO "Version Id:: 0x%lx\n", props->processor_model);
	printk("GP Counter per Logical Processor:: %d\n",props->nr_gp_pmcs);
	printk("Number of fixed func. counters:: %d\n", props->nr_fixed_pmcs);
	printk("Bit width of the PMC:: %d\n", props->pmc_width);
	printk("***************\n");
}
#endif

/* Detect PMUs available in the system  */
void init_pmu_props(void)
{
	int coretype=0;
	int cpu=0;
	int model_cpu=0;
	pmu_props_t* props;
	int i=0;
	int this_cpu=smp_processor_id();
#define MAX_CORETYPES 2
	int processor_model[MAX_CORETYPES];
	struct cpuinfo_x86 *cpudata = &cpu_data(this_cpu);
	static pmu_flag_t pmu_flags[]= {
		{"pmc",64,0},
		{"usr",1,0},
		{"os",1,0},
		{"umask",8,0},
		{"cmask",8,0},
		{"edge",1,0},
		{"inv",1,0},
		{"any",1,0},
		{"ebs",32,0},
		{"systemwide",1,0},
		{"coretype",1,1},
		{"config1",64,0},
		{"config2",64,0},
		{"pmu",8,0},
		{NULL,0,0}
	};

	/* Patch bitwidth AMD */
	if (cpudata->x86_vendor==X86_VENDOR_AMD)
		pmu_flags[0].bitwidth=64;

	on_each_cpu(init_pmu_props_cpu, NULL, 1);

	for (cpu=0; cpu<nr_cpu_ids; cpu++) {
		pmu_props_cpu[cpu].coretype=get_coretype_cpu(cpu);
	}

#ifdef DEBUG
	for (cpu=0; cpu<num_present_cpus(); cpu++)
		print_pmu_info_cpu(cpu);
#endif

	nr_core_types=0;
	//nr_cpu_ids
	//num_online_cpus()
	for (cpu=0; cpu<num_present_cpus(); cpu++) {
		model_cpu=pmu_props_cpu[cpu].processor_model;
		coretype=0;
		// Search model type in the list
		while (coretype<nr_core_types && model_cpu!=processor_model[coretype])
			coretype++;

		if (coretype>=nr_core_types) { /* Model not found */
			/* Allocate core type */
			processor_model[nr_core_types]=model_cpu;
			coretype_cpu[cpu]=nr_core_types;
			pmu_props_cpu[cpu].coretype=nr_core_types;
			nr_core_types++;
		} else  {/* Model found */
			coretype_cpu[cpu]=coretype;
			pmu_props_cpu[cpu].coretype=coretype;
		}
	}

	printk("*** PMU Info ***\n");
	printk("Number of core types detected:: %d\n",nr_core_types);

	for (coretype=0; coretype<nr_core_types; coretype++) {
		cpu=get_any_cpu_coretype(coretype);

		props=&pmu_props_cpu[cpu];
		pmu_props_cputype[coretype]=(*props);
		props=&pmu_props_cputype[coretype];
		/* Add flags */
		props->nr_flags=0;
		for (i=0; pmu_flags[i].name!=NULL; i++) {
			props->flags[i]=pmu_flags[i];
			props->nr_flags++;
		}
		printk("[PMU coretype%d]\n",coretype);
		printk(KERN_INFO "Version Id:: 0x%lx\n", props->processor_model);
		printk("GP Counter per Logical Processor:: %d\n",props->nr_gp_pmcs);
		printk("Number of fixed func. counters:: %d\n", props->nr_fixed_pmcs);
		printk("Bit width of the PMC:: %d\n", props->pmc_width);
	}
	printk("***************\n");
}
#endif

/*
 * Budget used is Up. PMU genrate an interrupt
 * this run in hardirq, nmi context with irq disabled
 */
void event_overflow_callback(struct perf_event *event,
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 2, 0)
                             int nmi,
#endif
                             struct perf_sample_data *data, struct pt_regs *regs)
{
	uint64_t overflow_mask;
	pmon_prof_t* prof = get_prof(current);
	core_experiment_t* core_exp=NULL;

	if (!get_prof_enabled(prof) || !prof->pmcs_config) return;

	/* Retrieve core_experiment from pmon_prof_t */
	core_exp=prof->pmcs_config;

	overflow_mask=(0x1 << core_exp->log_to_phys[core_exp->ebs_idx]);

	do_count_on_overflow(regs, overflow_mask);
}

int noinline trace_pmctrack_init_perf_counter(struct perf_event_attr* perf_hw_attr, pmc_usrcfg_t* pmc_cfg, int cpu, struct task_struct* task)
{
	asm(" ");
	return cpu;
}

/*
 * Initialization of perf event counter using a PMCTrack's
 * configuration structrure
 */
struct perf_event *init_counter(struct task_struct* task,int cpu,
                                pmc_usrcfg_t* pmc_cfg, uint64_t config_mask)
{
	struct perf_event *event = NULL;
	struct perf_event_attr perf_hw_attr = {
		.type           = pmc_cfg->cfg_pmu!=-1? pmc_cfg->cfg_pmu : PERF_TYPE_RAW,
		.size           = sizeof(struct perf_event_attr),
		.config         = pmc_cfg->cfg_pmu!=-1? pmc_cfg->cfg_evtsel: config_mask,
		.disabled       = !pmc_cfg->cfg_force_enable, /* Critical: alwas disabled on creation */
		.exclude_kernel = (pmc_cfg->cfg_os==0 && pmc_cfg->cfg_usr==1),
		.config1 = pmc_cfg->cfg_config1,
		.config2 = pmc_cfg->cfg_config2,
	};

	if (pmc_cfg->cfg_systemwide) {
		perf_hw_attr.exclude_kernel=0;
		task=NULL;
		cpu=0;
	}

	if (pmc_cfg->cfg_ebs_mode)
		perf_hw_attr.sample_period = pmc_cfg->cfg_reset_value;

	trace_pmctrack_init_perf_counter(&perf_hw_attr,pmc_cfg,cpu,task);

	event = perf_event_create_kernel_counter(
	            &perf_hw_attr,
	            cpu, task,
	            pmc_cfg->cfg_ebs_mode?event_overflow_callback:NULL
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 2, 0)
	            , NULL
#endif
	        );

	if(!event)
		return NULL;

	if(IS_ERR(event)) {
		pr_err("cpu%d. unable to create perf event: %ld\n", cpu, PTR_ERR(event));
		return NULL;
	}

	return event;
}


#if defined(__aarch64__) || defined (__arm__)

#if defined(__aarch64__)
#define PERFCTR_CPU_CYCLES	0x11
#else
#define PERFCTR_CPU_CYCLES	0xFF
#endif

/*
 * Transform an array of platform-agnostic PMC counter configurations (pmc_cfg)
 * into a low level structure that holds the necessary data to configure hardware counters.
 */
int do_setup_pmcs(pmc_usrcfg_t* pmc_cfg, int used_pmcs_msk,core_experiment_t* exp,int cpu, int exp_idx, struct task_struct *task)
{
	int i,j;
	low_level_exp* lle;
	pmu_props_t* props_cpu=get_pmu_props_cpu(cpu);
	uint64_t config_mask;
	struct hw_event* llex;
	int nr_fixed_pmcs_used=0;
	int max_events=props_cpu->nr_fixed_pmcs+props_cpu->nr_gp_pmcs;

	init_core_experiment_t(exp, exp_idx);
	exp->need_setup=0; /* In perf setup has been already handled */

	/* Set mask of used pmcs !! */
	exp->used_pmcs=used_pmcs_msk;

	for (i=0; i<max_events; i++) {
		/* PMC is used */
		if ( used_pmcs_msk & (0x1<<i)) {

			/* Set up int just in case */
			if (pmc_cfg[i].cfg_ebs_mode)  ///* Set EBS idx */
				exp->ebs_idx=exp->size;

			lle=&exp->array[exp->size++];

			/* Retrieve low level exp from core_experiment */
			llex=&lle->event;

			init_hw_event(llex);


			if (props_cpu->nr_fixed_pmcs>0 && i<props_cpu->nr_fixed_pmcs) {
				/* Retrieve PERF RAW config from fixed/normal event config */
				config_mask=PERFCTR_CPU_CYCLES; /* Cycle counter is preferred */
				nr_fixed_pmcs_used++;
			} else {
				pmxevtyper_t evtsel;

				init_pmxevtyper(&evtsel);
				set_bit_field32(&evtsel.m_evt_count, pmc_cfg[i].cfg_evtsel);
				//set_bit_field32(&evtsel.m_u, pmc_cfg[i].cfg_usr);
				//set_bit_field32(&evtsel.m_p,pmc_cfg[i].cfg_os);
				set_bit_field32(&evtsel.m_nsk,!pmc_cfg[i].cfg_os);
				set_bit_field32(&evtsel.m_nsu,!pmc_cfg[i].cfg_usr);

				/* Retrieve configuration from eventsel */
				config_mask=evtsel.m_value;
			}

			/* Retrieve 'reset_value' from config to hw_event */
			llex->reset_value=pmc_cfg[i].cfg_reset_value;

			/* Init PERF counter */
			llex->event=init_counter(task,-1, &pmc_cfg[i], config_mask);

			if (llex->event==NULL)
				goto free_up_err;

			/* Init EVENT TASK struct */
			llex->task=task;

			/* Add metainfo in the core experiment */
			exp->log_to_phys[exp->size-1]=i;
			exp->phys_to_log[i]=exp->size-1;
		}
	}

	return 0;
free_up_err:
	for (j=0; j<(exp->size-1); j++)
		perf_event_release_kernel(exp->array[j].event.event);
	exp->size=0;

	return -EINVAL;
}
#else
/*
 * Transform an array of platform-agnostic PMC counter configurations (pmc_cfg)
 * into a low level structure that holds the necessary data to configure hardware counters.
 */
int do_setup_pmcs(pmc_usrcfg_t* pmc_cfg, int used_pmcs_msk,core_experiment_t* exp,int cpu, int exp_idx, struct task_struct *task)
{
	int i,j;
	int gppmc_id=0;
	low_level_exp* lle;
	pmu_props_t* props_cpu=get_pmu_props_cpu(cpu);
	int is_intel=IS_INTEL;
	uint64_t config_mask;
	struct hw_event* llex;
	int nr_fixed_pmcs_used=0;
	unsigned int perf_config_fixed[]= {0x00c0,0x003c,0x0300};
	int max_events=props_cpu->nr_fixed_pmcs+props_cpu->nr_gp_pmcs;

	init_core_experiment_t(exp, exp_idx);
	exp->need_setup=0; /* In perf setup has been already handled */

	/* Set mask of used pmcs !! */
	exp->used_pmcs=used_pmcs_msk;

	for (i=0; i<max_events; i++) {
		/* PMC is used */
		if ( used_pmcs_msk & (0x1<<i)) {

			/* Set up int just in case */
			if (pmc_cfg[i].cfg_ebs_mode)  ///* Set EBS idx */
				exp->ebs_idx=exp->size;

			lle=&exp->array[exp->size++];

			/* Retrieve low level exp from core_experiment */
			llex=&lle->event;

			init_hw_event(llex);

			/* Intel fixed-count event */
			if (is_intel &&
			    props_cpu->nr_fixed_pmcs>0 &&
			    i<props_cpu->nr_fixed_pmcs) {
				/* Retrieve PERF RAW config from fixed/normal event config */
				config_mask=perf_config_fixed[i];
				nr_fixed_pmcs_used++;
			} else if (is_intel) {
				intel_evtsel_msr evtsel;
				gppmc_id=i-props_cpu->nr_fixed_pmcs;

				/*
				 * In modern Intel CPUs with more than 4 GP-PMCs the 'os' flag must be
				 * enable forcefully for GP-PMCs whose ID ranges between 4 and 7. Otherwise
				 * these performance counters will always display a 0 count.
				 * Note that this is necessary due to a hardware issue.
				 */
				if (gppmc_id>=4 && gppmc_id<=7)
					pmc_cfg[i].cfg_os=1;

				init_intel_evtsel_msr(&evtsel);
				set_bit_field(&evtsel.m_evtsel, pmc_cfg[i].cfg_evtsel);
				set_bit_field(&evtsel.m_umask, pmc_cfg[i].cfg_umask);
				set_bit_field(&evtsel.m_e,pmc_cfg[i].cfg_edge);
				set_bit_field(&evtsel.m_inv, pmc_cfg[i].cfg_inv);
				set_bit_field(&evtsel.m_cmask, pmc_cfg[i].cfg_cmask);
				set_bit_field(&evtsel.m_any, pmc_cfg[i].cfg_any);
				config_mask=evtsel.m_value;

			} else {
				amd_evtsel_msr evtsel;

				init_amd_evtsel_msr(&evtsel);
				set_bit_field(&evtsel.m_evtsel, pmc_cfg[i].cfg_evtsel);
				set_bit_field(&evtsel.m_evtsel2,(pmc_cfg[i].cfg_evtsel >>8));
				set_bit_field(&evtsel.m_umask, pmc_cfg[i].cfg_umask);
				set_bit_field(&evtsel.m_e,pmc_cfg[i].cfg_edge);
				set_bit_field(&evtsel.m_inv, pmc_cfg[i].cfg_inv);
				set_bit_field(&evtsel.m_cmask, pmc_cfg[i].cfg_cmask);
				config_mask=evtsel.m_value;
			}

			/* Retrieve 'reset_value' from config to hw_event */
			llex->reset_value=pmc_cfg[i].cfg_reset_value;

			/* Init PERF counter */
			llex->event=init_counter(task,-1, &pmc_cfg[i], config_mask);

			if (llex->event==NULL)
				goto free_up_err;

			/* Init EVENT TASK struct */
			llex->task=task;

			/* Add metainfo in the core experiment */
			exp->log_to_phys[exp->size-1]=i;
			exp->phys_to_log[i]=exp->size-1;
		}
	}

	return 0;
free_up_err:
	for (j=0; j<(exp->size-1); j++)
		perf_event_release_kernel(exp->array[j].event.event);
	exp->size=0;

	return -EINVAL;
}

#endif

/*
 * Enable perf counter asociated with perf_event.
 */
void perf_enable_counters(core_experiment_t* exp)
{
	int i;

	if (exp)
		for(i=0; i<exp->size; ++i)
			perf_event_enable(exp->array[i].event.event);
}

/*
 * Disable perf counter asociated with perf_event.
 */
void perf_disable_counters( core_experiment_t* exp)
{
	int i;

	if (exp)
		for(i=0; i<exp->size; ++i)
			perf_event_disable(exp->array[i].event.event);
}

/*
 * Fill the various fields in a pmc_cfg structure based on a PMC configuration string specified in raw format
 * (e.g., pmc0=0xc0,pmc1=0x76,...).
 */
int parse_pmcs_strconfig(const char *buf,
                         unsigned char ebs_allowed,
                         pmc_usrcfg_t* pmc_cfg,
                         unsigned int* used_pmcs_mask,
                         unsigned int* nr_pmcs,
                         int *ebs_index,
                         int *coretype)
{
	static const unsigned int default_ebs_window=500000000;
	int read_tokens=0;
	int idx, val;
	char cpbuf[PMCTRACK_MAX_LEN_RAW_PMC_STRING];
	char* strconfig=cpbuf;
	char* flag;
	int curr_flag=0;
	unsigned int used_pmcs=0; /* Mask to indicate which counters are actually used*/
	int error=0;
	int ebs_idx=-1;
	unsigned int ebs_window=0;
	unsigned int pmc_count=0;
	int coretype_selected=-1;	/* No coretype for now */
	uint64_t uval;

	/*
	 * Create a copy of the buf string since strsep()
	 * actually modifies the string by replacing the delimeter
	 * with the null byte ('\0')
	 */
	strncpy(strconfig,buf,PMCTRACK_MAX_LEN_RAW_PMC_STRING);
	strconfig[PMCTRACK_MAX_LEN_RAW_PMC_STRING-1]='\0';

	/* Clear array */
	memset(pmc_cfg,0,sizeof(pmc_usrcfg_t)*MAX_LL_EXPS);

	while((flag = strsep(&strconfig, ","))!=NULL) {
		if((read_tokens=sscanf(flag,"pmc%i=%llx", &idx, &uval))>0) {
			if ((idx<0 || idx>=MAX_LL_EXPS)) {
				error=1;
				break;
			}
			used_pmcs|=(0x1<<idx);
			pmc_count++;
			/* By default enable count in user mode */
			pmc_cfg[idx].cfg_usr=1;

			/* By default disable PMU */
			pmc_cfg[idx].cfg_pmu=-1;

			/* Write evtsel too */
			if (read_tokens==2) {
				pmc_cfg[idx].cfg_evtsel=uval;
			}

		} else if (((read_tokens=sscanf(flag,"umask%i=%x", &idx, &val))==2 || (read_tokens=sscanf(flag,"um%i=%x", &idx, &val))==2 ) && (idx>=0 && idx<MAX_LL_EXPS)) {
			pmc_cfg[idx].cfg_umask=val;
		} else if (((read_tokens=sscanf(flag,"cmask%i=%x", &idx, &val))==2 || (read_tokens=sscanf(flag,"cm%i=%x", &idx, &val))==2) && (idx>=0 && idx<MAX_LL_EXPS)) {
			pmc_cfg[idx].cfg_cmask=val;
		} else if ((read_tokens=sscanf(flag,"usr%i=%d", &idx, &val))>0 && (idx>=0 && idx<MAX_LL_EXPS)) {
			if (read_tokens==2) {
				if (val!=0 && val!=1) {
					error=1;
					break;
				} else {
					pmc_cfg[idx].cfg_usr=val;
				}
			} else {
				pmc_cfg[idx].cfg_usr=1;
			}
		} else if ((read_tokens=sscanf(flag,"os%i=%d", &idx, &val))>0 && (idx>=0 && idx<MAX_LL_EXPS)) {
			if (read_tokens==2) {
				if (val!=0 && val!=1) {
					error=1;
					break;
				} else {
					pmc_cfg[idx].cfg_os=val;
				}
			} else {
				pmc_cfg[idx].cfg_os=1;
			}
		} else if ((read_tokens=sscanf(flag,"edge%i=%d", &idx, &val))>0 && (idx>=0 && idx<MAX_LL_EXPS)) {
			if (read_tokens==2) {
				if (val!=0 && val!=1) {
					error=1;
					break;
				} else {
					pmc_cfg[idx].cfg_edge=val;
				}
			} else {
				pmc_cfg[idx].cfg_edge=1;
			}
		} else if ((read_tokens=sscanf(flag,"inv%i=%d", &idx, &val))>0 && (idx>=0 && idx<MAX_LL_EXPS)) {
			if (read_tokens==2) {
				if (val!=0 && val!=1) {
					error=1;
					break;
				} else {
					pmc_cfg[idx].cfg_inv=val;
				}
			} else {
				pmc_cfg[idx].cfg_inv=1;
			}
		} else if ((read_tokens=sscanf(flag,"any%i=%d", &idx, &val))>0 && (idx>=0 && idx<MAX_LL_EXPS)) {
			if (read_tokens==2) {
				if (val!=0 && val!=1) {
					error=1;
					break;
				} else {
					pmc_cfg[idx].cfg_any=val;
				}
			} else {
				pmc_cfg[idx].cfg_any=1;
			}
		} else if((read_tokens=sscanf(flag,"ebs%i=%d", &idx, &ebs_window))>0
		          && ebs_allowed
		          && (idx>=0 && idx<MAX_LL_EXPS)
		          && (ebs_idx==-1)) { /* Only if ebs is not enabled for other event already */
			if (read_tokens==1) {
				ebs_window=default_ebs_window;
			}

			/* Update ebs config */
			pmc_cfg[idx].cfg_ebs_mode=1;
			pmc_cfg[idx].cfg_reset_value=ebs_window;
			ebs_idx=idx;

		} else if((read_tokens=sscanf(flag,"coretype=%d", &idx))==1
		          && (idx>=0 && idx<AMP_MAX_CORETYPES)) {
			coretype_selected=idx;
		} else if((read_tokens=sscanf(flag,"pmu%i=%d", &idx, &val))>0 && (idx>=0 && idx<MAX_LL_EXPS)) {
			pmc_cfg[idx].cfg_pmu=val;
		} else if((read_tokens=sscanf(flag,"systemwide%i=%d", &idx, &val))>0 && (idx>=0 && idx<MAX_LL_EXPS)) {
			if (read_tokens==2) {
				if (val!=0 && val!=1) {
					error=1;
					break;
				} else {
					pmc_cfg[idx].cfg_systemwide=val;
				}
			} else {
				pmc_cfg[idx].cfg_systemwide=1;
			}
		} else if (((read_tokens=sscanf(flag,"config1%i=%llx", &idx, &uval))==2 ||
		            (read_tokens=sscanf(flag,"c1%i=%llx", &idx, &uval))==2)
		           && (idx>=0 && idx<MAX_LL_EXPS)) {
			pmc_cfg[idx].cfg_config1=uval;
		} else if (((read_tokens=sscanf(flag,"config2%i=%llx", &idx, &uval))==2 ||
		            (read_tokens=sscanf(flag,"c2%i=%llx", &idx, &uval))==2)
		           && (idx>=0 && idx<MAX_LL_EXPS)) {
			pmc_cfg[idx].cfg_config2=uval;
		} else {
			error=1;
			break;
		}

		curr_flag++;
	}

	if (error) {
		printk(KERN_INFO "Unrecognized format in flag %s\n",flag);
		return curr_flag+1;
	} else {
		(*used_pmcs_mask)=used_pmcs;
		(*nr_pmcs)=pmc_count;
		(*ebs_index)=ebs_idx;
		/* Important note: In perf always assume coretype 0 regardless the system */
		(*coretype)=0; /**coretype_selected; **/
		return 0;
	}
}

/*
 * Generate a summary string with the configuration of a hardware counter (lle)
 * in human-readable format. The string is stored in buf.
 */
int print_pmc_config(low_level_exp* lle, char* buf)
{
	/* TBD */
	return 0;
}


#ifdef DEBUG
int print_pmu_msr_values_debug(char* line_out)
{
	int i;
	pmu_props_t* props_cpu=get_pmu_props_cpu(0);
	char* dst=line_out;
	_msr_t msr;
	int cnt=0;
	_msr_t fixed_count_msr;
	_pmc_t pmc;
	int is_intel=IS_INTEL;

	if (is_intel) {
		for (i=0; i<props_cpu->nr_fixed_pmcs; i++,cnt++) {
			msr.address=MSR_PERF_FIXED_CTR_CTRL;
			fixed_count_msr.address=MSR_PERF_FIXED_CTR0+i;
			readMSR(&msr);
			readMSR(&fixed_count_msr);
			trace_printk("evtsel%i=0x%016llX\npmc%i=0x%016llX\n",cnt,msr.new_value,cnt,fixed_count_msr.new_value);
		}
	}

	for (i=0; i<props_cpu->nr_gp_pmcs; i++,cnt++) {
		msr.address=EVTSEL_MSR_INITIAL_ADDRESS+i;
		pmc.pmc_address=i;
		pmc.msr.address=PMC_MSR_INITIAL_ADDRESS+i;
		readMSR(&msr);
		readPMC(&pmc);
		trace_printk("evtsel%i=0x%016llX\npmc%i=0x%016llX\n",cnt,msr.new_value,cnt,pmc.msr.new_value);
	}

	if (is_intel) {
		msr.address=MSR_PERF_GLOBAL_STATUS;
		readMSR(&msr);
		trace_printk("MSR_PERF_GLOBAL_STATUS=0x%016llX\n",msr.new_value);

		msr.address=MSR_PERF_GLOBAL_CTRL;
		readMSR(&msr);
		trace_printk("MSR_PERF_GLOBAL_CTRL=0x%016llX\n",msr.new_value);

		msr.address=MSR_PERF_GLOBAL_OVF_CTRL;
		readMSR(&msr);
		trace_printk("MSR_PERF_GLOBAL_OVF_CTRL=0x%016llX\n",msr.new_value);
	}

	dst+=sprintf(dst,"See ftrace output. Using trace_printk()\n");
	return dst-line_out;
}

#endif


#if LINUX_VERSION_CODE < KERNEL_VERSION(4,10,0)
/* TODO Not implemented for now (robust for online, offline cpus) */
int pmctrack_cpu_notifier(struct notifier_block *b, unsigned long action,
                          void *data)
{
	//int cpu = (unsigned long)data;
	switch (action) {
	case CPU_DOWN_FAILED:
	case CPU_ONLINE:
		//smp_call_function_single(cpu, nmi_cpu_up, NULL, 0);
		break;
	case CPU_DOWN_PREPARE:
		//smp_call_function_single(cpu, nmi_cpu_down, NULL, 1);
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block pmctrack_cpu_nb = {
	.notifier_call = pmctrack_cpu_notifier,
};
#else
static enum cpuhp_state pcap_pmctrack_online;

static int pmctrack_cpu_online(unsigned int cpu)
{
	return 0;
}

static int pmctrack_cpu_down_prep(unsigned int cpu)
{
	return 0;
}
#endif


/* Stop PMUs and free up resources allocated by init_pmu() */
int pmu_shutdown(void)
{
	get_online_cpus();
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
	unregister_cpu_notifier(&pmctrack_cpu_nb);
#else
	cpuhp_remove_state(pcap_pmctrack_online);
#endif
	put_online_cpus();
	smp_mb();
	return 0;
}


/* Initialize the PMUs of the various CPUs in the system  */
#if defined(__aarch64__) || defined (__arm__)
int init_pmu(void)
{
	int ret=0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
	register_cpu_notifier(&pmctrack_cpu_nb);
	ret=0;
#else
	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "pmctrack/cpu:online",
	                        pmctrack_cpu_online, pmctrack_cpu_down_prep);
	if (ret < 0)
		return -ENOTSUPP;

	pcap_pmctrack_online = ret;
#endif

	init_pmu_props();
	return 0;
}
#else
int init_pmu(void)
{
	//int dev=0;
	int ret=0;
	int vendor=boot_cpu_data.x86_vendor;

	if (!(vendor==X86_VENDOR_AMD || vendor==X86_VENDOR_INTEL))
		return -ENOTSUPP;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
	register_cpu_notifier(&pmctrack_cpu_nb);
	ret=0;
#else
	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "pmctrack/cpu:online",
	                        pmctrack_cpu_online, pmctrack_cpu_down_prep);
	if (ret < 0)
		return -ENOTSUPP;

	pcap_pmctrack_online = ret;
#endif

	init_pmu_props();

	return 0;
}
#endif

