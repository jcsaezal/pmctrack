/*
 *  pmu_config_arm.c
 *
 *  Configuration code for Performance Monitoring Units (PMUs) on
 * 	ARM Cortex 32-bit processors
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */
/*
 * The interrupt-handling code found in this file is based
 * heavily on that of the arch/arm/kernel/perf_event_cpu.c file
 * of the vanilla Linux kernel v3.17.3.
 */
#include <pmc/pmu_config.h>
#include <pmc/mc_experiments.h>
#include <linux/irq.h>
#include <linux/printk.h>
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <asm/cputype.h>
#include <asm/cpu.h>
#include <asm/smp_plat.h>
#include <linux/ftrace.h>
#include <linux/version.h>

typedef unsigned int u32;

/* Variables to aid in detecting the various PMUs in the system */
int coretype_cpu_static[NR_CPUS];
int* coretype_cpu=coretype_cpu_static;
int  nr_core_types=1;

/* Global definition of prop coretype */
pmu_props_t pmu_props_cputype[PMC_CORE_TYPES];
pmu_props_t pmu_props_cpu[NR_CPUS];

/*
 * *** IMPORTANT NOTE:***
 * Define the ENABLE_IRQ macro only if using the ARM Coretile
 * Express Board (TC2) featuring an ARM big.LITTLE processor
 * with 3 small cores (Cortex A7) and 2 big cores (Cortex A15)
 * The interrupt lines listed in this source file correspond
 * to those found in this system.
 *
 * If using a different ARM 32-bit system, set the right interrupt
 * lines for that system in the sources or comment the line found below.
 */
#define ENABLE_IRQ
static void unregister_overflow_irq(void);
static int setup_overflow_irq(void);


/* cat /proc/cpuinfo (Cpuparts ...)

processor       : 0
model name      : ARMv7 Processor rev 1 (v7l)
Features        : half thumb fastmult vfp edsp neon vfpv3 tls vfpv4 idiva idivt vfpd32 lpae evtstrm
CPU implementer : 0x41
CPU architecture: 7
CPU variant     : 0x0
CPU part        : 0xc07
CPU revision    : 1

...

processor       : 3
model name      : ARMv7 Processor rev 1 (v7l)
Features        : half thumb fastmult vfp edsp neon vfpv3 tls vfpv4 idiva idivt vfpd32 lpae evtstrm
CPU implementer : 0x41
CPU architecture: 7
CPU variant     : 0x2
CPU part        : 0xc0f
CPU revision    : 1


*/

/*
 * Code to list and detect fully-supported ARM Cortex processors
 */
static struct pmctrack_cpu_model cpu_models[]= {
	{0xc0f,"cortex_a15"},{0xc07,"cortex_a7"},
	{0,NULL} /* Marker */
};

