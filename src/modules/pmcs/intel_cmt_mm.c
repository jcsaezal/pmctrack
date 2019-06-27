/*
 *  intel_cmt_mm.c
 *
 *  PMCTrack monitoring module that provides support for
 * - Intel Cache Monitoring Technology (CMT)
 * - Intel Memory Bandwidth Monitoring (MBM) 
 * - Intel Cache Allocation Technology (CAT) 
 * - Intel Memory Bandwidth Allocation (MBA) 
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 * 	      and Jorge Casas <jorcasas@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#include <pmc/hl_events.h>
#include <pmc/mc_experiments.h>
#include <pmc/monitoring_mod.h>
#include <pmc/intel_cmt.h>

#include <asm/atomic.h>
#include <asm/topology.h>
#include <linux/ftrace.h>

#define INTEL_CMT_MODULE_STR "PMCtrack module that supports Intel CMT"


static intel_cmt_support_t cmt_support;
static intel_cat_support_t cat_support;
static intel_mba_support_t mba_support;

/* Configuration parameters for this monitoring module */
static struct {
	rmid_allocation_policy_t rmid_allocation_policy; /* Selected RMID allocation policy */
}
intel_cmt_config= {RMID_FIFO};

/* Per-thread private data for this monitoring module */
typedef struct {
	unsigned char first_time;
	intel_cmt_thread_struct_t cmt_data;
	uint_t security_id;
} intel_cmt_thread_data_t;

/* Return the capabilities/properties of this monitoring module */
static void intel_cmt_module_counter_usage(monitoring_module_counter_usage_t* usage)
{
	usage->hwpmc_mask=0;
	usage->nr_virtual_counters=CMT_MAX_EVENTS; // L3_USAGE, L3_TOTAL_BW, L3_LOCAL_BW
	usage->nr_experiments=0;
	usage->vcounter_desc[0]="llc_usage";
	usage->vcounter_desc[1]="total_llc_bw";
	usage->vcounter_desc[2]="local_llc_bw";
}


/* MM initialization function */
static int intel_cmt_enable_module(void)
{
	int retval=0;

	if ((retval=intel_cmt_initialize(&cmt_support)))
		return retval;

	if ((retval=intel_cat_initialize(&cat_support))) {
		intel_cmt_release(&cmt_support);
		return retval;
	}

	intel_mba_initialize(&mba_support);

	return 0;
}

/* MM cleanup function */
static void intel_cmt_disable_module(void)
{
	intel_cmt_release(&cmt_support);
	intel_cat_release(&cat_support);
	intel_mba_release(&mba_support);

	printk(KERN_ALERT "%s monitoring module unloaded!!\n",INTEL_CMT_MODULE_STR);
}


static int intel_cmt_on_read_config(char* str, unsigned int len)
{
	char* dest=str;
	dest+=sprintf(dest,"rmid_alloc_policy=%d (%s)\n",
	              intel_cmt_config.rmid_allocation_policy,
	              rmid_allocation_policy_str[intel_cmt_config.rmid_allocation_policy]);
	dest+=sprintf(dest,"cat_nr_cos_available=%d\n",
	              cat_support.cat_nr_cos_available);
	dest+=sprintf(dest,"cat_cbm_length=%d\n",
	              cat_support.cat_cbm_length);

	if (mba_support.mba_is_supported) {
		dest+=sprintf(dest,"mba_max_delay=%d\n",
		              mba_support.mba_max_throtling);
		dest+=sprintf(dest,"mba_cos_available=%d\n",
		              mba_support.mba_nr_cos_available);
		dest+=sprintf(dest,"mba_linear_throttling=%d\n",
		              mba_support.mba_is_linear);
	}
	dest+=intel_cat_print_capacity_bitmasks(dest,&cat_support);
	dest+=intel_mba_print_delay_values(dest,&mba_support);

	return dest-str;
}

//

static int intel_cmt_on_write_config(const char *str, unsigned int len)
{
	int val;
	unsigned int uval;
	unsigned int mask=0;

	if (sscanf(str,"rmid_alloc_policy %i",&val)==1 && val>=0 && val<NR_RMID_ALLOC_POLICIES) {
		intel_cmt_config.rmid_allocation_policy=val;
		set_cmt_policy(intel_cmt_config.rmid_allocation_policy);
	} else if (sscanf(str,"cos_id=%i",&val)==1 &&
	           (val>=0 && val<cat_support.cat_nr_cos_available)) {
		pmon_prof_t* prof=current->pmc;
		intel_cmt_thread_data_t*  data;

		if (!prof || !prof->monitoring_mod_priv_data)
			return -EINVAL;

		/* Setup cosid !! */
		data=prof->monitoring_mod_priv_data;
		data->cmt_data.cos_id=val;
		printk(KERN_ALERT "Setting COSID=%d for process %d\n",data->cmt_data.cos_id,current->tgid);
	} else if (sscanf(str,"llc_cbm%i 0x%x",&val,&mask)==2) {
		intel_cat_set_capacity_bitmask(&cat_support,val,mask);
	} else if (sscanf(str,"mba_delay%i %u",&val,&uval)==2) {
		intel_mba_set_delay_values(&mba_support,val,uval);
	} else {
		return -EINVAL;
	}
	return len;
}

