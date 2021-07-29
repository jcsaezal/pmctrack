/*
 *  intel_cmt_mm.c
 *
 *  PMCTrack monitoring module that provides support for user-space control
 *  of Intel RDT features (CAT, CMT and MBM)
 *
 *  Copyright (c) 2019 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#include <pmc/hl_events.h>
#include <pmc/mc_experiments.h>
#include <pmc/monitoring_mod.h>
#include <pmc/intel_rdt.h>

#include <asm/atomic.h>
#include <asm/topology.h>
#include <linux/ftrace.h>
#include <linux/spinlock.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/semaphore.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <pmc/edp.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
#include <linux/sched/task.h> /* for get_task_struct()/put_task_struct() */
#endif

#define INTEL_RDT_MODULE_STR "User-space control of Intel RDT features"


static intel_cmt_support_t cmt_support;
static intel_cat_support_t cat_support;
static intel_mba_support_t mba_support;

/* Configuration parameters for this monitoring module */
static struct {
	rmid_allocation_policy_t rmid_allocation_policy; /* Selected RMID allocation policy */
	unsigned long sched_period_ms;
}
intel_cmt_config= {RMID_FIFO,500};

/* Per-thread private data for this monitoring module */
typedef struct {
	unsigned char first_time;
	intel_cmt_thread_struct_t cmt_data;
	uint64_t instr_counter;         /* Global instruction counter to compute the EDP */
	uint_t security_id;
} intel_cmt_thread_data_t;

#define RDT_MAX_EVENTS (MAX_PERFORMANCE_COUNTERS+MAX_VIRTUAL_COUNTERS)


typedef struct {
	spinlock_t lock;
	int cpu;
	pid_t pid;
	pid_t last_pid;
	int clos_id;
	int last_clos_id;
	unsigned long cat_mask;
	unsigned long mba_setting;
	uint64_t acum_values[RDT_MAX_EVENTS];
	uint64_t avg_values[RDT_MAX_EVENTS];
	/* To be set by each app */
	int nr_events;
	int nr_virtual_counters;
	int nr_samples;
	char comm[TASK_COMM_LEN];
	ktime_t last_sample;
} userspace_cpu_rdt_sample_t;

#define MAX_STR_CONFIG 512

typedef struct {
	ktime_t timestamp_sample; /* Incremented */
	struct hrtimer hr_timer; /* hrtimer */
	struct proc_dir_entry* proc_entry;
	userspace_cpu_rdt_sample_t __percpu *cpu_sample;
	spinlock_t lock;
	struct semaphore sem;
	core_experiment_set_t* pmc_configuration;
	char pmc_str_config[MAX_STR_CONFIG];
	unsigned char reader_blocked;
} userspace_rdt_global_t;

static userspace_rdt_global_t userspace_rdt_info;

static enum hrtimer_restart rdt_timer_callback_master(struct hrtimer *timer);

static ssize_t rdt_proc_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);
static ssize_t rdt_proc_read(struct file *filp, char __user *buf, size_t len, loff_t *off);

extern struct proc_dir_entry *pmc_dir;

static const pmctrack_proc_ops_t fops = {
	.PMCT_PROC_READ = rdt_proc_read,
	.PMCT_PROC_WRITE = rdt_proc_write,
};

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
	userspace_rdt_global_t *global = &userspace_rdt_info;
	int i,j;
	int retval=0;
	ktime_t now=ktime_get();
	int cmt_init=0;
	int cat_init=0;
	int mba_init=0;
	int edp_init=0;

	/* Clear global structure ... */
	memset(global,0,sizeof(userspace_rdt_global_t));

	if ((retval=intel_cmt_initialize(&cmt_support)))
		return retval;

	cmt_init=1;


	if ((retval=intel_cat_initialize(&cat_support)))
		goto out_error;

	cat_init=1;

	if ((retval=edp_initialize_environment()))
		goto out_error;

	edp_init=1;

	intel_mba_initialize(&mba_support);

	mba_init=1;


	/* Initialize global stuff for the userspace control */

	global->cpu_sample = alloc_percpu(userspace_cpu_rdt_sample_t);
	global->pmc_configuration=NULL; /* No counters for now */
	strcpy(global->pmc_str_config,"none\n");

	get_online_cpus();
	for_each_online_cpu(i) {
		userspace_cpu_rdt_sample_t *cinfo = per_cpu_ptr(global->cpu_sample, i);
		spin_lock_init(&cinfo->lock);
		cinfo->cpu=i; /* No sample... */
		cinfo->clos_id=cinfo->last_clos_id=0;
		cinfo->pid=cinfo->last_pid=-1;
		cinfo->cat_mask=0;
		cinfo->mba_setting=0;

		for (j=0; j<RDT_MAX_EVENTS; j++)
			cinfo->acum_values[i]=cinfo->avg_values[i]=0;

		cinfo->nr_events=cinfo->nr_virtual_counters=cinfo->nr_samples=0;
		cinfo->last_sample=now;
		strcpy(cinfo->comm,"unknown");
	}
	put_online_cpus();

	spin_lock_init(&global->lock);
	sema_init(&global->sem,0);
	global->reader_blocked=0;
	global->timestamp_sample=now;
	global->proc_entry = proc_create("rdt_user", 0666, pmc_dir, &fops);

	if (!global->proc_entry) {
		retval=-ENOMEM;
		goto out_error;
	}


	get_cpu();
	hrtimer_init(&global->hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_PINNED );
	global->hr_timer.function = rdt_timer_callback_master;
	hrtimer_start(&global->hr_timer, ktime_set(0, intel_cmt_config.sched_period_ms * 1000000 /* In ns */), HRTIMER_MODE_REL_PINNED);
	put_cpu();

	return 0;
