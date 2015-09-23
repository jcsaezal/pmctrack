/*
 *  ipc_sampling_sf_mm.c
 *
 *	Determine threads' SFs on AMPs via IPC sampling
 * 
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 * 
 *  This code is licensed under the GNU GPL v2.
 */

#include <pmc/hl_events.h>
#include <pmc/mc_experiments.h>
#include <pmc/monitoring_mod.h>
#include <pmc/pmu_config.h> //For cpuid_regs_t

#ifndef AMP_CORE_TYPES
#define AMP_CORE_TYPES 2
#endif
#ifndef AMP_FAST_CORE
#define AMP_FAST_CORE 1
#endif
#ifndef AMP_SLOW_CORE
#define AMP_SLOW_CORE 0
#endif

#ifdef SCHED_AMP
#include <linux/schedctl.h>
#endif

#define IPC_MODEL_STRING "IPC sampling SF estimation module"

/* Global PMC configuration for both core types */
core_experiment_set_t ipc_sampling_pmc_configuration[AMP_CORE_TYPES];

/* Descriptors for the various performance metrics */
metric_experiment_set_t ipc_sampling_metric_set[AMP_CORE_TYPES];

/* Per-thread private data for this monitoring module */
typedef struct {
	metric_experiment_set_t metric_set[AMP_CORE_TYPES]; /* Performance-metric descriptors */
	pmon_change_history_t ipc_history[AMP_CORE_TYPES];	/* Control of IPC phase changes */
	int cur_speedup_factor;								/* Store current SF (normalized) */
	metric_smoother_t sf_smoother;						/* Data structure to smooth SF values */
	int ipc_samples_cnt[AMP_CORE_TYPES];				/* Last IPC counts on the big and the small core */
	unsigned char warming_up;							/* Is the thread still warming up? */
} ipc_sampling_thread_data_t;

/* MM configuration parameters */
static struct {
	uint_t sfmodel_running_average_factor; 		/* Percentage of the previous running average employed to compute
												   the current moving average [0..100]	*/
	uint_t sfmodel_bigsmall_ratio;	/* In fixed point (two digits) ==> 22 <==> 2.2x */
	uint_t sfmodel_initial_ratio;	/* In fixed point two digits ==> 22 <==> 2.2x */
	uint_t sfmodel_freq_ratio;		/* 3 decimal digits ---> 15000 -> 1.5x  */
	uint_t sfmodel_nr_samples_warmup;
}
ipc_sampling_sfmodel_config;

/* RAW PMC configuration strings for the IPC on different architectures */
const char* ipc_sampling_pmcstr_cfg[]= {
#if defined(CONFIG_PMC_CORE_2_DUO) || defined(CONFIG_PMC_CORE_I7)
	"pmc0,pmc1,coretype=0",
	"pmc0,pmc1,coretype=1",
#elif defined(CONFIG_PMC_AMD)
	"pmc0=0xc0,pmc1=0x76,coretype=0",
	"pmc0=0xc0,pmc1=0x76,coretype=1",
#else  /* ARM */
	"pmc1=0x8,pmc2=0x11,coretype=0",
	"pmc1=0x8,pmc2=0x11,coretype=1",
#endif
	NULL
};

/* Return the capabilities/properties of this monitoring module */
static void ipc_sampling_module_counter_usage(monitoring_module_counter_usage_t* usage)
{
#if defined(CONFIG_PMC_ARM) || defined(CONFIG_PMC_ARM64)
	usage->hwpmc_mask= (1<<1) | (1<<2);
#else
	usage->hwpmc_mask=0x3;
#endif
	usage->nr_virtual_counters=1; // SF
	usage->nr_experiments=1;
	usage->vcounter_desc[0]="estimated_sf";
}

/* Create a simple set of performance metric descriptors (IPC) */
static void init_ipc_metric(metric_experiment_set_t* metric_set)
{
	pmc_metric_t* metric=NULL;
	metric_experiment_t* metricExp;
	pmc_arg_t arguments[2];
	init_metric_experiment_set_t(metric_set);

	/*######## EXP1 #########*/
	metricExp=&metric_set->exps[metric_set->nr_exps++];

	/* IPC */
	metric=&metricExp->metrics[metricExp->size++];

	arguments[0].index=0;
	arguments[0].type=hw_event_arg;
	arguments[1].index=1;
	arguments[1].type=hw_event_arg;

	init_pmc_metric(metric,"IPC",op_rate,arguments,1000);
}

/* MM initialization function */
static int ipc_sampling_enable_module(void)
{
	int i=0;

	if (configure_performance_counters_set(ipc_sampling_pmcstr_cfg, ipc_sampling_pmc_configuration, 2)) {
		printk("Can't configure global performance counters. This is too bad ... ");
		return -EINVAL;
	}

	for (i=0; i<AMP_CORE_TYPES; i++)
		init_ipc_metric(&ipc_sampling_metric_set[i]);

	/* init configuration parameters */
	ipc_sampling_sfmodel_config.sfmodel_running_average_factor=35; 	/* Percentage of the previous moving average of
						the current moving average [0..100]	*/
	ipc_sampling_sfmodel_config.sfmodel_bigsmall_ratio=70;		/* In fixed point two digit ==> 22 <==> 2.2x */
	ipc_sampling_sfmodel_config.sfmodel_initial_ratio=25;
	ipc_sampling_sfmodel_config.sfmodel_freq_ratio=750;
	ipc_sampling_sfmodel_config.sfmodel_nr_samples_warmup=5; /* At least one second running on each core type */

	printk(KERN_ALERT "%s has been loaded successfuly\n",IPC_MODEL_STRING);
	return 0;
}

