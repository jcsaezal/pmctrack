/*
 *  pmu_config_phi.c
 *
 *  Configuration code for Performance Monitoring Units (PMUs) on
 * 	the Intel Xeon Phi Coprocessor
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */
/*
 * Part of this code is based on oprofile's kernel module.
 */

#include <pmc/pmu_config.h>
#include <asm/nmi.h>
#include <asm/apic.h>
#include <asm/processor.h>
#include <linux/printk.h>
#include <linux/cpu.h>

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

/* Initialize the PMU of the current CPU  */
static void init_pmu_props_cpu(void* dummy)
{
	int this_cpu=smp_processor_id();
	int i=0;
	pmu_props_t* props=&pmu_props_cpu[this_cpu];
	u64 val;

	props->nr_fixed_pmcs=0;
	props->nr_gp_pmcs=2;
	props->pmc_width=40;
	strcpy(props->arch_string,"x86_intel.xeon-phi");

	/* Mask */
	props->pmc_width_mask=0;
	for (i=0; i<props->pmc_width; i++)
		props->pmc_width_mask|=(1ULL<<i);

	coretype_cpu_static[this_cpu]=0;
	props->coretype=0; /* For now we do nothing fancy here*/
	/* Reset this core's counters and msrs ... */
	/* Globally enable counters !! */
	rdmsrl(MSR_KNC_IA32_PERF_GLOBAL_CTRL, val);
	val |= (KNC_ENABLE_COUNTER0|KNC_ENABLE_COUNTER1);
	wrmsrl(MSR_KNC_IA32_PERF_GLOBAL_CTRL, val);
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
	static pmu_flag_t pmu_flags[]= {
		{"pmc",8,0},
		{"usr",1,0},
		{"os",1,0},
		{"umask",8,0},
		{"cmask",8,0},
		{"edge",1,0},
		{"inv",1,0},
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
#if defined(SCHED_AMP)
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
void do_setup_pmcs(pmc_usrcfg_t* pmc_cfg, int used_pmcs_msk,core_experiment_t* exp,int cpu, int exp_idx)
{
	int i;
	int gppmc_id=0;
	low_level_exp* lle;
	pmu_props_t* props_cpu=get_pmu_props_cpu(cpu);
	uint64_t reset_value=0;
	simple_exp* se;
	evtsel_msr evtsel;

	init_core_experiment_t(exp, exp_idx);
	/* Set mask of used pmcs !! */
	exp->used_pmcs=used_pmcs_msk;

	for (i=0; i<MAX_LL_EXPS; i++) {
		/* PMC is used */
		if ( used_pmcs_msk & (0x1<<i)) {
			/* Clear reset value */
			reset_value=0;

			init_evtsel_msr(&evtsel);
			gppmc_id=i-props_cpu->nr_fixed_pmcs;

			set_bit_field(&evtsel.m_evtsel, pmc_cfg[i].cfg_evtsel);
			set_bit_field(&evtsel.m_umask, pmc_cfg[i].cfg_umask);
			set_bit_field(&evtsel.m_usr, pmc_cfg[i].cfg_usr);
			set_bit_field(&evtsel.m_os,pmc_cfg[i].cfg_os);
			set_bit_field(&evtsel.m_e,pmc_cfg[i].cfg_edge);
			set_bit_field(&evtsel.m_en,1);
			set_bit_field(&evtsel.m_inv, pmc_cfg[i].cfg_inv);
			set_bit_field(&evtsel.m_cmask, pmc_cfg[i].cfg_cmask);
			set_bit_field(&evtsel.m_int, 1);

			/* Set up int just in case */
			if (pmc_cfg[i].cfg_ebs_mode) {
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

			/* Add metainfo in the core experiment */
			exp->log_to_phys[exp->size-1]=i;
			exp->phys_to_log[i]=exp->size-1;
		}
	}
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

		} else if ((read_tokens=sscanf(flag,"umask%i=%x", &idx, &val))==2 && (idx>=0 && idx<MAX_LL_EXPS)) {
			pmc_cfg[idx].cfg_umask=val;
		} else if ((read_tokens=sscanf(flag,"cmask%i=%x", &idx, &val))==2 && (idx>=0 && idx<MAX_LL_EXPS)) {
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
	_pmc_t pmc;

	for (i=0; i<props_cpu->nr_gp_pmcs; i++,cnt++) {
		msr.address=EVTSEL_MSR_INITIAL_ADDRESS+i;
		msr.reset_value=0;
		pmc.pmc_address=i;
		pmc.msr.address=PMC_MSR_INITIAL_ADDRESS+i;
		pmc.msr.reset_value=0;
		resetMSR(&msr);
		resetMSR(&pmc.msr);
	}
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


	for (i=0; i<props_cpu->nr_gp_pmcs; i++,cnt++) {
		msr.address=EVTSEL_MSR_INITIAL_ADDRESS+i;
		pmc.pmc_address=i;
		pmc.msr.address=PMC_MSR_INITIAL_ADDRESS+i;
		readMSR(&msr);
		readPMC(&pmc);
		dst+=sprintf(dst,"evtsel%i=0x%016llX\npmc%i=0x%016llX\n",cnt,msr.new_value,cnt,pmc.msr.new_value);
	}

	return dst-line_out;
}

#endif


#define V_2639

#if defined(V_2639)
#include <asm/nmi.h>
#include <asm/kdebug.h>
#include <linux/kdebug.h>

/* Invoked when a PMC overflows */
int pmc_do_nmi_counter_overflow(struct notifier_block *self,
                                unsigned long val, void *data)
{
	struct die_args *args = (struct die_args *)data;
	int ret = NOTIFY_DONE;
	unsigned int overflow_mask=0;

	switch (val) {
	case DIE_NMI:
		if (!nmi_enabled)
			break;

		overflow_mask=read_overflow_mask();

		/* Process Counter overflow */
		if (overflow_mask)
			do_count_on_overflow(args->regs,overflow_mask);

		/* Write apic again */
		apic_write(APIC_LVTPC, APIC_DM_NMI);
		ret = NOTIFY_STOP;
		break;
	default:
		break;
	}

	return ret;
}

static struct notifier_block pmctrack_exceptions_nb = {
	.notifier_call = pmc_do_nmi_counter_overflow,
	.next = NULL,
	.priority = NMI_LOCAL_LOW_PRIOR,
};

#else

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

	/* Write apic again */
	apic_write(APIC_LVTPC, APIC_DM_NMI);
	return 1; /* Must return something positive or it won't work */
}

#endif

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
	.notifier_call = pmctrack_cpu_notifier
};

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

	nmi_enabled = 0;
	ctr_running = 0;
	/* make variables visible to the nmi handler: */
	smp_mb();

	if ((err = pmc_fill_in_addresses())) {
		printk("Can't fill in pmc addresses\n");
		goto out_pmcs_nmi_setup;
	}

#if defined(V_2639)
	if ((err = register_die_notifier(&pmctrack_exceptions_nb))) {
		/* Undo stuff */
		pmc_unfill_addresses(-1);
		goto out_pmcs_nmi_setup;
	}
#else
	register_nmi_handler(NMI_LOCAL, pmc_do_nmi_counter_overflow, 0, "PMI_PMCTRACK");
#endif
	get_online_cpus();
	register_cpu_notifier(&pmctrack_cpu_nb);
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
 * Perform the necessary steps to stop receiving interrupts
 * upon PMC overflow on the current CPU
 */
static void pmc_nmi_cpu_shutdown(void *dummy)
{
	unsigned int v;
	int cpu = smp_processor_id();
	//struct op_msrs *msrs = &per_cpu(cpu_msrs, cpu);

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

/*
 * Disable both performance counters using
 * the global control register
 */
static void disable_counters(void* dummy)
{
	u64 val;
	rdmsrl(MSR_KNC_IA32_PERF_GLOBAL_CTRL, val);
	val &= ~(KNC_ENABLE_COUNTER0|KNC_ENABLE_COUNTER1);
	wrmsrl(MSR_KNC_IA32_PERF_GLOBAL_CTRL, val);
}

/* Stop PMUs and free up resources allocated by init_pmu() */
int pmu_shutdown(void)
{
	get_online_cpus();
	unregister_cpu_notifier(&pmctrack_cpu_nb);
	on_each_cpu(pmc_nmi_cpu_shutdown, NULL, 1);
	on_each_cpu(disable_counters, NULL, 1);
	nmi_enabled = 0;
	ctr_running = 0;
	put_online_cpus();
	smp_mb();

#if defined(V_2639)
	unregister_die_notifier(&pmctrack_exceptions_nb);
#else
	unregister_nmi_handler(NMI_LOCAL, "PMI_PMCTRACK");
#endif
	pmc_unfill_addresses(-1); // Release pmcs!!
	return 0;
}

/* Initialize the PMUs of the various CPUs in the system  */
int init_pmu(void)
{
	int dev=0;

	if (boot_cpu_data.x86_vendor!=X86_VENDOR_INTEL)
		return -ENOTSUPP;

	init_pmu_props();

	if((dev = pmc_nmi_setup()) != 0) {
		printk("Error in pmc_nmi_setup()");
		return dev;
	}

	return 0;
}

/* Reset the PMC overflow register */
void reset_overflow_status(void)
{
	_msr_t status;
	_msr_t overflow;

	status.address=MSR_KNC_IA32_PERF_GLOBAL_STATUS;
	overflow.address=MSR_KNC_IA32_PERF_GLOBAL_OVF_CONTROL;

	readMSR(&status);
	overflow.reset_value=status.new_value;
	resetMSR(&overflow);
}

/*
 * Return a bitmask specifiying which PMCs overflowed */
unsigned int read_overflow_mask(void)
{
	_msr_t status;
	status.address=MSR_KNC_IA32_PERF_GLOBAL_STATUS;
	readMSR(&status);
	return status.new_value;
}
