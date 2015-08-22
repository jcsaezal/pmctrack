/*
 *  intel_cmt_mm.c
 *
 *  PMCTrack monitoring module that provides support for
 * 	Intel Cache Monitoring Technology (CMT) and Intel Memory Bandwidth
 *  Monitoring (MBM) 
 * 
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 * 
 *  This code is licensed under the GNU GPL v2.
 */

#include <pmc/hl_events.h>
#include <pmc/mc_experiments.h>
#include <pmc/monitoring_mod.h>
#include <pmc/pmu_config.h> //For cpuid_regs_t

#include <linux/random.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>
#include <asm/topology.h>
#include <linux/ftrace.h>
#define INTEL_CMT_MODULE_STR "PMCtrack module that supports Intel CMT"
#define DISABLE_RMID 0  	/* RMID reserved for the OS */

/* Variables to store CMT and MBM global parameters */
static uint_t nr_available_rmids;	/* Number of available RMIDs */
static uint_t upscaling_factor;		/* Factor to transform read values into byte counts */
static uint_t event_l3_occupancy;
static uint_t event_l3_total_bw;
static uint_t event_l3_local_bw;

/* Low level functions to access CMT/MBM MSRs */
static u64 __rmid_read(unsigned long rmid, uint_t event);
static inline void __set_rmid(unsigned long rmid);
static inline void __unset_rmid(void);

#define MSR_IA32_PQR_ASSOC	0xc8f
#define MSR_IA32_QM_CTR		0xc8e
#define MSR_IA32_QM_EVTSEL	0xc8d
#define RMID_VAL_ERROR		(1ULL << 63)
#define RMID_VAL_UNAVAIL	(1ULL << 62)
#define RMID_MAX_LLCS		4	/* Max number of processor "chips" in the machine */
#define CMT_MAX_EVENTS		3 	/* L3_USAGE, L3_TOTAL_BW, L3_LOCAL_BW */
#define L3_OCCUPANCY_EVENT_ID	0x01
#define L3_TOTAL_BW_EVENT_ID	0x02
#define L3_LOCAL_BW_EVENT_ID	0x03
/* 
 * We have found empirically that memory bandwith 
 * cumulative counters are 24-bit wide !!
 * That's the reason for this macro.
 */
#define MAX_CMT_COUNT	((1ULL<<24ULL)-1ULL)	

/** Functions to perform RMID allocation **/

/* 
 * Initialize a pool with the RMID values
 * supported by the current processor
 */
static int initialize_rmid_pool(void);

/* Free up the resources associated with the RMID pool */
static inline void free_rmid_pool(void);

/* 
 * Decrease the reference counter of a given RMID.
 * If the reference counter reaches 0, put the RMID back in
 * the free RMID pool. 
 */
static inline void put_rmid(uint_t rmid);

/* Remove a unused RMID from the free pool and return it. */
static uint_t get_rmid(void);

/* Increase the reference counter of a given RMID */
static void use_rmid(uint_t rmid); 

/* Supported RMID allocation policies */
typedef enum {
	RMID_FIFO,
	RMID_LIFO,
	RMID_RANDOM,
	NR_RMID_ALLOC_POLICIES
}
rmid_allocation_policy_t;

char* rmid_allocation_policy_str[NR_RMID_ALLOC_POLICIES]= {"FIFO","LIFO","RANDOM"};

/* Configuration parameters for this monitoring modules */
static struct {
	rmid_allocation_policy_t rmid_allocation_policy; /* Selected RMID allocation policy */
}
intel_cmt_config= {RMID_FIFO};