/* MM cleanup function */
static void ipc_sampling_disable_module(void)
{
	int i=0;
	for (i=0; i<AMP_CORE_TYPES; i++)
		free_experiment_set(&ipc_sampling_pmc_configuration[i]);

	printk(KERN_ALERT "%s monitoring module unloaded!!\n",IPC_MODEL_STRING);
}


/* Print the values for the different configuration parameters */
static int ipc_sampling_on_read_config(char* str, unsigned int len)
{
	char* dest=str;

	dest+=sprintf(dest,"running_average_factor=%d\n",ipc_sampling_sfmodel_config.sfmodel_running_average_factor );
	dest+=sprintf(dest,"bigsmall_ratio=%d\n",ipc_sampling_sfmodel_config.sfmodel_bigsmall_ratio);
	dest+=sprintf(dest,"initial_ratio=%d\n",ipc_sampling_sfmodel_config.sfmodel_initial_ratio);
	dest+=sprintf(dest,"freq_ratio=%d\n",ipc_sampling_sfmodel_config.sfmodel_freq_ratio);
	dest+=sprintf(dest,"nr_samples_warmup=%d\n",ipc_sampling_sfmodel_config.sfmodel_nr_samples_warmup);
	
	return dest-str;
}

/* Parse a param_name=value string and change param_name's value accordingly   */
static int ipc_sampling_on_write_config(const char *str, unsigned int len)
{
	int val;

	if (sscanf(str,"running_average_factor %i",&val)==1 && val>0) {
		ipc_sampling_sfmodel_config.sfmodel_running_average_factor=val;
	} else if (sscanf(str,"bigsmall_ratio %i",&val)==1 && val>0) {
		ipc_sampling_sfmodel_config.sfmodel_bigsmall_ratio=val;
	} else if (sscanf(str,"initial_ratio %i",&val)==1 && val>0) {
		ipc_sampling_sfmodel_config.sfmodel_initial_ratio=val;
	} else if (sscanf(str,"freq_ratio %i",&val)==1 && val>0) {
		ipc_sampling_sfmodel_config.sfmodel_freq_ratio=val;
	} else if (sscanf(str,"nr_samples_warmup %i",&val)==1 && val>0) {
		ipc_sampling_sfmodel_config.sfmodel_nr_samples_warmup=val;
	}
	return len;
}