static void fill_model_string(int model, char* model_str)
{
	int i=0;
	while(cpu_models[i].model!=0 && cpu_models[i].model!=model)
		i++;
	if (cpu_models[i].model==0)
		strcpy(model_str,"armv7.unknown");
	else
		sprintf(model_str,"armv7.%s",cpu_models[i].model_str);
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

	props->nr_gp_pmcs=get_bit_field32(&reg.m_n) & ARMV7_PMNC_N_MASK ;
	props->nr_fixed_pmcs=1; //Cycle counter
	props->pmc_width=32;

	/* Mask */
	props->pmc_width_mask=0;
	for (i=0; i<props->pmc_width; i++)
		props->pmc_width_mask|=(1ULL<<i);

	/* Read PMU ID*/
	props->processor_model=reg.m_value;
	cpuid=read_cpuid_id();

	if ((cpuid & 0x0008f000) == 0x00000000) {
		/* pre-ARM7 */
		model=cpuid >> 4;
	} else {
		model=(cpuid >> 4) & 0xfff;
	}
	fill_model_string(model,props->arch_string);
#ifdef DEBUG
	u32 val;
	if (this_cpu==1) {
		printk(KERN_INFO "PMNC registers dump:\n");

		asm volatile("mrc p15, 0, %0, c9, c12, 0" : "=r" (val));
		printk(KERN_INFO "PMNC  =0x%08x\n", val);

		asm volatile("mrc p15, 0, %0, c9, c12, 1" : "=r" (val));
		printk(KERN_INFO "CNTENS=0x%08x\n", val);

		asm volatile("mrc p15, 0, %0, c9, c14, 1" : "=r" (val));
		printk(KERN_INFO "INTENS=0x%08x\n", val);

		asm volatile("mrc p15, 0, %0, c9, c12, 3" : "=r" (val));
		printk(KERN_INFO "FLAGS =0x%08x\n", val);

		asm volatile("mrc p15, 0, %0, c9, c12, 5" : "=r" (val));
		printk(KERN_INFO "SELECT=0x%08x\n", val);

		asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r" (val));
		printk(KERN_INFO "CCNT  =0x%08x\n", val);
	}
#endif
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

/* Initialize the PMU of the current CPU  */
static void cpu_pmu_init(void *dummy)
{
	int cpu = smp_processor_id();
	int idx;
	int nr_counters;
	pmu_props_t* props=NULL;

	props=get_pmu_props_cpu(cpu);
	nr_counters=props->nr_gp_pmcs+props->nr_fixed_pmcs;

	/* The counter and interrupt enable registers are unknown at reset. */
	for (idx = ARMV7_IDX_CYCLE_COUNTER; idx < nr_counters; ++idx) {
		armv7_pmnc_disable_counter(idx);
		armv7_pmnc_disable_intens(idx);
	}

	mc_clear_all_platform_counters(props);

	for (idx = ARMV7_IDX_CYCLE_COUNTER; idx < nr_counters; ++idx) {
		armv7_pmnc_enable_counter(idx);
#ifdef ENABLE_IRQ
		/* For now, disable OS/USR CYCLE COUNTER Overflow */
		if (idx!=ARMV7_IDX_CYCLE_COUNTER)
			armv7_pmnc_enable_intens(idx);
		else
			armv7_pmnc_enable_intens(idx);
#else
		armv7_pmnc_disable_intens(idx);
#endif
	}

	/* Initialize & Reset PMNC: C and P bits ... and enable */
	armv7_pmnc_write(ARMV7_PMNC_P | ARMV7_PMNC_C | ARMV7_PMNC_E);
}

/*
 * Disable performance counters and clear global PMC flags
 * in the current CPU
 */
static void cpu_pmu_shutdown(void *dummy)
{
	int cpu = smp_processor_id();
	int idx;
	int nr_counters;
	pmu_props_t* props=NULL;

	props=get_pmu_props_cpu(cpu);
	nr_counters=props->nr_gp_pmcs+props->nr_fixed_pmcs;

	/* Disable performance counters and overflow interrupt bits */
	for (idx = ARMV7_IDX_CYCLE_COUNTER; idx < nr_counters; ++idx) {
		armv7_pmnc_disable_counter(idx);
		armv7_pmnc_disable_intens(idx);
	}

	/* Clear global enable flags */
	armv7_pmnc_write(armv7_pmnc_read() & ~(ARMV7_PMNC_E|ARMV7_PMNC_P | ARMV7_PMNC_C));
	mc_clear_all_platform_counters(props);
}

/* Initialize the PMUs of the various CPUs in the system  */
int init_pmu(void)
{
	init_pmu_props();

	if (setup_overflow_irq())
		return -EINVAL;

	get_online_cpus();
	on_each_cpu(cpu_pmu_init, NULL, 1);
	put_online_cpus();

	return 0;
}

/* Stop PMUs and free up resources allocated by init_pmu() */
int pmu_shutdown(void)
{
	get_online_cpus();
	on_each_cpu(cpu_pmu_shutdown, NULL, 1);
	put_online_cpus();

	unregister_overflow_irq();

	return 0;
}

/* Reset the PMC overflow register */
void reset_overflow_status(void)
{
	armv7_pmnc_getreset_flags();
}

/* Return a bitmask specifying which PMCs overflowed */
unsigned int read_overflow_mask(void)
{
	return armv7_pmnc_getovf_flags();
}

/*
 *  Interrupt handler to deal with PMC overflow interrupts
 *  (This function is a variant of the armv7pmu_handle_irq()
 * 	 function found in the arch/arm/kernel/perf_event_cpu.c
 *	 file of the Linux kernel)
 */
static irqreturn_t armv7pmu_handle_irq(int irq_num, void *dev)
{
	u32 pmnc;
	struct pt_regs *regs;
	int idx;
	int cpu = smp_processor_id();
	pmu_props_t* props=get_pmu_props_cpu(cpu);
	int nr_counters=props->nr_gp_pmcs+props->nr_fixed_pmcs;
	unsigned int overflow_mask=0x0;

	/*
	 * Get and reset the IRQ flags
	 */
	pmnc = armv7_pmnc_getreset_flags();

	/*
	 * Did an overflow occur?
	 */
	if (!armv7_pmnc_has_overflowed(pmnc)) {
		trace_printk(KERN_ALERT "PMCTRACK in irq %i, cpu %d\n",irq_num,smp_processor_id());
		return IRQ_NONE;
	}

	/*
	 * Handle the counter(s) overflow(s)
	 */
	regs = get_irq_regs();

	for (idx = 0; idx < nr_counters; ++idx) {
		if (!armv7_pmnc_counter_has_overflowed(pmnc, idx))
			continue;

		/* For now, ignore overflows for the cycle counter */
		if (idx!=0) {
			overflow_mask|=1<<idx;
			trace_printk(KERN_ALERT "Counter %d overflowed\n",idx);
		}
	}

	/* Process Counter overflow (Architecture-independent code) */
	if (overflow_mask)
		do_count_on_overflow(regs,overflow_mask);

	return IRQ_HANDLED;
}


/* No support for PMC overflow interrupts */
#ifndef ENABLE_IRQ
static void unregister_overflow_irq(void) { }
static int setup_overflow_irq(void)
{
	return 0;
}
#else

/*
+	arm-pmu {
+			compatible = "arm,cortex-a15-pmu";
+			interrupt-parent = <&combiner>;
+			interrupts = <1 2>, <7 0>, <16 6>, <19 2>;
+
+			compatible = "arm,cortex-a7-pmu";
+			interrupt-parent = <&gic>;
+			interrupts = <0 192 4>, <0 193 4>, <0 194 4>, <0 195 4>;
+
+	};
+
*/

/*cat /proc/interrupts | grep arm-pmu
279:          0          0          0          0          0          0          0          0  COMBINER  10  arm-pmu
280:          0          0          0          0          0          0          0          0  COMBINER  56  arm-pmu
281:          0          0          0          0          0          0          0          0  COMBINER 134  arm-pmu
282:          0          0          0      36213          0          0          0          0  COMBINER 154  arm-pmu
*/
#ifdef ODROID
#define VEXPRESS_NR_IRQS 8
/* Interrupt lines for the odroid-xu3 board */
int vexpress_fixed_irqs[VEXPRESS_NR_IRQS]= {
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,14,0)
	192,193,194,195,266,312,390,410
#elif LINUX_VERSION_CODE <= KERNEL_VERSION(4,2,0)
	192,193,194,195,279,280,281,282
#elif LINUX_VERSION_CODE <= KERNEL_VERSION(4,14,0)
	192,193,194,195,279,280,281,282
	/*	192,134,135,136,137,193,194,195 */
#elif LINUX_VERSION_CODE <= KERNEL_VERSION(5,5,0)
	152,153,154,155,156,157,158,159
	/*	192,134,135,136,137,193,194,195 */
#else
	69,70,71,72,73,74,75,76
#endif
};
#else
/* Interrupt lines for the ARM Coretile Express TC2 board */
#define VEXPRESS_NR_IRQS 5
int vexpress_fixed_irqs[VEXPRESS_NR_IRQS]= {160,161,162,100,101};
#endif
cpumask_t pmu_active_irqs;

