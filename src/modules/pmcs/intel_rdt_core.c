#include <pmc/intel_rdt.h>
#include <linux/random.h>
#include <linux/spinlock.h>

/* Basic node of the RMID pool (linked list) */
typedef struct {
	uint_t rmid;				/* RMID */
	atomic_t ref_counter;		/* Reference counter for this RMID */
	unsigned char free_rmid;	/* Is this RMID free? */
	uint64_t raw_bandwidth_value[RMID_MAX_LLCS];	/* For syswide MBM monitoring */
	uint64_t bandwidth_value[RMID_MAX_LLCS];
	struct list_head links;		/* prev and next pointers */
} rmid_node_t;

static rmid_node_t* node_pool; 			/* Array of RMID nodes */
static LIST_HEAD(available_rmids);			/* RMID free pool (doubly linked list) */
static DEFINE_SPINLOCK(rmid_pool_lock);	/* Lock for the RMID pool */
static int nr_available_rmids;
static rmid_allocation_policy_t rmid_allocation_policy=RMID_FIFO;
static uint_t* assigned_rmids;	/* For syswide MBM monitoring */
static int nr_assigned_rmids;
static intel_rdt_event_t* local_bandwidth_counts;

const char* rmid_allocation_policy_str[NR_RMID_ALLOC_POLICIES]= {"FIFO","LIFO","RANDOM"};
static intel_cmt_support_t* private_cmt_support=NULL;

#ifdef DEBUG
static void print_assigned_rmids(void)
{
	int i=0;
	char buf[256];
	char* dest=buf;

	dest+=sprintf(dest,"RMIDs,#%d",nr_assigned_rmids);
	for (i=0; i<nr_assigned_rmids; i++)
		dest+=sprintf(dest,",%u",assigned_rmids[i]);

	trace_printk("%s\n",buf);
}
#endif

static void update_local_bandwidth(rmid_node_t* node, unsigned int llc_id /* Use zero */)
{
	u64 val=__rmid_read(node->rmid,L3_LOCAL_BW_EVENT_ID);

	if (val & (RMID_VAL_ERROR | RMID_VAL_UNAVAIL)) {
		trace_printk(KERN_INFO "Error when reading bandwidth value");
		return;
	}

	val&=MAX_CMT_COUNT;

	if(node->raw_bandwidth_value[llc_id]>val)
		node->bandwidth_value[llc_id]=(MAX_CMT_COUNT - node->raw_bandwidth_value[llc_id])+val+1;
	else
		node->bandwidth_value[llc_id]=val-node->raw_bandwidth_value[llc_id];

	node->bandwidth_value[llc_id]*=private_cmt_support->upscaling_factor;
	node->raw_bandwidth_value[llc_id]=val;
}


/*
 * Initialize a pool with the RMID values
 * supported by the current processor
 */