/* on fork() callback */
static int ipc_sampling_on_fork(unsigned long clone_flags, pmon_prof_t* prof)
{

	int i=0;
	ipc_sampling_thread_data_t*  data=NULL;

	if (prof->monitoring_mod_priv_data!=NULL)
		return 0;

	if (!prof->pmcs_config) {
		for(i=0; i<2; i++)
			clone_core_experiment_set_t(&prof->pmcs_multiplex_cfg[i],&ipc_sampling_pmc_configuration[i]);

		/* For now start with slow core events */
		prof->pmcs_config=get_cur_experiment_in_set(&prof->pmcs_multiplex_cfg[0]);
	}

	data= kmalloc(sizeof (ipc_sampling_thread_data_t), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	/* Initialization */
	/* Clone metric metainfo */
	memcpy(&data->metric_set[0],&ipc_sampling_metric_set[0],sizeof(metric_experiment_set_t)*2);

	for(i=0; i<AMP_CORE_TYPES; i++) {
		init_pmon_change_history(&data->ipc_history[i]);
		data->ipc_history[i].running_average=1000;
		data->ipc_samples_cnt[i]=0;
	}

	data->warming_up=1;
	data->cur_speedup_factor=ipc_sampling_sfmodel_config.sfmodel_initial_ratio*100; /* 2.5 x (default) */
	prof->monitoring_mod_priv_data = data;
	return 0;
}

/* on exec() callback */
static void ipc_sampling_on_exec(pmon_prof_t* prof)
{
#ifdef DEBUG
	printk(KERN_ALERT "A thread executed exec() !!!\n" );
#endif
}

/* 
 * Update the thread's SF based on the latest IPC value on both core types and
 * set the associated virtual count (SF value) in the PMC sample structure
 */
static int ipc_sampling_on_new_sample(pmon_prof_t* prof,int cpu,pmc_sample_t* sample,int flags,void* data)
{
	int cur_coretype=get_coretype_cpu(cpu);
	//core_experiment_t *cur_exp=prof->pmcs_config;
	ipc_sampling_thread_data_t* sfdata=prof->monitoring_mod_priv_data;
	int ipc=0;

	if (sfdata!=NULL) {

		metric_experiment_t* metric_exp=&sfdata->metric_set[cur_coretype].exps[sample->exp_idx];
		pmon_change_history_t *ipc_history=&sfdata->ipc_history[cur_coretype];
		/* Compute hl_metrics */
		compute_performance_metrics(sample->pmc_counts,metric_exp);
		ipc=metric_exp->metrics[0].count;

		/* Update running average */
		pmon_update_running_average(ipc_history,ipc,
		                            ipc_sampling_sfmodel_config.sfmodel_running_average_factor,12,1);


		sfdata->ipc_samples_cnt[cur_coretype]++;


		if (!sfdata->warming_up) {
			int complementary_core_type=cur_coretype==AMP_FAST_CORE?AMP_SLOW_CORE:AMP_FAST_CORE;
			int ipc_sample[AMP_CORE_TYPES];
			/* Update the SF */
			int speedup_estimate=0;

			/* Sort out ipc samples */
			ipc_sample[cur_coretype]=ipc_history->running_average;
			ipc_sample[complementary_core_type]=sfdata->ipc_history[complementary_core_type].running_average;

			/* Do not update in this case */
			if (ipc_sample[AMP_SLOW_CORE]==0)
				/* No change */
				speedup_estimate=sfdata->cur_speedup_factor;
			else
				speedup_estimate=(ipc_sample[AMP_FAST_CORE]*ipc_sampling_sfmodel_config.sfmodel_freq_ratio)/ipc_sample[AMP_SLOW_CORE];

			/* Update speedup taking into account extreme values */
			if (speedup_estimate<1000)
				sfdata->cur_speedup_factor=1000;
			else if (speedup_estimate>(ipc_sampling_sfmodel_config.sfmodel_bigsmall_ratio*100))
				sfdata->cur_speedup_factor=ipc_sampling_sfmodel_config.sfmodel_bigsmall_ratio*100;
			else
				sfdata->cur_speedup_factor=speedup_estimate;

			/* Embed virtual counter information so that the user can see what's going on */
			if ((prof->virt_counter_mask & 0x1) ) {
				sample->virt_mask|=0x1;
				sample->nr_virt_counts++;
				sample->virtual_counts[0]=sfdata->cur_speedup_factor;
			}

#ifdef DEBUG
			printk(KERN_ALERT "Current SF=%d (%d), Current IPC=%d, IPC FAST=%d, IPC SLOW=%d\n", sfdata->cur_speedup_factor, speedup_estimate,ipc,ipc_sample[AMP_FAST_CORE], ipc_sample[AMP_SLOW_CORE]);
#endif

			/* SF notifications for the runtime*/
#ifdef SCHED_AMP
			if (prof->flags & PMCTRACK_SF_NOTIFICATIONS) {
				/* Write SF in shared memory region */
				prof->this_tsk->schedctl->sc_sf=normalize_speedup_factor (sfdata->cur_speedup_factor);
			}
#endif
		} else {
			/* Check whether or not the warm up period is done  */
			if(sfdata->ipc_samples_cnt[AMP_FAST_CORE]>=ipc_sampling_sfmodel_config.sfmodel_nr_samples_warmup &&
			   sfdata->ipc_samples_cnt[AMP_SLOW_CORE]>=ipc_sampling_sfmodel_config.sfmodel_nr_samples_warmup)
				sfdata->warming_up=0;
		}

	}

	return 0;
}

static void ipc_sampling_on_migrate(pmon_prof_t* prof, int prev_cpu, int new_cpu) { }

/* Free up private data */
static void ipc_sampling_on_free_task(pmon_prof_t* prof)
{
	ipc_sampling_thread_data_t* data=(ipc_sampling_thread_data_t*)prof->monitoring_mod_priv_data;
	if (data)
		kfree(data);
}

/* Return current SF value for this thread */
static int ipc_sampling_get_current_metric_value(pmon_prof_t* prof, int key, uint64_t* value)
{
	ipc_sampling_thread_data_t* sfdata=prof->monitoring_mod_priv_data;

	/* Detect null data before accessing the samples buffer*/
	if (sfdata==NULL)
		return -1;

	if (key==MC_SPEEDUP_FACTOR) {

		(*value)=normalize_speedup_factor(sfdata->cur_speedup_factor);

		return 0;
	} else {
		return -1;
	}
}

/* Implementation of the monitoring_module_t interface */
monitoring_module_t ipc_sampling_sf_mm= {
	.info=IPC_MODEL_STRING,
	.id=-1,
	.enable_module=ipc_sampling_enable_module,
	.disable_module=ipc_sampling_disable_module,
	.on_read_config=ipc_sampling_on_read_config,
	.on_write_config=ipc_sampling_on_write_config,
	.on_fork=ipc_sampling_on_fork,
	.on_exec=ipc_sampling_on_exec,
	.on_new_sample=ipc_sampling_on_new_sample,
	.on_migrate=ipc_sampling_on_migrate,
	.on_free_task=ipc_sampling_on_free_task,
	.get_current_metric_value=ipc_sampling_get_current_metric_value,
	.module_counter_usage=ipc_sampling_module_counter_usage
};