/* on fork() callback */
static int intel_cmt_on_fork(unsigned long clone_flags, pmon_prof_t* prof)
{
	intel_cmt_thread_data_t*  data=NULL;

	if (prof->monitoring_mod_priv_data!=NULL)
		return 0;

	data= kmalloc(sizeof (intel_cmt_thread_data_t), GFP_KERNEL);

	if (data == NULL)
		return -ENOMEM;

	initialize_cmt_thread_struct(&data->cmt_data);

	if (is_new_thread(clone_flags) && current->pmc) {
		pmon_prof_t *pprof= (pmon_prof_t*)current->pmc;
		intel_cmt_thread_data_t* parent_data=pprof->monitoring_mod_priv_data;
		data->first_time=0;
		data->cmt_data.rmid=parent_data->cmt_data.rmid;
		use_rmid(parent_data->cmt_data.rmid); /* Increase ref counter */
		trace_printk("Assigned RMID::%u\n",data->cmt_data.rmid);
	} else {
		data->first_time=1;
		data->cmt_data.rmid=0; /* It will be assigned in the first context switch */
	}

	data->security_id=current_monitoring_module_security_id();
	prof->monitoring_mod_priv_data = data;
	return 0;
}

/* on exec() callback */
static void intel_cmt_on_exec(pmon_prof_t* prof) { }

/*
 * Read the LLC occupancy and cumulative memory bandwidth counters
 * and set the associated virtual counts in the PMC sample structure
 */
static int intel_cmt_on_new_sample(pmon_prof_t* prof,int cpu,pmc_sample_t* sample,int flags,void* data)
{
	intel_cmt_thread_data_t* tdata=prof->monitoring_mod_priv_data;
	uint_t llc_id=topology_physical_package_id(cpu),i=0;
	int cnt_virt=0;

	if (tdata!=NULL) {

		if (!tdata->first_time) {

			intel_cmt_update_supported_events(&cmt_support,&tdata->cmt_data,llc_id);

			/* Embed virtual counter information so that the user can see what's going on */
			for(i=0; i<CMT_MAX_EVENTS; i++) {
				if ((prof->virt_counter_mask & (1<<i) )) {
					sample->virt_mask|=(1<<i);
					sample->nr_virt_counts++;
					sample->virtual_counts[cnt_virt]=tdata->cmt_data.last_llc_utilization[llc_id][i];
					cnt_virt++;
				}
			}
		}

	}

	return 0;
}

static void intel_cmt_on_migrate(pmon_prof_t* prof, int prev_cpu, int new_cpu) { }

/* on exit() callback -> return RMID to the pool */
static void intel_cmt_on_exit(pmon_prof_t* prof)
{
	intel_cmt_thread_data_t* data=(intel_cmt_thread_data_t*)prof->monitoring_mod_priv_data;
	if (data)
		put_rmid(data->cmt_data.rmid);

}

/* Free up private data */
static void intel_cmt_on_free_task(pmon_prof_t* prof)
{
	if (prof->monitoring_mod_priv_data)
		kfree(prof->monitoring_mod_priv_data);
}

/* on switch_in callback */
static void intel_cmt_on_switch_in(pmon_prof_t* prof)
{
	intel_cmt_thread_data_t* data=(intel_cmt_thread_data_t*)prof->monitoring_mod_priv_data;
	int was_first_time=0;

	if (!data)
		return;

	if (data->first_time && data->security_id==current_monitoring_module_security_id()) {
		// Assign RMID
		data->cmt_data.rmid=get_rmid();
		data->first_time=0;
		was_first_time=0;
#ifdef DEBUG
		trace_printk("Assigned RMID::%u\n",data->cmt_data.rmid);
#endif
	}

	__set_rmid_and_cos(data->cmt_data.rmid,data->cmt_data.cos_id);

	if (was_first_time) {
		uint_t llc_id=topology_physical_package_id(smp_processor_id());
		intel_cmt_update_supported_events(&cmt_support,&data->cmt_data,llc_id);
	}


}

/* on switch_out callback */
static void intel_cmt_on_switch_out(pmon_prof_t* prof)
{
	__unset_rmid();
}

/* Modify this function if necessary to expose CMT/MBM information to the OS scheduler */
static int intel_cmt_get_current_metric_value(pmon_prof_t* prof, int key, uint64_t* value)
{
	return -1;
}



/* Implementation of the monitoring_module_t interface */
monitoring_module_t intel_cmt_mm= {
	.info=INTEL_CMT_MODULE_STR,
	.id=-1,
	.probe_module=intel_cmt_probe,
	.enable_module=intel_cmt_enable_module,
	.disable_module=intel_cmt_disable_module,
	.on_read_config=intel_cmt_on_read_config,
	.on_write_config=intel_cmt_on_write_config,
	.on_fork=intel_cmt_on_fork,
	.on_exec=intel_cmt_on_exec,
	.on_new_sample=intel_cmt_on_new_sample,
	.on_migrate=intel_cmt_on_migrate,
	.on_exit=intel_cmt_on_exit,
	.on_free_task=intel_cmt_on_free_task,
	.on_switch_in=intel_cmt_on_switch_in,
	.on_switch_out=intel_cmt_on_switch_out,
	.get_current_metric_value=intel_cmt_get_current_metric_value,
	.module_counter_usage=intel_cmt_module_counter_usage
};