out_error:
	if (cmt_init)
		intel_cmt_release(&cmt_support);

	if (cat_init)
		intel_cat_release(&cat_support);

	if (edp_init)
		edp_release_resources();

	if (mba_init)
		intel_mba_release(&mba_support);

	if (global->proc_entry)
		remove_proc_entry("rdt_user",pmc_dir);

	if (global->cpu_sample)
		free_percpu(global->cpu_sample);

	return retval;
}

static void free_up_pmc_config(void)
{

	if (userspace_rdt_info.pmc_configuration) {
		free_experiment_set(userspace_rdt_info.pmc_configuration);
		vfree(userspace_rdt_info.pmc_configuration);
		userspace_rdt_info.pmc_configuration=NULL;
		strcpy(userspace_rdt_info.pmc_str_config,"none\n");
	}

}

/* MM cleanup function */
static void intel_cmt_disable_module(void)
{
	userspace_rdt_global_t *global = &userspace_rdt_info;

	intel_cmt_release(&cmt_support);
	intel_cat_release(&cat_support);
	intel_mba_release(&mba_support);
	edp_release_resources();

	if (global->cpu_sample)
		free_percpu(global->cpu_sample);
	if (global->proc_entry)
		remove_proc_entry("rdt_user", pmc_dir);

	printk(KERN_ALERT "%s monitoring module unloaded!!\n",INTEL_RDT_MODULE_STR);

	/* Unregister sched-tick callback */
	pr_info("Stopping timer...\n");
	hrtimer_cancel(&global->hr_timer);

	free_up_pmc_config();
}


static inline void update_cpu_statistics(int cpu )
{
	userspace_rdt_global_t *global = &userspace_rdt_info;
	userspace_cpu_rdt_sample_t *cinfo = per_cpu_ptr(global->cpu_sample,cpu);
	unsigned long flags;
	int i=0;

	spin_lock_irqsave(&cinfo->lock,flags);

	/* No samples last time */
	if (cinfo->nr_samples==0) {
		cinfo->pid=-1;
		cinfo->clos_id=0;
		cinfo->cat_mask=cinfo->mba_setting=0;
		strcpy(cinfo->comm,"unknown");
	} else {
		/* Record last pid .... */
		cinfo->pid=cinfo->last_pid;
		cinfo->clos_id=cinfo->last_clos_id;
		/* Obtain averages... */
		for (i=0; i<(cinfo->nr_events+cinfo->nr_virtual_counters); i++) {
			cinfo->avg_values[i]=cinfo->acum_values[i];
			if (i==cinfo->nr_events) /* Average cache usage...*/
				cinfo->avg_values[i]/=cinfo->nr_samples;
			/* Clear acums... */
			cinfo->acum_values[i]=0;
		}
		cinfo->nr_samples=0;
		/* Update cat mask and mba mask */
		get_cat_cbm(&cinfo->cat_mask,&cat_support,cinfo->clos_id,cpu);
		get_mba_setting(&cinfo->mba_setting,&mba_support,cinfo->clos_id,cpu);

		cinfo->mba_setting=0;


	}
	spin_unlock_irqrestore(&cinfo->lock,flags);

}

