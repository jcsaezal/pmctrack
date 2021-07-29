/*
 *  pmu_config_x86.c
 *
 *  Configuration code for Performance Monitoring Units (PMUs) on
 * 	general-purpose x86 processors
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */
/*
 * Part of this code is based on oprofile's kernel module.
 */


#include <pmc/pmu_config.h>
#include <pmc/mc_experiments.h>
#include <asm/nmi.h>
#include <asm/apic.h>
#include <asm/processor.h>
#include <linux/printk.h>
#include <linux/cpu.h>
#include <linux/ftrace.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
#include <linux/cpuhotplug.h>
#endif

/** Variables taken from oprofile kernel module */
static DEFINE_PER_CPU(unsigned long, saved_lvtpc);
static int nmi_enabled;  /* Interrupts enabled */
static int ctr_running;  /* Counters configured */

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
#if defined(CONFIG_PMC_CORE_2_DUO) || defined(CONFIG_PMC_CORE_I7)

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

/* Initialize PMU properties of the current CPU  */
static void init_pmu_props_cpu(void* dummy)
{
	int this_cpu=smp_processor_id();
	int i=0;
	pmu_props_t* props=&pmu_props_cpu[this_cpu];

#if defined(CONFIG_PMC_CORE_2_DUO) || defined(CONFIG_PMC_CORE_I7)
	cpuid_regs_t rv;
	int nr_gp_pmcs;
	unsigned int model=0;

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
#elif defined(CONFIG_PMC_AMD)
	props->nr_fixed_pmcs=0;
	props->nr_gp_pmcs=4;
	props->pmc_width=48; /* 48-bit wide */
	/* Forced for now */
	strcpy(props->arch_string,"x86_amd.opteron");
#endif
	/* Mask */
	props->pmc_width_mask=0;
	for (i=0; i<props->pmc_width; i++)
		props->pmc_width_mask|=(1ULL<<i);

	coretype_cpu_static[this_cpu]=0;
	props->coretype=0; /* For now we do nothing fancy here*/
	/* Reset this core's counters and msrs ... */
	mc_clear_all_platform_counters(props);
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
#ifdef CONFIG_PMC_AMD
		{"pmc",12,0},
#else
		{"pmc",8,0},
#endif
		{"usr",1,0},
		{"os",1,0},
		{"umask",8,0},
		{"cmask",8,0},
		{"edge",1,0},
		{"inv",1,0},
		{"any",1,0},
		{"ebs",32,0},
		{"coretype",1,1},
		{NULL,0,0}
	};

#if !defined(SCHED_AMP) && !defined(PMCTRACK_QUICKIA)
#define MAX_CORETYPES 2
	int processor_model[MAX_CORETYPES];
#endif
	on_each_cpu(init_pmu_props_cpu, NULL, 1);

	for (cpu=0; cpu<nr_cpu_ids; cpu++) {
		pmu_props_cpu[cpu].coretype=get_coretype_cpu(cpu);
	}

#ifdef DEBUG
	for (cpu=0; cpu<num_present_cpus(); cpu++)
		print_pmu_info_cpu(cpu);
#endif

	/* The topology is already known */
#if defined(SCHED_AMP) || defined(PMCTRACK_QUICKIA)
	nr_core_types=get_nr_coretypes();

	/* Set coretype per CPU */
	for (cpu=0; cpu<num_present_cpus(); cpu++) {
		pmu_props_cpu[cpu].coretype=get_coretype_cpu(cpu);
	}
#else
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