int initialize_rmid_pool(void)
{
	int i=0;
	unsigned int starting_item;
	/* Bear in mind that RMID 0 is already used (reserved) !! */
	node_pool= kmalloc(sizeof (rmid_node_t)*(nr_available_rmids-1), GFP_KERNEL);

	if (node_pool == NULL) {
		printk(KERN_INFO "Monitoring module was not able to successfully allocate memory for RMID node pool");
		return -ENOMEM;
	}

	assigned_rmids= kmalloc(sizeof (uint_t)*(nr_available_rmids-1), GFP_KERNEL);
	nr_assigned_rmids=0;

	if (assigned_rmids == NULL) {
		printk(KERN_INFO "Monitoring module was not able to successfully allocate memory for RMID array");
		kfree(node_pool);
		return -ENOMEM;
	}

	local_bandwidth_counts=kmalloc(sizeof (intel_rdt_event_t)*(nr_available_rmids-1), GFP_KERNEL);

	if (local_bandwidth_counts == NULL) {
		printk(KERN_INFO "Monitoring module was not able to successfully allocate memory for local bandwidth counts");
		kfree(assigned_rmids);
		kfree(node_pool);
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
void free_rmid_pool(void)
{
	kfree(node_pool);
	kfree(assigned_rmids);
}

/* Remove a unused RMID from the free pool and return it. */
uint_t get_rmid(void)
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

	switch(rmid_allocation_policy) {
	case RMID_FIFO:
		rmid_node=list_first_entry(&available_rmids,rmid_node_t,links);
		break;
	case RMID_LIFO:
		rmid_node=list_entry(available_rmids.prev,rmid_node_t,links); /* list_last_entry */
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
		assigned_rmids[nr_assigned_rmids++]=selected_rmid;
		update_local_bandwidth(rmid_node,0);
#ifdef DEBUG
		print_assigned_rmids();
#endif
	}

	spin_unlock_irqrestore(&rmid_pool_lock,flags);
	return selected_rmid;

}

/* Functions to select/retrieve the current allocation
	policy for Intel CMT */
rmid_allocation_policy_t get_cmt_policy(void)
{
	return rmid_allocation_policy;
}

void set_cmt_policy(rmid_allocation_policy_t new_policy)
{
	rmid_allocation_policy=new_policy;
}

/*
 * Decrease the reference counter of a given RMID.
 * If the reference counter reaches 0, put the RMID back in
 * the free RMID pool.
 */
void put_rmid(uint_t rmid)
{
	rmid_node_t* rmid_node=NULL;
	unsigned long flags;

	if (rmid==0)
		return;

	rmid_node=&node_pool[rmid-1];

	if (rmid_node->free_rmid)
		return;

	if (atomic_dec_and_test(&rmid_node->ref_counter)) {
		int i=0;
		uint_t rmid=0;
		spin_lock_irqsave(&rmid_pool_lock,flags);
		rmid_node->free_rmid=1;
		rmid=rmid_node->rmid;
		list_add_tail(&(rmid_node->links),&available_rmids);
		nr_available_rmids++;

		/* Delete item and compact array */
		/* Find */
		while (i<nr_assigned_rmids && assigned_rmids[i]!=rmid) {
			i++;
		}

		/* Compact and delete */
		for (; i<nr_assigned_rmids-1; i++)
			assigned_rmids[i]=assigned_rmids[i+1];

		nr_assigned_rmids--;
#ifdef DEBUG
		print_assigned_rmids();
#endif
		spin_unlock_irqrestore(&rmid_pool_lock,flags);
	}
}

/* Increase the reference counter of a given RMID */
void use_rmid(uint_t rmid)
{
	rmid_node_t* rmid_node=NULL;

	if (rmid==0)
		return;

	rmid_node=&node_pool[rmid-1];

	if (rmid_node->free_rmid)
		return;

	atomic_inc(&rmid_node->ref_counter);
}


/*
 * Return array with per RMID event counts
 * the length of the array and the aggregate
 * system-wide count
 */
intel_rdt_event_t* intel_rdt_syswide_read_localbw(intel_cmt_support_t* cmt_support, unsigned int llc_id, unsigned int* count, uint64_t* aggregate_count)
{
	unsigned long flags;
	int i=0;
	uint64_t aggregate=0;
	rmid_node_t* node;
	intel_rdt_event_t* event_rmid;

	spin_lock_irqsave(&rmid_pool_lock,flags);

	/* Traverse assigned RMIDs */
	for (i=0; i<nr_assigned_rmids; i++) {
		node=&node_pool[assigned_rmids[i]-1];
		event_rmid=&local_bandwidth_counts[assigned_rmids[i]-1];
		update_local_bandwidth(node,llc_id);
		event_rmid->rmid=node->rmid;
		event_rmid->value=node->bandwidth_value[llc_id];
		aggregate+=event_rmid->value;
#ifdef DEBUG
		trace_printk("RMID=%u,BW=%llu\n",event_rmid->rmid,event_rmid->value);
#endif
	}

	/* Return values */
	*count=nr_assigned_rmids;
	*aggregate_count=aggregate;

	spin_unlock_irqrestore(&rmid_pool_lock,flags);

	return local_bandwidth_counts;
}

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

int intel_cmt_initialize(intel_cmt_support_t* cmt_support)
{
	cpuid_regs_t cpuid_regs;
	uint_t supported_events=0;

	/* Check capabilities*/
	cpuid_regs.eax=0x0F;
	cpuid_regs.ecx=0x01;
	cpuid_regs.ebx=cpuid_regs.edx=0x0;
	run_cpuid(cpuid_regs);
	/* Initialize the number of RMIDS (max pool) */
	cmt_support->total_rmids=nr_available_rmids=cpuid_regs.ecx;
	cmt_support->upscaling_factor=cpuid_regs.ebx;
	supported_events=cpuid_regs.edx & 0x07;

	printk(KERN_INFO "*** Intel CMT Info ***\n");
	printk(KERN_INFO "Available RMIDs:: %u\n", nr_available_rmids);
	printk(KERN_INFO "Upscaling Factor:: %d\n", cmt_support->upscaling_factor);

	if(supported_events&0x01) {
		printk(KERN_INFO "L3 Occupancy Supported\n");
		cmt_support->event_l3_occupancy=L3_OCCUPANCY_EVENT_ID;
	} else
		cmt_support->event_l3_occupancy=0;
	if(supported_events&0x02) {
		printk(KERN_INFO "L3 Total BW Supported\n");
		cmt_support->event_l3_total_bw=L3_TOTAL_BW_EVENT_ID;
	} else
		cmt_support->event_l3_total_bw=0;
	if(supported_events&0x04) {
		printk(KERN_INFO "L3 Local BW Supported\n");
		cmt_support->event_l3_local_bw=L3_LOCAL_BW_EVENT_ID;
	} else
		cmt_support->event_l3_local_bw=0;

	if (initialize_rmid_pool())
		return -ENOMEM;

	/* For MBM system-wide monitoring */
	private_cmt_support=cmt_support;
	return 0;
}


int intel_cat_initialize(intel_cat_support_t* cat_support)
{
	cpuid_regs_t cpuid_regs;
	int i;

	/* Check CAT capabilities (assume CAT is supported for now) */
	cpuid_regs.eax=0x10;
	cpuid_regs.ecx=0x01;
	cpuid_regs.ebx=cpuid_regs.edx=0x0;
	run_cpuid(cpuid_regs);
	cat_support->cat_cbm_length=(cpuid_regs.eax & 0x1f) +1;
	cat_support->cat_cbm_mask=(1<<cat_support->cat_cbm_length)-1;
	cat_support->cat_nr_cos_available=(cpuid_regs.edx & 0xff) + 1; //0 ... cpuid_regs.edx

	printk(KERN_INFO "*** Intel CAT Info ***\n");
	printk(KERN_INFO "Available # Class of Service IDs:: %u\n", cat_support->cat_nr_cos_available);
	printk(KERN_INFO "Length of capacity bitmasks (CBMs):: %d\n",cat_support->cat_cbm_length);

#ifdef CAT_PROOF_OF_CONCEPT
	/* Set up BITMASK to lower than usual */
	/* LOW */ /*HIGH*/
	wrmsr(IA32_LLC_MASK_1, 0x3f, 0);
#endif

	/** TODO: Handle the case correctly for masks bigger than 32 bits **/
	/* Initialize all masks to the default value (Just in case) */
	for (i=0; i<cat_support->cat_nr_cos_available; i++)
		wrmsr(IA32_LLC_MASK_0+i, cat_support->cat_cbm_mask, 0);

	return 0;
}

int intel_cmt_release(intel_cmt_support_t* cmt_support)
{
	free_rmid_pool();
	private_cmt_support=NULL;
	return 0;
}

int intel_cat_release(intel_cat_support_t* cat_support)
{

	return 0;
}


int intel_mba_initialize(intel_mba_support_t* mba_support)
{
	cpuid_regs_t cpuid_regs;
#ifdef MBA_PROCESSOR_ERRATA
	pmu_props_t* props=get_pmu_props_cpu(smp_processor_id());
#endif

	/* Check CAT capabilities (assume CAT is supported for now) */
	cpuid_regs.eax=0x10;
	cpuid_regs.ecx=0x0;
	cpuid_regs.ebx=cpuid_regs.edx=0x0;
	run_cpuid(cpuid_regs);

	mba_support->mba_is_supported=cpuid_regs.ebx & (1<<3); // || props->processor_model==85; /* Skylake */



	if (!mba_support->mba_is_supported) {
		printk("Intel MBA is NOT supported\n");
	}


	cpuid_regs.eax=0x10;
	cpuid_regs.ecx=0x03;
	cpuid_regs.ebx=cpuid_regs.edx=0x0;
	run_cpuid(cpuid_regs);

	mba_support->mba_max_throtling=(cpuid_regs.eax & ((12<<1)-1)) +1; /* Get bits */

	mba_support->mba_is_linear=(cpuid_regs.ecx & (1<<2)) >> 2;

	mba_support->mba_nr_cos_available=(cpuid_regs.edx & ((16<<1)-1)) + 1;

	printk(KERN_INFO "*** Intel MBA Info ***\n");
	printk(KERN_INFO "Available # Class of Service IDs:: %u\n", mba_support->mba_nr_cos_available);
	printk(KERN_INFO "Max Throtling:: %d\n",mba_support->mba_max_throtling);
	printk(KERN_INFO "Is linear?:: %d\n",mba_support->mba_is_linear);

	return mba_support->mba_is_supported;

}

int intel_mba_release(intel_mba_support_t* mba_support)
{
	return 0;
}

int get_cat_cbm(unsigned long* mask, intel_cat_support_t* cat_support, unsigned int clos, int cpu)
{
	uint64_t val;

	if (clos >= cat_support->cat_nr_cos_available)
		return 1;

	rdmsrl(IA32_LLC_MASK_0+clos, val);
	(*mask)=val & cat_support->cat_cbm_mask;

	return 0;
}

int get_mba_setting(unsigned long* val, intel_mba_support_t* mba_support, unsigned int clos, int cpu)
{
	uint64_t value;

	if (!mba_support->mba_is_supported)
		return 1;

	if (clos >= mba_support->mba_nr_cos_available)
		return 1;

	rdmsrl(IA32_L2_QOS_EXT_BW_THRTL_0+clos, value);
	(*val)=value;

	return 0;
}

int intel_cat_print_capacity_bitmasks(char* str, intel_cat_support_t* cat_support)
{
	int i=0;
	uint64_t val;
	unsigned int mask;
	char* dest=str;

	for (i=0; i<cat_support->cat_nr_cos_available; i++) {
		rdmsrl(IA32_LLC_MASK_0+i, val);
		mask=val & cat_support->cat_cbm_mask;
		dest+=sprintf(dest,"llc_cbm%i=0x%x\n",i,mask);
	}

	return dest-str;
}

int intel_cat_set_capacity_bitmask(intel_cat_support_t* cat_support, unsigned int idx, unsigned int mask)
{

	// /* For security reasons not allowed to change llc_cbm0 */

	if (idx<0 || idx>=cat_support->cat_nr_cos_available)
		return -EINVAL;

	mask&=cat_support->cat_cbm_mask;
	wrmsr(IA32_LLC_MASK_0+idx, mask, 0);
	//printk(KERN_INFO "Attempting to set  up CLOS %d MASK to 0x%x, actual value:: 0x%x\n",val,mask,(unsigned int)actual_val);

	return 0;
}


int intel_mba_print_delay_values(char* str, intel_mba_support_t* mba_support)
{
	int i=0;
	uint64_t val;
	char* dest=str;

	if (!mba_support->mba_is_supported)
		return 0;

	for (i=0; i<mba_support->mba_nr_cos_available; i++) {
		rdmsrl(IA32_L2_QOS_EXT_BW_THRTL_0+i, val);
		dest+=sprintf(dest,"mba_delay%i=%llu\n",i,val);
	}

	return dest-str;

}

int intel_mba_set_delay_values(intel_mba_support_t* mba_support, unsigned int idx, unsigned int val)
{

	uint64_t mask;

	if (!mba_support->mba_is_supported || idx>=mba_support->mba_nr_cos_available) // || val>=mba_support->mba_max_throtling)
		return -EINVAL;

	mask=val;
	wrmsr(IA32_L2_QOS_EXT_BW_THRTL_0+idx, mask, 0);
	//printk(KERN_INFO "Attempting to set  up CLOS %d MASK to 0x%x, actual value:: 0x%x\n",val,mask,(unsigned int)actual_val);

	return 0;
}


void intel_cmt_update_supported_events(intel_cmt_support_t* cmt_support,intel_cmt_thread_struct_t* tdata, unsigned int llc_id)
{
	u64 val=0;

	/* check for l3 occupancy */
	if(cmt_support->event_l3_occupancy) {
		val = __rmid_read(tdata->rmid,L3_OCCUPANCY_EVENT_ID);

		if (val & (RMID_VAL_ERROR | RMID_VAL_UNAVAIL))
			trace_printk("Error when reading value");
		else {
			val&=((1ULL<<62ULL)-1); //Mask...
			tdata->last_llc_utilization[llc_id][L3_OCCUPANCY_EVENT_ID-1]=val*cmt_support->upscaling_factor;
#ifdef DEBUG
			trace_printk("LLC Usage=%llu bytes\n",tdata->last_llc_utilization[llc_id][L3_OCCUPANCY_EVENT_ID-1]);
#endif
		}
	}

	/* check for l3 total BW */
	if(cmt_support->event_l3_total_bw) {
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
			tdata->last_llc_utilization[llc_id][L3_TOTAL_BW_EVENT_ID-1]*=cmt_support->upscaling_factor;
			tdata->last_cmt_value[llc_id][L3_TOTAL_BW_EVENT_ID-1]=val;
#ifdef DEBUG
			trace_printk("LLC Total BW=%llu bytes/s\n",tdata->last_llc_utilization[llc_id][L3_TOTAL_BW_EVENT_ID-1]);
#endif
		}
	}

	/* check for l3 local BW */
	if(cmt_support->event_l3_local_bw) {
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
			tdata->last_llc_utilization[llc_id][L3_LOCAL_BW_EVENT_ID-1]*=cmt_support->upscaling_factor;
			tdata->last_cmt_value[llc_id][L3_LOCAL_BW_EVENT_ID-1]=val;
#ifdef DEBUG
			trace_printk("LLC Local BW=%llu bytes/s\n",tdata->last_llc_utilization[llc_id][L3_LOCAL_BW_EVENT_ID-1]);
#endif
		}
	}

}