#ifdef USE_SLAVE_TIMER
static void rdt_timer_callback_slave(void *info)
{
	update_cpu_statistics(smp_processor_id());

}
#endif

static enum hrtimer_restart rdt_timer_callback_master(struct hrtimer *timer)
{
	userspace_rdt_global_t *global = &userspace_rdt_info;
	int i;
	unsigned long flags;
	int orun;
	ktime_t now = timer->base->get_time();

	orun = hrtimer_forward(timer, now, ktime_set(0, intel_cmt_config.sched_period_ms * 1000000 /* In ns */));
	if (orun == 0)
		return HRTIMER_RESTART;

#ifdef USE_SLAVE_TIMER
	smp_call_function_many(cpu_online_mask, rdt_timer_callback_slave, (void*)&now, 0);
#else
	get_online_cpus();
	for_each_online_cpu(i)
	update_cpu_statistics(i);
	put_online_cpus();
#endif


	/* Notify reader */
	spin_lock_irqsave(&global->lock,flags);

	global->timestamp_sample=now; /* Update info */

	/* Wake it up! */
	if (global->reader_blocked) {
		global->reader_blocked=0;
		up(&global->sem);
	}

	spin_unlock_irqrestore(&global->lock,flags);

	return HRTIMER_RESTART;
}

static int intel_cmt_on_read_config(char* str, unsigned int len)
{
	char* dest=str;
	dest+=sprintf(dest,"rmid_alloc_policy=%d (%s)\n",
	              intel_cmt_config.rmid_allocation_policy,
	              rmid_allocation_policy_str[intel_cmt_config.rmid_allocation_policy]);
	dest+=sprintf(dest,"sched_period_ms=%ld\n",
	              intel_cmt_config.sched_period_ms);

	dest+=sprintf(dest,"rdt_pmc_config=%s",userspace_rdt_info.pmc_str_config);
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
	dest+=edp_dump_global_counters(dest);

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
	} else if (sscanf(str,"sched_period_ms %i",&val)==1 && val>0 && val<=1000) {
		intel_cmt_config.sched_period_ms=val;
	} else if (sscanf(str,"cos_id=%i",&val)==1 &&
	           (val>=0 && val<cat_support.cat_nr_cos_available)) {
		pmon_prof_t* prof = get_prof(current);
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

	} else if (strncmp(str,"rdt_pmc_config ",15)==0) {
		core_experiment_set_t* new_set=NULL;
		const char* event_config=str + 15;
		const char *str_config[]= {event_config,NULL};

		if (strlen(event_config) >= MAX_STR_CONFIG)
			return -ENOSPC;


		if (strcmp(event_config,"none\n")==0) {
			/* Clear previous config */
			free_up_pmc_config();
		} else {

			new_set=vmalloc(sizeof(core_experiment_set_t));

			if (configure_performance_counters_set(str_config, new_set, 1)) {
				printk("Can't configure global performance counters. This is too bad ... ");
				vfree(new_set);
				return -EINVAL;
			}

			/* Clear previous config */
			free_up_pmc_config();

			/* Set up new config */
			userspace_rdt_info.pmc_configuration=new_set;
			strcpy(userspace_rdt_info.pmc_str_config,event_config);
		}
	} else if (strncmp(str,"restart_edp",11)==0) {
		printk(KERN_INFO "Resetting global EDP counters\n");
		edp_reset_global_counters();
		return len;
	} else if (strncmp(str,"pause_edp",9)==0) {
		printk(KERN_INFO "Pause global EDP counts\n");
		edp_pause_global_counts();
		return len;
	} else if (strncmp(str,"resume_edp",10)==0) {
		printk(KERN_INFO "Resume global EDP counts\n");
		edp_resume_global_counts();
		return len;
	} else {
		return -EINVAL;
	}
	return len;
}