/*
 *  Install interrupt handlers to react to PMC overflow interrupts.
 *  (This function is a variant of the cpu_pmu_request_irq()
 * 	 function found in the arch/arm/kernel/perf_event_cpu.c
 *	 file of the Linux kernel)
 */
static int setup_overflow_irq(void)
{
	int i, err, irq, irqs;
	unsigned long flags=0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,9,0)
	flags=IRQF_NO_THREAD | IRQF_PERCPU | IRQF_NOBALANCING;
#else
	flags=IRQF_DISABLED | IRQF_NOBALANCING;
#endif

	cpumask_clear(&pmu_active_irqs);

	irqs = VEXPRESS_NR_IRQS;

	for (i = 0; i < irqs; ++i) {
		err = 0;

		irq=vexpress_fixed_irqs[i];
		/*
		 * If we have a single PMU interrupt that we can't shift,
		  * assume that we're running on a uniprocessor machine and
		  * continue. Otherwise, continue without this interrupt.
		  */
		if (irq_set_affinity(irq, cpumask_of(i)) && irqs > 1) {
			pr_warning("unable to set irq affinity (irq=%d, cpu=%u)\n",
			           irq, i);
			continue;
		}
		err = request_irq(irq, armv7pmu_handle_irq,
		                  flags, "pmctrack-arm-pmu",
		                  &pmu_props_cpu[0]);
		if (err) {
			pr_err("unable to request IRQ%d for ARM PMU counters\n",irq);
			unregister_overflow_irq();
			return err;
		}

		cpumask_set_cpu(i, &pmu_active_irqs);
	}

	return 0;
}