/* Per-thread private data for this monitoring module */
typedef struct {
	unsigned char first_time;
	uint_t rmid;
	uint64_t last_llc_utilization[RMID_MAX_LLCS][CMT_MAX_EVENTS];
	uint64_t last_cmt_value[RMID_MAX_LLCS][CMT_MAX_EVENTS];
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

/* probe callback for this monitoring module */
int intel_cmt_probe(void)
{
	cpuid_regs_t cpuid_regs;

	if (boot_cpu_data.x86_vendor!=X86_VENDOR_INTEL)
		return -ENOTSUPP;

	/* Instructions for Intel CMT Detection
	   can be found in Section 17.15.3 of Intel Software Developer Manual
	   */

	/* Check max leaf */
	cpuid_regs.eax=cpuid_regs.ebx=cpuid_regs.ecx=cpuid_regs.edx=0x0;

	run_cpuid(cpuid_regs);

	if (cpuid_regs.eax<7)
		return -ENOTSUPP;

	/* Check capabilities*/
	cpuid_regs.eax=0x07;
	cpuid_regs.ecx=0x0;
	cpuid_regs.ebx=cpuid_regs.edx=0x0;

	run_cpuid(cpuid_regs);

	if (!(cpuid_regs.ebx & (1<<12)))
		return -ENOTSUPP;

	cpuid_regs.eax=0x0F;
	cpuid_regs.ecx=0x0;
	cpuid_regs.ebx=cpuid_regs.edx=0x0;

	run_cpuid(cpuid_regs);

	if (!(cpuid_regs.ebx & (1<<1)))
		return -ENOTSUPP;

	return 0;
}

/* MM initialization function */
static int intel_cmt_enable_module(void)
{
	cpuid_regs_t cpuid_regs;
	uint_t supported_events=0;
	/* Check capabilities*/
	cpuid_regs.eax=0x0F;
	cpuid_regs.ecx=0x01;
	cpuid_regs.ebx=cpuid_regs.edx=0x0;
	run_cpuid(cpuid_regs);
	nr_available_rmids=cpuid_regs.ecx;
	upscaling_factor=cpuid_regs.ebx;
	supported_events=cpuid_regs.edx & 0x07;
	printk(KERN_INFO "*** Intel CMT Info ***\n");
	printk(KERN_INFO "Available RMIDs:: %u\n", nr_available_rmids);
	printk(KERN_INFO "Upscaling Factor:: %d\n",upscaling_factor);
	if(supported_events&0x01) {
		printk(KERN_INFO "L3 Occupancy Supported\n");
		event_l3_occupancy=L3_OCCUPANCY_EVENT_ID;
	} else
		event_l3_occupancy=0;
	if(supported_events&0x02) {
		printk(KERN_INFO "L3 Total BW Supported\n");
		event_l3_total_bw=L3_TOTAL_BW_EVENT_ID;
	} else
		event_l3_total_bw=0;
	if(supported_events&0x04) {
		printk(KERN_INFO "L3 Local BW Supported\n");
		event_l3_local_bw=L3_LOCAL_BW_EVENT_ID;
	} else
		event_l3_local_bw=0;

	if (initialize_rmid_pool())
		return -ENOMEM;
	return 0;
}

/* MM cleanup function */
static void intel_cmt_disable_module(void)
{
	free_rmid_pool();
	printk(KERN_ALERT "%s monitoring module unloaded!!\n",INTEL_CMT_MODULE_STR);
}


static int intel_cmt_on_read_config(char* str, unsigned int len)
{
	char* dest=str;
	dest+=sprintf(dest,"rmid_alloc_policy=%d (%s)\n",
	              intel_cmt_config.rmid_allocation_policy,
	              rmid_allocation_policy_str[intel_cmt_config.rmid_allocation_policy]);
	return dest-str;
}

static int intel_cmt_on_write_config(const char *str, unsigned int len)
{
	int val;

	if (sscanf(str,"rmid_alloc_policy %i",&val)==1 && val>=0 && val<NR_RMID_ALLOC_POLICIES) {
		intel_cmt_config.rmid_allocation_policy=val;
	} else {
		return -EINVAL;
	}
	return len;
}

/* on fork() callback */
static int intel_cmt_on_fork(unsigned long clone_flags, pmon_prof_t* prof)
{
	int i=0,j=0;
	intel_cmt_thread_data_t*  data=NULL;

	if (prof->monitoring_mod_priv_data!=NULL)
		return 0;

	data= kmalloc(sizeof (intel_cmt_thread_data_t), GFP_KERNEL);

	if (data == NULL)
		return -ENOMEM;

	if (is_new_thread(clone_flags)) {
		pmon_prof_t *pprof= (pmon_prof_t*)current->pmc;
		intel_cmt_thread_data_t* parent_data=pprof->monitoring_mod_priv_data;
		data->first_time=0;
		data->rmid=parent_data->rmid;
		use_rmid(parent_data->rmid); /* Increase ref counter */
		trace_printk("Assigned RMID::%u\n",data->rmid);
	} else {
		data->first_time=1;
		data->rmid=0; /* It will be assigned in the first context switch */
	}

	for (i=0; i<RMID_MAX_LLCS; i++) {
		for(j=0; j<CMT_MAX_EVENTS; j++) {
			data->last_llc_utilization[i][j]=0;
			data->last_cmt_value[i][j]=0;
		}
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
	uint64_t val;
	uint_t llc_id=topology_physical_package_id(cpu),i=0;
	int cnt_virt=0;

	if (tdata!=NULL) {

		if (!tdata->first_time) {
			/* check for l3 occupancy */
			if(event_l3_occupancy) {
				val = __rmid_read(tdata->rmid,L3_OCCUPANCY_EVENT_ID);

				if (val & (RMID_VAL_ERROR | RMID_VAL_UNAVAIL))
					trace_printk("Error when reading value");
				else {
					val&=((1ULL<<62ULL)-1); //Mask...
					tdata->last_llc_utilization[llc_id][L3_OCCUPANCY_EVENT_ID-1]=val*upscaling_factor;
					trace_printk("LLC Usage=%llu bytes\n",tdata->last_llc_utilization[llc_id][L3_OCCUPANCY_EVENT_ID-1]);
				}
			}

			/* check for l3 total BW */
			if(event_l3_total_bw) {
				val = __rmid_read(tdata->rmid,L3_TOTAL_BW_EVENT_ID);

				if (val & (RMID_VAL_ERROR | RMID_VAL_UNAVAIL))
					trace_printk(KERN_INFO "Error when reading value");
				else {
					val&=MAX_CMT_COUNT;
					if(tdata->last_cmt_value[llc_id][L3_TOTAL_BW_EVENT_ID-1]>val) {
						tdata->last_llc_utilization[llc_id][L3_TOTAL_BW_EVENT_ID-1]=(MAX_CMT_COUNT - tdata->last_cmt_value[llc_id][L3_TOTAL_BW_EVENT_ID-1])+val+1;
					} else {
						tdata->last_llc_utilization[llc_id][L3_TOTAL_BW_EVENT_ID-1]=val-tdata->last_cmt_value[llc_id][L3_TOTAL_BW_EVENT_ID-1];
					}
					tdata->last_llc_utilization[llc_id][L3_TOTAL_BW_EVENT_ID-1]*=upscaling_factor;
					tdata->last_cmt_value[llc_id][L3_TOTAL_BW_EVENT_ID-1]=val;
					trace_printk("LLC Total BW=%llu bytes/s\n",tdata->last_llc_utilization[llc_id][L3_TOTAL_BW_EVENT_ID-1]);
				}
			}
			
			/* check for l3 local BW */
			if(event_l3_local_bw) {
				val = __rmid_read(tdata->rmid,L3_LOCAL_BW_EVENT_ID);

				if (val & (RMID_VAL_ERROR | RMID_VAL_UNAVAIL))
					trace_printk(KERN_INFO "Error when reading value");
				else {
					val&=MAX_CMT_COUNT;
					if(tdata->last_cmt_value[llc_id][L3_LOCAL_BW_EVENT_ID-1]>val) {
						tdata->last_llc_utilization[llc_id][L3_LOCAL_BW_EVENT_ID-1]=(MAX_CMT_COUNT - tdata->last_cmt_value[llc_id][L3_LOCAL_BW_EVENT_ID-1])+val+1;
					} else {
						tdata->last_llc_utilization[llc_id][L3_LOCAL_BW_EVENT_ID-1]=val-tdata->last_cmt_value[llc_id][L3_LOCAL_BW_EVENT_ID-1];
					}
					tdata->last_llc_utilization[llc_id][L3_LOCAL_BW_EVENT_ID-1]*=upscaling_factor;
					tdata->last_cmt_value[llc_id][L3_LOCAL_BW_EVENT_ID-1]=val;
					trace_printk("LLC Local BW=%llu bytes/s\n",tdata->last_llc_utilization[llc_id][L3_LOCAL_BW_EVENT_ID-1]);
				}
			}


			/* Embed virtual counter information so that the user can see what's going on */
			for(i=0; i<CMT_MAX_EVENTS; i++) {
				if ((prof->virt_counter_mask & (1<<i) )) { 
					sample->virt_mask|=(1<<i);
					sample->nr_virt_counts++;
					sample->virtual_counts[cnt_virt]=tdata->last_llc_utilization[llc_id][i];
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
		put_rmid(data->rmid);

}

/* Free up private data */
static void intel_cmt_on_free_task(pmon_prof_t* prof)
{
	if (prof->monitoring_mod_priv_data)
		kfree(prof->monitoring_mod_priv_data);
}

/* on switch_in callback */
void intel_cmt_on_switch_in(pmon_prof_t* prof)
{
	intel_cmt_thread_data_t* data=(intel_cmt_thread_data_t*)prof->monitoring_mod_priv_data;
	if (!data)
		return;

	if (data->first_time && data->security_id==current_monitoring_module_security_id()) {
		// Assign RMID
		data->rmid=get_rmid(); 
		data->first_time=0;
#ifdef DEBUG		
		trace_printk("Assigned RMID::%u\n",data->rmid);
#endif
	}

	__set_rmid(data->rmid);

}

/* on switch_out callback */
void intel_cmt_on_switch_out(pmon_prof_t* prof)
{
	__unset_rmid();
}

/* Modify this function if necessary to expose CMT/MBM information to the OS scheduler */
static int intel_cmt_get_current_metric_value(pmon_prof_t* prof, int key, uint64_t* value)
{
	return -1;
}

/* Basic node of the RMID pool (linked list) */
typedef struct {
	uint_t rmid;				/* RMID */
	atomic_t ref_counter;		/* Reference counter for this RMID */
	unsigned char free_rmid;	/* Is this RMID free? */
	struct list_head links;		/* prev and next pointers */
} rmid_node_t;

rmid_node_t* node_pool; 			/* Array of RMID nodes */
LIST_HEAD(available_rmids);			/* RMID free pool (doubly linked list) */
DEFINE_SPINLOCK(rmid_pool_lock);	/* Lock for the RMID pool */

/* 
 * Initialize a pool with the RMID values
 * supported by the current processor
 */
static int initialize_rmid_pool(void)
{
	int i=0;
	unsigned int starting_item;
	/* Bear in mind that RMID 0 is already used (reserved) !! */
	node_pool= kmalloc(sizeof (rmid_node_t)*(nr_available_rmids-1), GFP_KERNEL);

	if (node_pool == NULL) {
		printk(KERN_INFO "Monitoring module was not able to successfully allocate memory for RMID node pool");
		return -ENOMEM;
	}

	/* Important to initialize the list every time */
	INIT_LIST_HEAD(&available_rmids);

	/* Generate pseudo-randomly at first */
	starting_item=(get_random_int()%nr_available_rmids);

	/* Allocate from the beginning */
	for (i=starting_item; i<nr_available_rmids; i++) {
		node_pool[i].rmid=i+1;
		node_pool[i].free_rmid=1;
		atomic_set(&node_pool[i].ref_counter,0);
		list_add_tail(&(node_pool[i].links),&available_rmids);
	}

	/* Remaining items... */
	for (i=0; i<starting_item; i++) {
		node_pool[i].rmid=i+1;
		node_pool[i].free_rmid=1;
		atomic_set(&node_pool[i].ref_counter,0);
		list_add_tail(&(node_pool[i].links),&available_rmids);
	}
	return 0;
}

/* Free up the resources associated with the RMID pool */
static inline void free_rmid_pool(void)
{
	kfree(node_pool);
}

/* Remove a unused RMID from the free pool and return it. */
static uint_t get_rmid(void)
{
	rmid_node_t* rmid_node=NULL;
	unsigned int random_item;
	rmid_node_t *aux = NULL;
	uint selected_rmid=0; //None
	unsigned long flags;

	spin_lock_irqsave(&rmid_pool_lock,flags);

	if (list_empty(&available_rmids)) {
		spin_unlock(&rmid_pool_lock);
		return 0;
	}

	switch(intel_cmt_config.rmid_allocation_policy) {
	case RMID_FIFO:
		rmid_node=list_first_entry(&available_rmids,rmid_node_t,links);
		break;
	case RMID_LIFO:
		rmid_node=list_last_entry(&available_rmids,rmid_node_t,links);
		break;
	case RMID_RANDOM:
		random_item=(get_random_int()%nr_available_rmids);
		list_for_each_entry_safe(rmid_node, aux, &available_rmids, links) {
			if ((random_item--)==0)
				break;
		}
		break;
	default:
		break;
	}

	if (rmid_node) {
		list_del(&rmid_node->links);
		rmid_node->free_rmid=0;
		nr_available_rmids--;
		selected_rmid=rmid_node->rmid;
		atomic_inc(&rmid_node->ref_counter);
	}

	spin_unlock_irqrestore(&rmid_pool_lock,flags);
	return selected_rmid;

}

/* 
 * Decrease the reference counter of a given RMID.
 * If the reference counter reaches 0, put the RMID back in
 * the free RMID pool. 
 */
static inline void put_rmid(uint_t rmid)
{
	rmid_node_t* rmid_node=NULL;
	unsigned long flags;

	if (rmid==0)
		return;

	rmid_node=&node_pool[rmid-1];

	if (rmid_node->free_rmid)
		return;

	if (atomic_dec_and_test(&rmid_node->ref_counter)) {
		spin_lock_irqsave(&rmid_pool_lock,flags);
		rmid_node->free_rmid=1;
		list_add_tail(&(rmid_node->links),&available_rmids);
		nr_available_rmids++;
		spin_unlock_irqrestore(&rmid_pool_lock,flags);
	}
}

/* Increase the reference counter of a given RMID */
static void use_rmid(uint_t rmid)
{
	rmid_node_t* rmid_node=NULL;

	if (rmid==0)
		return;

	rmid_node=&node_pool[rmid-1];

	if (rmid_node->free_rmid)
		return;

	atomic_inc(&rmid_node->ref_counter);
}

/** Low level functions to access CMT/MBM MSRs **/

static u64 __rmid_read(unsigned long rmid, uint_t event)
{
	u64 val;

	/* Select desired event by writting to MSR_IA32_QM_EVTSEL
	 * and then read the event count
	*/
	wrmsr(MSR_IA32_QM_EVTSEL, event, rmid);
	rdmsrl(MSR_IA32_QM_CTR, val);
	return val;
}

static inline void __set_rmid(unsigned long rmid)
{
	wrmsrl(MSR_IA32_PQR_ASSOC, rmid);
}

static inline void __unset_rmid(void)
{
	wrmsrl(MSR_IA32_PQR_ASSOC, DISABLE_RMID);
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