#endif

	printk("*** PMU Info ***\n");
	printk("Number of core types detected:: %d\n",nr_core_types);

	for (coretype=0; coretype<nr_core_types; coretype++) {
		cpu=get_any_cpu_coretype(coretype);
#ifdef SCHED_AMP //HACK	    
		if (cpu==-1)
			cpu=get_any_cpu_coretype(AMP_SLOW_CORE); /* Clone slow core information */
#endif
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

/*
 * Transform an array of platform-agnostic PMC counter configurations (pmc_cfg)
 * into a low level structure that holds the necessary data to configure hardware counters.
 */
int do_setup_pmcs(pmc_usrcfg_t* pmc_cfg, int used_pmcs_msk,core_experiment_t* exp, int cpu, int exp_idx, struct task_struct* p)
{
	int i;
	int gppmc_id=0;
	low_level_exp* lle;
	pmu_props_t* props_cpu=get_pmu_props_cpu(cpu);
	uint64_t reset_value=0;

#ifndef CONFIG_PMC_AMD
	struct hw_event* llex;
	int nr_fixed_pmcs_used=0;
	msr_perf_fixed_ctr_ctrl fixed_ctrl;
	fixed_count_exp *fce;

	/* Configure fixed function counter here */
	init_msr_perf_fixed_ctr_ctrl(&fixed_ctrl);
#endif
	init_core_experiment_t(exp, exp_idx);
	/* Set mask of used pmcs !! */
	exp->used_pmcs=used_pmcs_msk;

	for (i=0; i<MAX_LL_EXPS; i++) {
		/* PMC is used */
		if ( used_pmcs_msk & (0x1<<i)) {
			/* Clear reset value */
			reset_value=0;
#ifndef CONFIG_PMC_AMD
			/* Determine if this is a fixed function counter or not */
			if (props_cpu->nr_fixed_pmcs>0 && i<props_cpu->nr_fixed_pmcs) {
				int fixed_counter_id=i;
				/* Enable user-mode count for all the available fixed-function PMCs*/

				/* Setup enable 2=user 1=os 3=all !! */
				set_bit_field(&fixed_ctrl.m_enable[fixed_counter_id],(pmc_cfg[i].cfg_usr << 1)|(pmc_cfg[i].cfg_os));

				/* Set up int just in case */
				if (pmc_cfg[i].cfg_ebs_mode) {
					/* Force to user mode in this case */
					set_bit_field(&fixed_ctrl.m_enable[fixed_counter_id],(pmc_cfg[i].cfg_usr << 1));
					set_bit_field(&fixed_ctrl.m_pmi[fixed_counter_id],1);
					reset_value= ((-pmc_cfg[i].cfg_reset_value) & props_cpu->pmc_width_mask);
					/* Set EBS idx */
					exp->ebs_idx=exp->size;
				}

				lle=&exp->array[exp->size++];

				init_low_level_exp_id(lle,"fixed-count",i);
				llex=&lle->event;
				init_hw_event(llex,_FIXED);

				fce=&llex->g_event.f_exp;
				init_fixed_count_exp(fce,MSR_PERF_FIXED_CTR0+fixed_counter_id,fixed_ctrl.m_value,reset_value);

				nr_fixed_pmcs_used++;
			} else
#endif
			{
				simple_exp* se;
				evtsel_msr evtsel;
				init_evtsel_msr(&evtsel);
				gppmc_id=i-props_cpu->nr_fixed_pmcs;

#ifndef	CONFIG_PMC_AMD
				set_bit_field(&evtsel.m_evtsel, pmc_cfg[i].cfg_evtsel);
				/*
				 * In modern Intel CPUs with more than 4 GP-PMCs the 'os' flag must be
				 * enable forcefully for GP-PMCs whose ID ranges between 4 and 7. Otherwise
				 * these performance counters will always display a 0 count.
				 * Note that this is necessary due to a hardware issue.
				 */
				if (gppmc_id>=4 && gppmc_id<=7)
					pmc_cfg[i].cfg_os=1;
#else
				set_bit_field(&evtsel.m_evtsel, pmc_cfg[i].cfg_evtsel);
				set_bit_field(&evtsel.m_evtsel2,(pmc_cfg[i].cfg_evtsel >>8));
#endif
				set_bit_field(&evtsel.m_umask, pmc_cfg[i].cfg_umask);
				set_bit_field(&evtsel.m_usr, pmc_cfg[i].cfg_usr);
				set_bit_field(&evtsel.m_os,pmc_cfg[i].cfg_os);
				set_bit_field(&evtsel.m_e,pmc_cfg[i].cfg_edge);
				set_bit_field(&evtsel.m_en,1);
				set_bit_field(&evtsel.m_inv, pmc_cfg[i].cfg_inv);
				set_bit_field(&evtsel.m_cmask, pmc_cfg[i].cfg_cmask);
#ifndef	CONFIG_PMC_AMD
				set_bit_field(&evtsel.m_int, 1);
				set_bit_field(&evtsel.m_any, pmc_cfg[i].cfg_any);
#endif
				/* Set up int just in case */
				if (pmc_cfg[i].cfg_ebs_mode) {
#ifdef	CONFIG_PMC_AMD
					set_bit_field(&evtsel.m_int, 1);
#endif
					set_bit_field(&evtsel.m_os, 0);	/* Force os flag to zero in this case */
					reset_value= ((-pmc_cfg[i].cfg_reset_value) & props_cpu->pmc_width_mask);
					/* Set EBS idx */
					exp->ebs_idx=exp->size;
				}

				/* HW configuration ready!! */
				lle=&exp->array[exp->size++];

				init_low_level_exp_id(lle,"gp-pmc",i);
				init_hw_event(&lle->event,_SIMPLE);
				se=&lle->event.g_event.s_exp;

				init_simple_exp (se, 				/* Simple exp */
				                 PMC_MSR_INITIAL_ADDRESS+gppmc_id, 	/* MSR address of the PMC */
				                 gppmc_id, 				/* Assigned PMC ID */
				                 EVTSEL_MSR_INITIAL_ADDRESS+gppmc_id,	/* MSR address of the associated evtsel */
				                 evtsel.m_value,
				                 reset_value
				                );
			}

			/* Add metainfo in the core experiment */
			exp->log_to_phys[exp->size-1]=i;
			exp->phys_to_log[i]=exp->size-1;
		}
	}
#ifndef CONFIG_PMC_AMD
	/* Make sure that all fixed-count events have the same value for the ctrl_msr
		(the last one was configured okay but the others did not) */
	for (i=1; i<nr_fixed_pmcs_used; i++) {
		lle=&exp->array[exp->size-(i+1)];
		fce=&lle->event.g_event.f_exp;
		fce->evtsel.new_value=fixed_ctrl.m_value;
	}
#endif
	return 0;
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
		if((read_tokens=sscanf(flag,"pmc%i=%x", &idx, &val))>0) {
			if ((idx<0 || idx>=MAX_LL_EXPS)) {
				error=1;
				break;
			}
			used_pmcs|=(0x1<<idx);
			pmc_count++;
			/* By default enable count in user mode */
			pmc_cfg[idx].cfg_usr=1;

			/* Write evtsel too */
			if (read_tokens==2) {
				pmc_cfg[idx].cfg_evtsel=val;
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
		(*coretype)=coretype_selected;
		return 0;
	}
}

/*
 * Perform a default initialization of all performance monitoring counters
 * in the current CPU.
 */
void mc_clear_all_platform_counters(pmu_props_t* props_cpu)
{
	int i;
	_msr_t msr;
	int cnt=0;
	uint64_t  gp_counter_mask=0;
#ifndef CONFIG_PMC_AMD
	_msr_t fixed_count_msr;
#endif
	_pmc_t pmc;


	for (i=0; i<props_cpu->nr_gp_pmcs; i++,cnt++) {
		msr.address=EVTSEL_MSR_INITIAL_ADDRESS+i;
		msr.reset_value=0;
		pmc.pmc_address=i;
		pmc.msr.address=PMC_MSR_INITIAL_ADDRESS+i;
		pmc.msr.reset_value=0;
		resetMSR(&msr);
		resetMSR(&pmc.msr);
		gp_counter_mask|=1<<i;
	}


#ifndef CONFIG_PMC_AMD
	for (i=0; i<props_cpu->nr_fixed_pmcs; i++,cnt++) {
		msr.address=MSR_PERF_FIXED_CTR_CTRL;
		msr.reset_value=0;
		fixed_count_msr.address=MSR_PERF_FIXED_CTR0+i;
		fixed_count_msr.reset_value=0;

		resetMSR(&msr);
		resetMSR(&fixed_count_msr);
	}

	/* THINK ABOUT THIS!!
			msr.address=MSR_PERF_GLOBAL_OVF_CTRL;
			msr.reset_value=0;
			resetMSR(&msr);
	*/
	msr.address=MSR_PERF_GLOBAL_CTRL;
	msr.reset_value=(gp_counter_mask) | (0x7ULL << 32ULL) ;
	resetMSR(&msr);

#endif

}

/*
 * Generate a summary string with the configuration of a hardware counter (lle)
 * in human-readable format. The string is stored in buf.
 */
int print_pmc_config(low_level_exp* lle, char* buf)
{
	struct hw_event* event=&lle->event;
	_msr_t* msrdesc=NULL;
	_msr_t* msrpmc=NULL;

	switch ( event->type ) {
	case _SIMPLE:
		msrdesc=& ( event->g_event.s_exp.evtsel);
		msrpmc=& ( event->g_event.s_exp.pmc.msr);
		break;
#ifndef CONFIG_PMC_AMD
	case _FIXED:
		msrdesc=& ( event->g_event.f_exp.evtsel);
		msrpmc=& ( event->g_event.f_exp.pmc);
		break;
#endif
	default:
		break;
	}

	if (msrdesc) {
		return sprintf(buf,"evtsel%d=0x%012llX\npmcReset=0x%012llX\n",lle->pmc_id,msrdesc->new_value,msrpmc->reset_value);
	} else {
		return 0;
	}

}


#ifdef DEBUG
int print_pmu_msr_values_debug(char* line_out)
{
	int i;
	pmu_props_t* props_cpu=get_pmu_props_cpu(0);
	char* dst=line_out;
	_msr_t msr;
	int cnt=0;
#ifndef CONFIG_PMC_AMD
	_msr_t fixed_count_msr;
#endif
	_pmc_t pmc;



#ifndef CONFIG_PMC_AMD
	for (i=0; i<props_cpu->nr_fixed_pmcs; i++,cnt++) {
		msr.address=MSR_PERF_FIXED_CTR_CTRL;
		fixed_count_msr.address=MSR_PERF_FIXED_CTR0+i;
		readMSR(&msr);
		readMSR(&fixed_count_msr);
		trace_printk("evtsel%i=0x%016llX\npmc%i=0x%016llX\n",cnt,msr.new_value,cnt,fixed_count_msr.new_value);
	}
#endif

	for (i=0; i<props_cpu->nr_gp_pmcs; i++,cnt++) {
		msr.address=EVTSEL_MSR_INITIAL_ADDRESS+i;
		pmc.pmc_address=i;
		pmc.msr.address=PMC_MSR_INITIAL_ADDRESS+i;
		readMSR(&msr);
		readPMC(&pmc);
		trace_printk("evtsel%i=0x%016llX\npmc%i=0x%016llX\n",cnt,msr.new_value,cnt,pmc.msr.new_value);
	}

#ifndef CONFIG_PMC_AMD
	msr.address=MSR_PERF_GLOBAL_STATUS;
	readMSR(&msr);
	trace_printk("MSR_PERF_GLOBAL_STATUS=0x%016llX\n",msr.new_value);

	msr.address=MSR_PERF_GLOBAL_CTRL;
	readMSR(&msr);
	trace_printk("MSR_PERF_GLOBAL_CTRL=0x%016llX\n",msr.new_value);

	msr.address=MSR_PERF_GLOBAL_OVF_CTRL;
	readMSR(&msr);
	trace_printk("MSR_PERF_GLOBAL_OVF_CTRL=0x%016llX\n",msr.new_value);
#endif
	dst+=sprintf(dst,"See ftrace output. Using trace_printk()\n");
	return dst-line_out;
}

#endif


/*
 * Invoked when a PMC overflows
 * (On success return a positive value)
 */
static int pmc_do_nmi_counter_overflow(unsigned int cmd, struct pt_regs *regs)
{

	unsigned int overflow_mask=read_overflow_mask();

	/* Process Counter overflow */
	if (overflow_mask)
		do_count_on_overflow(regs,overflow_mask);


	reset_overflow_status();
	/* Write apic again */
	apic_write(APIC_LVTPC, APIC_DM_NMI);
	return 1;
}

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

/* Enable the PERF interrupt on this CPU */
static void pmc_nmi_cpu_setup(void *dummy)
{
	int cpu = smp_processor_id();
	per_cpu(saved_lvtpc, cpu) = apic_read(APIC_LVTPC);
	apic_write(APIC_LVTPC, APIC_DM_NMI);
}

static void pmc_unfill_addresses(int nr_counts) { }
static int pmc_fill_in_addresses(void)
{
	return 0;
}





/*
 * Prepare a CPU to handle interrupts on PMC overflow
 * (Function based on nmi_setup() code in the Linux kernel
 * -  arch/oprofile/nmi_int.c )
 */
static int pmc_nmi_setup(void)
{
	int err=0;
	int ret;
	nmi_enabled = 0;
	ctr_running = 0;
	/* make variables visible to the nmi handler: */
	smp_mb();

	if ((err = pmc_fill_in_addresses())) {
		printk("Can't fill in pmc addresses\n");
		goto out_pmcs_nmi_setup;
	}

#ifdef V_2639
	if ((err = register_die_notifier(&pmctrack_exceptions_nb))) {
		/* Undo stuff */
		pmc_unfill_addresses(-1);
		goto out_pmcs_nmi_setup;
	}
#else
	register_nmi_handler(NMI_LOCAL, pmc_do_nmi_counter_overflow, 0, "PMI_PMCTRACK");
#endif
	get_online_cpus();

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
	register_cpu_notifier(&pmctrack_cpu_nb);
	ret=0;
#else
	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "pmctrack/cpu:online",
	                        pmctrack_cpu_online, pmctrack_cpu_down_prep);
	if (ret < 0)
		goto out_pmcs_nmi_setup;
	pcap_pmctrack_online = ret;
#endif
	nmi_enabled = 1;
	/* make nmi_enabled visible to the nmi handler: */
	smp_mb();
	on_each_cpu(pmc_nmi_cpu_setup, NULL, 1);
	put_online_cpus();

	reset_overflow_status();

out_pmcs_nmi_setup:
	return err;
}

/*
 * Perform the necessary steps to stop receiving interrupts on PMC overflow
 * on the current CPU
 */
static void pmc_nmi_cpu_shutdown(void *dummy)
{
	unsigned int v;
	int cpu = smp_processor_id();

	/* restoring APIC_LVTPC can trigger an apic error because the delivery
	  * mode and vector nr combination can be illegal. That's by design: on
	  * power on apic lvt contain a zero vector nr which are legal only for
	  * NMI delivery mode. So inhibit apic err before restoring lvtpc
	*/
	v = apic_read(APIC_LVTERR);
	apic_write(APIC_LVTERR, v | APIC_LVT_MASKED);
	apic_write(APIC_LVTPC, per_cpu(saved_lvtpc, cpu));
	apic_write(APIC_LVTERR, v);
	/* nmi_cpu_restore_registers(msrs);
	if (model->cpu_down)
		model->cpu_down();
	*/
}

/* Stop PMUs and free up resources allocated by init_pmu() */
int pmu_shutdown(void)
{
	get_online_cpus();
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
	unregister_cpu_notifier(&pmctrack_cpu_nb);
#else
	cpuhp_remove_state(pcap_pmctrack_online);
#endif
	on_each_cpu(pmc_nmi_cpu_shutdown, NULL, 1);
	nmi_enabled = 0;
	ctr_running = 0;
	put_online_cpus();
	smp_mb();
#ifdef V_2639
	unregister_die_notifier(&pmctrack_exceptions_nb);
#else
	unregister_nmi_handler(NMI_LOCAL, "PMI_PMCTRACK");
#endif
	pmc_unfill_addresses(-1); // Return pmcs!!
	return 0;
}

/* Initialize the PMUs of the various CPUs in the system  */
int init_pmu(void)
{
	int dev=0;
#ifdef CONFIG_PMC_AMD
	if (boot_cpu_data.x86_vendor!=X86_VENDOR_AMD)
		return -ENOTSUPP;
#else
	if (boot_cpu_data.x86_vendor!=X86_VENDOR_INTEL)
		return -ENOTSUPP;
#endif

	init_pmu_props();


	if((dev = pmc_nmi_setup()) != 0) {
		printk("Error in pmc_nmi_setup()");
		return dev;
	}

	return 0;
}

#ifdef CONFIG_PMC_AMD
/* No overflow register in AMD processors */
void reset_overflow_status(void) {}
unsigned int read_overflow_mask(void)
{
	return 0x1;
}
#else
/* Reset the PMC overflow register */
void reset_overflow_status(void)
{
	_msr_t status;
	_msr_t overflow;


	status.address=MSR_PERF_GLOBAL_STATUS;
	overflow.address=MSR_PERF_GLOBAL_OVF_CTRL;

	readMSR(&status);
	overflow.reset_value=status.new_value;
	resetMSR(&overflow);

}

#define FIXED_COUNTERS_STARTING_OVF_BIT 32
#define GP_COUNTERS_STARTING_OVF_BIT 0

/*
 * Return a bitmask specifiying which PMCs overflowed
 * This bitmask must be in a platform-independent "normalized" format.
 * bit(i,mask)==1 if pmc_i overflowed
 * (bear in mind that lower PMC ids are reserved for
 * fixed-function PMCs)
 */
unsigned int read_overflow_mask(void)
{
	_msr_t status;
	unsigned int mask=0;
	pmu_props_t* props=get_pmu_props_cpu(smp_processor_id());
	int idx,i;
	uint64_t pmcn=0;

	status.address=MSR_PERF_GLOBAL_STATUS;
	readMSR(&status);

	for (i=0,idx=FIXED_COUNTERS_STARTING_OVF_BIT; i<props->nr_fixed_pmcs; i++,idx++,pmcn++)
		mask|=((status.new_value>>idx)&0x1ULL)<<pmcn;

	for (i=0,idx=GP_COUNTERS_STARTING_OVF_BIT; i<props->nr_gp_pmcs; i++,idx++,pmcn++)
		mask|=((status.new_value>>idx)&0x1ULL)<<pmcn;

	return mask;
}
#endif