/*
 *  Remove interrupt handlers for PMC overflow interrupts.
 *  (This function is a variant of the cpu_pmu_free_irq()
 * 	 function found in the arch/arm/kernel/perf_event_cpu.c
 *	 file of the Linux kernel)
 */
static void unregister_overflow_irq(void)
{
	int i, irq, irqs;

	irqs = VEXPRESS_NR_IRQS;

	for (i = 0; i < irqs; ++i) {
		if (!cpumask_test_and_clear_cpu(i, &pmu_active_irqs))
			continue;

		irq = vexpress_fixed_irqs[i];
		free_irq(irq,&pmu_props_cpu[0]);
	}
}

#endif

/*
 * Transform an array of platform-agnostic PMC counter configurations (pmc_cfg)
 * into a low level structure that holds the necessary data to configure hardware counters.
 */
int do_setup_pmcs(pmc_config_set_t* cconfig, int used_pmcs_msk,core_experiment_t* exp, int cpu, int exp_idx, struct task_struct* p)
{
	pmc_usrcfg_t* pmc_cfg=cconfig->pmc_cfg;
	int i;
	low_level_exp* lle;
	pmu_props_t* props_cpu=get_pmu_props_cpu(cpu);
	uint64_t reset_value=0;

	struct hw_event* llex;
	fixed_count_exp *fce;


	init_core_experiment_t(exp, exp_idx);
	/* Set mask of used pmcs !! */
	exp->used_pmcs=used_pmcs_msk;

	for (i=0; i<MAX_LL_EXPS; i++) {
		/* PMC is used */
		if ( used_pmcs_msk & (0x1<<i)) {
			/* Clear reset value */
			reset_value=0;
			// Fixed counter (cycle counter )
			if (i==0) {
				/* Set up int just in case */
				if (pmc_cfg[i].cfg_ebs_mode) {
					reset_value= ((-pmc_cfg[i].cfg_reset_value) & props_cpu->pmc_width_mask);
					/* Set EBS idx */
					exp->ebs_idx=exp->size;
				}

				lle=&exp->array[exp->size++];

				init_low_level_exp_id(lle,"fixed-count",i);
				llex=&lle->event;
				init_hw_event(llex,_FIXED);

				fce=&llex->g_event.f_exp;
				init_fixed_count_exp(fce,i,reset_value);

			} else {

				simple_exp* se;
				pmxevtyper_t evtsel;
				init_pmxevtyper(&evtsel);

				set_bit_field32(&evtsel.m_evt_count, pmc_cfg[i].cfg_evtsel);
				set_bit_field32(&evtsel.m_u, pmc_cfg[i].cfg_usr);
				set_bit_field32(&evtsel.m_p,pmc_cfg[i].cfg_os);
				set_bit_field32(&evtsel.m_nsk,!pmc_cfg[i].cfg_os);
				set_bit_field32(&evtsel.m_nsu,!pmc_cfg[i].cfg_usr);

				/* Set up int just in case */
				if (pmc_cfg[i].cfg_ebs_mode) {
					//set_bit_field(&evtsel.m_int, 1);
					set_bit_field32(&evtsel.m_p, 1);	/* Disable OS in this case */
					reset_value= ((-pmc_cfg[i].cfg_reset_value) & props_cpu->pmc_width_mask);
					/* Set EBS idx */
					exp->ebs_idx=exp->size;
				}

				/* HW configuration ready!! */
				lle=&exp->array[exp->size++];

				init_low_level_exp_id(lle,"gp-pmc",i);
				init_hw_event(&lle->event,_SIMPLE);
				se=&lle->event.g_event.s_exp;

				init_simple_exp (se, 	/* Simple exp */
				                 i, 	/* Assigned PMC ID */
				                 evtsel.m_value,
				                 reset_value
				                );
			}

			/* Add metainfo in the core experiment */
			exp->log_to_phys[exp->size-1]=i;
			exp->phys_to_log[i]=exp->size-1;
		}
	}

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
			pmc_cfg[idx].cfg_usr=0;
			pmc_cfg[idx].cfg_os=1;

			/* Write evtsel too */
			if (read_tokens==2) {
				pmc_cfg[idx].cfg_evtsel=val;
			}
		} else if ((read_tokens=sscanf(flag,"usr%i=%d", &idx, &val))>0 && (idx>=0 && idx<MAX_LL_EXPS)) {
			if (read_tokens==2) {
				if (val==0 || val==1) {
					pmc_cfg[idx].cfg_usr=(0x1&~val);
				} else {
					error=1;
					break;
				}
			} else {
				pmc_cfg[idx].cfg_usr=0;
			}
		} else if ((read_tokens=sscanf(flag,"os%i=%d", &idx, &val))>0 && (idx>=0 && idx<MAX_LL_EXPS)) {
			if (read_tokens==2) {
				if (val==0 || val==1) {
					pmc_cfg[idx].cfg_os=(0x1&~val);
				} else {
					error=1;
					break;
				}
			} else {
				pmc_cfg[idx].cfg_os=0;
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
		printk("Unrecognized format in flag %s\n",flag);
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

	for (i=0; i<props_cpu->nr_gp_pmcs; i++) {
		armv7pmu_write_counter(i+1,0);
		armv7_pmnc_write_evtsel(i+1,EVTSEL_RESET_VALUE);
	}

	/* Clear cycle counter explicitly */
	armv7pmu_write_counter(0,0);

}

//TODO
int print_pmc_config(low_level_exp* lle, char* buf)
{
	return 0;
}

#ifdef DEBUG
int print_pmu_msr_values_debug(char* line_out)
{
	u32 val;
	unsigned int cnt;
	char* dst=line_out;
	int cpu = smp_processor_id();
	pmu_props_t* props=get_pmu_props_cpu(cpu);
	int nr_counters=props->nr_gp_pmcs+props->nr_fixed_pmcs;

	dst+=sprintf(dst,"PMNC registers dump:\n");

	asm volatile("mrc p15, 0, %0, c9, c12, 0" : "=r" (val));
	dst+=sprintf(dst,"PMNC  =0x%08x\n", val);

	asm volatile("mrc p15, 0, %0, c9, c12, 1" : "=r" (val));
	dst+=sprintf(dst,"CNTENS=0x%08x\n", val);

	asm volatile("mrc p15, 0, %0, c9, c14, 1" : "=r" (val));
	dst+=sprintf(dst,"INTENS=0x%08x\n", val);

	asm volatile("mrc p15, 0, %0, c9, c12, 3" : "=r" (val));
	dst+=sprintf(dst,"FLAGS =0x%08x\n", val);

	asm volatile("mrc p15, 0, %0, c9, c12, 5" : "=r" (val));
	dst+=sprintf(dst,"SELECT=0x%08x\n", val);

	asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r" (val));
	dst+=sprintf(dst,"CCNT  =0x%08x\n", val);

	for (cnt = ARMV7_IDX_COUNTER0;
	     cnt < nr_counters; cnt++) {
		armv7_pmnc_select_counter(cnt);
		asm volatile("mrc p15, 0, %0, c9, c13, 2" : "=r" (val));
		dst+=sprintf(dst,"CNT[%d] count =0x%08x\n",
		             ARMV7_IDX_TO_COUNTER(cnt), val);
		asm volatile("mrc p15, 0, %0, c9, c13, 1" : "=r" (val));
		dst+=sprintf(dst,"CNT[%d] evtsel=0x%08x\n",
		             ARMV7_IDX_TO_COUNTER(cnt), val);
	}
	return dst-line_out;
}
#endif


#ifdef DEBUG
static void armv7_pmnc_dump_regs(struct arm_pmu *cpu_pmu)
{
	u32 val;
	unsigned int cnt;
	int cpu = smp_processor_id();
	pmu_props_t* props=get_pmu_props_cpu(cpu);
	int nr_counters=props->nr_gp_pmcs+props->nr_fixed_pmcs;

	printk(KERN_INFO "PMNC registers dump:\n");

	asm volatile("mrc p15, 0, %0, c9, c12, 0" : "=r" (val));
	printk(KERN_INFO "PMNC  =0x%08x\n", val);

	asm volatile("mrc p15, 0, %0, c9, c12, 1" : "=r" (val));
	printk(KERN_INFO "CNTENS=0x%08x\n", val);

	asm volatile("mrc p15, 0, %0, c9, c14, 1" : "=r" (val));
	printk(KERN_INFO "INTENS=0x%08x\n", val);

	asm volatile("mrc p15, 0, %0, c9, c12, 3" : "=r" (val));
	printk(KERN_INFO "FLAGS =0x%08x\n", val);

	asm volatile("mrc p15, 0, %0, c9, c12, 5" : "=r" (val));
	printk(KERN_INFO "SELECT=0x%08x\n", val);

	asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r" (val));
	printk(KERN_INFO "CCNT  =0x%08x\n", val);

	for (cnt = ARMV7_IDX_COUNTER0;
	     cnt < nr_counters; cnt++) {
		armv7_pmnc_select_counter(cnt);
		asm volatile("mrc p15, 0, %0, c9, c13, 2" : "=r" (val));
		printk(KERN_INFO "CNT[%d] count =0x%08x\n",
		       ARMV7_IDX_TO_COUNTER(cnt), val);
		asm volatile("mrc p15, 0, %0, c9, c13, 1" : "=r" (val));
		printk(KERN_INFO "CNT[%d] evtsel=0x%08x\n",
		       ARMV7_IDX_TO_COUNTER(cnt), val);
	}
}
#endif