/* on fork() callback */
static int intel_cmt_on_fork(unsigned long clone_flags, pmon_prof_t* prof)
{
	int error=0;
	intel_cmt_thread_data_t*  data=NULL;
	pmon_prof_t *pprof = get_prof(current);

	if (prof->monitoring_mod_priv_data!=NULL)
		return 0;

	data= kmalloc(sizeof (intel_cmt_thread_data_t), GFP_KERNEL);

	if (data == NULL)
		return -ENOMEM;

	initialize_cmt_thread_struct(&data->cmt_data);

	if (is_new_thread(clone_flags) && get_prof_enabled(pprof)) {
		intel_cmt_thread_data_t* parent_data=pprof->monitoring_mod_priv_data;
		data->first_time=0;
		data->cmt_data.rmid=parent_data->cmt_data.rmid;
		use_rmid(parent_data->cmt_data.rmid); /* Increase ref counter */
		//trace_printk("Assigned RMID::%u\n",data->cmt_data.rmid);
	} else {
		data->first_time=1;
		data->cmt_data.rmid=0; /* It will be assigned in the first context switch */
	}


	/* Configure counters (if forced by the user) */
	if (!prof->pmcs_config && userspace_rdt_info.pmc_configuration) {
		error=clone_core_experiment_set_t(&prof->pmcs_multiplex_cfg[0],userspace_rdt_info.pmc_configuration,prof->this_tsk);

		if (error) {
			kfree(data);
			return error;
		}

		/* For now start with slow core events */
		prof->pmcs_config=get_cur_experiment_in_set(&prof->pmcs_multiplex_cfg[0]);

		/* Activate EBS MODE if necessary */
		if (prof->pmcs_config->ebs_idx!=-1)
			prof->profiling_mode=EBS_SCHED_MODE;
	}

	data->instr_counter=0;
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
	uint_t llc_id=topology_physical_package_id(cpu);
	int cnt_virt=0;
	unsigned long spflags;
	int i=0;
	int j=0;
	userspace_cpu_rdt_sample_t *cinfo = per_cpu_ptr(userspace_rdt_info.cpu_sample, cpu);
	struct task_struct* p=prof->this_tsk;

	if (tdata==NULL)
		return -1;

	/*
	 * Update global instruction counter to measure EDP.
	 * Important assumption -> instructions are always counted
	 * with the first PMC in the set (logical counter #0)
	 */
	tdata->instr_counter+=sample->pmc_counts[0];


	if (tdata->first_time)
		return -1;

	intel_cmt_update_supported_events(&cmt_support,&tdata->cmt_data,llc_id);


	if (prof->profiling_mode==EBS_SCHED_MODE || prof->profiling_mode==TBS_SCHED_MODE)
		prof->virt_counter_mask=0x7; /* Force activation of virtual counters*/

	/* Embed virtual counter information so that the user can see what's going on */
	for(i=0; i<CMT_MAX_EVENTS; i++) {
		if ((prof->virt_counter_mask & (1<<i) )) {
			sample->virt_mask|=(1<<i);
			sample->nr_virt_counts++;
			sample->virtual_counts[cnt_virt]=tdata->cmt_data.last_llc_utilization[llc_id][i];
			cnt_virt++;
		}
	}

	/* Add new per-cpu sample */
	spin_lock_irqsave(&cinfo->lock,spflags);

	cinfo->last_pid=sample->pid;
	cinfo->last_clos_id=tdata->cmt_data.cos_id;
	cinfo->nr_events=sample->nr_counts;
	cinfo->nr_virtual_counters=sample->nr_virt_counts;

	for (i=0; i<cinfo->nr_events; i++)
		cinfo->acum_values[i]+=sample->pmc_counts[i];

	for (i=0,j=cinfo->nr_events; i<cinfo->nr_virtual_counters; i++,j++)
		cinfo->acum_values[j]+=sample->virtual_counts[i];

	cinfo->nr_samples++;
	cinfo->last_sample=ktime_get();
	get_task_comm(cinfo->comm, p);

	spin_unlock_irqrestore(&cinfo->lock,spflags);

#ifdef DEBUG
	trace_printk("Comm is %s for pid %d, flags,type is (%d,%d)\n",comm,p->pid,flags,sample->type);
#endif
	return 0;
}

static void intel_cmt_on_migrate(pmon_prof_t* prof, int prev_cpu, int new_cpu) { }

/* on exit() callback -> return RMID to the pool */
static void intel_cmt_on_exit(pmon_prof_t* prof)
{
	intel_cmt_thread_data_t* data=(intel_cmt_thread_data_t*)prof->monitoring_mod_priv_data;

	if (data)
		put_rmid(data->cmt_data.rmid);

	if (get_prof_enabled(prof))
		edp_update_global_instr_counter(&data->instr_counter);

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


static int __update_cos_task(void *arg)
{
	intel_cmt_thread_data_t*  data=(intel_cmt_thread_data_t*) arg;
	__set_rmid_and_cos(data->cmt_data.rmid,data->cmt_data.cos_id);
	return 0;
}


#define PBUFSIZ 100

static ssize_t rdt_proc_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
	char kbuf[PBUFSIZ];
	pid_t pid;
	int cos;
	struct task_struct* target=NULL;
	pmon_prof_t* prof;
	intel_cmt_thread_data_t*  data;
	int cpu_task;
	int nr_tries=0;
	int ret=-EAGAIN;
	int retval;
	const int max_nr_tries=3;

	if (len>=PBUFSIZ)
		return -ENOSPC;

	if (copy_from_user(kbuf,buf,len))
		return -EFAULT;

	kbuf[len]=0;

	if (sscanf(kbuf,"set_cos %d %d",&cos,&pid)==2) {
		if (cos <0 || cos>=cat_support.cat_nr_cos_available)
			return -EINVAL;

		rcu_read_lock();
		target = pmctrack_find_process_by_pid(pid);

		if(!target || !(prof = get_prof(target))) {
			rcu_read_unlock();
			return len; /* if the process does not exist, no error occurs */
			//return -ESRCH;
		}
		/* Prevent target from going away */
		get_task_struct(target);
		rcu_read_unlock();

		/* set cos */
		if (!prof->monitoring_mod_priv_data) {
			retval=-ENODATA;
			goto out_err;
		}


		/* Setup cosid !! */
		data=prof->monitoring_mod_priv_data;
		data->cmt_data.cos_id=cos;

		/* Go to remote CPU to make the change right away if pid is a runnable process */
		cpu_task=task_cpu_safe(target);

		if (cpu_task!=-1 && target->state==TASK_RUNNING) {
			/*
			 * Note that the task may go to sleep or be migrated in the meantime. That is not a big deal
			 * since the save callback is invoked in that case. That callback already updates
			 * the COS ID
			 */
			while (nr_tries<max_nr_tries && ret==-EAGAIN) {
				ret=pmct_cpu_function_call(target, cpu_task, __update_cos_task,data);
				nr_tries++;
			}
			if (ret!=0)
				trace_printk("Couldn't change the COS ID after %d tries\n",nr_tries);
			retval=ret;
		}

		put_task_struct(target);
	} else
		return -EINVAL;

	return len;
out_err:
	put_task_struct(target);
	return retval;
}

char unsafe_kbuf[4096];

static ssize_t rdt_proc_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{

	static ktime_t last_read;
	userspace_rdt_global_t *global = &userspace_rdt_info;
	int nr_bytes = 0;
	char* dest=unsafe_kbuf;
	int i=0,j=0;
	int active_cpus=0;
	unsigned long flags;

	/* First time Initialization */
	if (filp->private_data==NULL) {
		last_read=global->timestamp_sample; /* Dirty assignment */
		filp->private_data=&last_read;
	}

	/* reader CS */
	spin_lock_irqsave(&global->lock,flags);

	while (ktime_compare(last_read,global->timestamp_sample)==0) {
		global->reader_blocked=1;

		spin_unlock_irqrestore(&global->lock,flags);

		if (down_interruptible(&global->sem)) {
			global->reader_blocked=0;
			return -EINTR;
		}

		spin_lock_irqsave(&global->lock,flags);
	}

	/* Print stuff */
	get_online_cpus();
	for_each_online_cpu(i) {
		userspace_cpu_rdt_sample_t *cinfo = per_cpu_ptr(global->cpu_sample, i);

		if (cinfo->pid==-1)
			continue;

		active_cpus++;

		dest+=sprintf(dest,"%d,%d,%s,%d,0x%lx",cinfo->cpu,cinfo->pid,cinfo->comm,cinfo->clos_id,cinfo->cat_mask);

		for (j=0; j<(cinfo->nr_events+cinfo->nr_virtual_counters); j++)
			dest+=sprintf(dest,",%llu",cinfo->avg_values[j]);

		dest+=sprintf(dest,"\n");
	}
	put_online_cpus();

	/* Update read op ... */
	last_read=global->timestamp_sample;

	if (active_cpus==0)
		dest+=sprintf(dest,"Idle\n");

	spin_unlock_irqrestore(&global->lock,flags);

	nr_bytes=dest-unsafe_kbuf;

	if (copy_to_user(buf, unsafe_kbuf, nr_bytes) > 0)
		return -EFAULT;

	(*off) += nr_bytes;

	return nr_bytes;
}



/* Implementation of the monitoring_module_t interface */
monitoring_module_t intel_rdt_userspace_mm= {
	.info=INTEL_RDT_MODULE_STR,
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
