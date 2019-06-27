/*
 *  monitoring_mod.c
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#include <pmc/monitoring_mod.h>
#include <asm-generic/errno.h>
#include <asm/uaccess.h>
#include <pmc/pmu_config.h>
#include <pmc/hl_events.h>
#include <linux/vmalloc.h>
#include <linux/semaphore.h>
#include <linux/module.h>
#include <pmc/smart_power.h>
#include <linux/uaccess.h>

#ifdef DEBUG
static const char* sample_type_to_str[PMC_NR_SAMPLE_TYPES]= {"tick","ebs","exit","migration"};
#endif


/********************** DEFAULT MONITORING MODULE *********************************/
/* Definition of the callback functions for the default (dummy) monitoring module */
#ifdef CONFIG_PMC_PHI
static int dummy_enable_module(void)
{
	return 0;
}
static void dummy_disable_module(void) {}
static int dummy_on_read_config(char* str, unsigned int len)
{
	return 0;
}
static int dummy_on_write_config(const char *str, unsigned int len)
{
	return 0;
}
static int dummy_on_fork(unsigned long clone_flags, pmon_prof_t* prof)
{
	return 0;
}
static void dummy_on_exec(pmon_prof_t* prof) {}
static int dummy_on_new_sample(pmon_prof_t* prof,int cpu,pmc_sample_t* sample,int flags,void* data)
{
	return 0;
}
static void dummy_on_migrate(pmon_prof_t* prof, int prev_cpu, int new_cpu) {}
static void dummy_module_counter_usage(monitoring_module_counter_usage_t* usage)
{
	usage->hwpmc_mask=0x0;
	usage->nr_experiments=0;
	usage->nr_virtual_counters=0;
}
#else
core_experiment_set_t dummy_pmc_configuration[2];

static int dummy_enable_module(void)
{
	const char* pmcstr_cfg[]=
#if defined(CONFIG_PMC_CORE_2_DUO)
	{
		"pmc0,pmc1,pmc2,pmc3=0x82,umask3=0x2,pmc4=0x8,umask4=0x7,coretype=0",
		"pmc0,pmc2,coretype=1",
		"pmc0,pmc1,pmc3=0x2e,umask3=0x4f,pmc4=0x2e,umask4=0x41,coretype=1",
		NULL
	};
#elif defined(CONFIG_PMC_AMD)
	    {"pmc0=0xc0,pmc1=0x76",NULL
	    }; /* Just instr and cycles */
#elif defined(CONFIG_PMC_CORE_I7)
	    {"pmc0,pmc1,pmc2",NULL
	    };    /* Just the fixed-function PMCs */
#else /* ARM and ARM64 */
	    { "pmc1=0x8,pmc2=0x11",NULL
	    }; /* Just instr and cycles */
#endif
	if (configure_performance_counters_set(pmcstr_cfg, dummy_pmc_configuration, 2)) {
		printk("Can't configure global performance counters. This is too bad ... ");
		return -EINVAL;
	}
	printk(KERN_ALERT "Dummy monitoring module has been loaded successfuly\n" );
	return 0;
}

/* By default, the dummy monitoring module does not
 * take the control of performance counters
 */
static int dummy_enable_counters=0;

void dummy_module_counter_usage(monitoring_module_counter_usage_t* usage)
{
	if (dummy_enable_counters) {
#if defined(CONFIG_PMC_CORE_2_DUO)
		usage->hwpmc_mask=0x1b;
		usage->nr_experiments=2;
#elif defined(CONFIG_PMC_AMD)
		usage->hwpmc_mask=0x3;
		usage->nr_experiments=1;
#elif defined(CONFIG_PMC_CORE_I7)
		usage->hwpmc_mask=0x7;
		usage->nr_experiments=1;
#else
		usage->hwpmc_mask=0x6;
		usage->nr_experiments=1;
#endif
	} else {
		usage->hwpmc_mask=0x0;
		usage->nr_experiments=0;
	}

	usage->nr_virtual_counters=0;
}


static void dummy_disable_module(void)
{
	int i=0;
	for (i=0; i<2; i++)
		free_experiment_set(&dummy_pmc_configuration[i]);

	printk(KERN_ALERT "Dummy estimation module unloaded!!\n" );
}


static int dummy_on_read_config(char* str, unsigned int len)
{
	return sprintf(str,"dummy_enable_counters=%d\n",dummy_enable_counters);
}

static int dummy_on_write_config(const char *str, unsigned int len)
{
	int val;

	if (sscanf(str,"dummy_enable_counters %i",&val)==1 && (val==0 || val==1)) {
		dummy_enable_counters=val;
	}
	return len;
}

static int dummy_on_fork(unsigned long clone_flags, pmon_prof_t* prof)
{

	int i=0;

	/* If counter config was not inherited already ... */
	if (dummy_enable_counters && !prof->pmcs_config) {
		for(i=0; i<2; i++)
			clone_core_experiment_set_t(&prof->pmcs_multiplex_cfg[i],&dummy_pmc_configuration[i]);

		/* For now start with slow core events */
		prof->pmcs_config=get_cur_experiment_in_set(&prof->pmcs_multiplex_cfg[0]);
	}
	return 0;
}

static void dummy_on_exec(pmon_prof_t* prof) { }

static int dummy_on_new_sample(pmon_prof_t* prof,int cpu,pmc_sample_t* sample,int flags,void* data)
{

#ifdef DEBUG
	int remaining_pmcmask=0x1f;
	int j=0;
	/* Max supported counters 10 */
	char buf[100]="";
	char* dst=buf;
	int cnt=0;
	int cur_coretype=get_coretype_cpu(cpu);
	core_experiment_t* next;


	for(j=0; (j<MAX_PERFORMANCE_COUNTERS) && (remaining_pmcmask); j++) {
		if(sample->pmc_mask & (0x1<<j)) {
			dst+=sprintf(dst,"%llu ",sample->pmc_counts[cnt++]);
			remaining_pmcmask&=~(0x1<<j);
		}
		/* Print dash only if the particular pmcmask contains the pmc*/
		else if (remaining_pmcmask & (0x1<<j)) {
			dst+=sprintf(dst,"- ");
		}
	}

	printk(KERN_ALERT "%d %d %s %s\n", sample->coretype,sample->exp_idx, sample_type_to_str[sample->type], buf);

	/* Engage event multiplexing on tick only */
	if (flags & MM_TICK) {
		next=get_next_experiment_in_set(&prof->pmcs_multiplex_cfg[cur_coretype]);

		if (next && prof->pmcs_config!=next) {
			/* Clear all counters in the platform */
			mc_clear_all_platform_counters(get_pmu_props_coretype(cur_coretype));
			prof->pmcs_config=next;
			mc_restart_all_counters(prof->pmcs_config);
		}
	}
#endif
	return 0;
}


#endif  //CONFIG_PMC_PHI

static monitoring_module_t dummy_mm= {
	.info="This is just a proof of concept",
	.id=-1,
	.enable_module=dummy_enable_module,
	.disable_module=dummy_disable_module,
	.on_read_config=dummy_on_read_config,
	.on_write_config=dummy_on_write_config,
	.on_fork=dummy_on_fork,
	.on_exec=dummy_on_exec,
	.on_new_sample=dummy_on_new_sample,
	.module_counter_usage=dummy_module_counter_usage
};

/*****************************************************************************
 ********** IMPLEMENTATION OF THE MONITORING MODULE MANAGER ******************
 *****************************************************************************/

/*
 * Monitoring module manager
 */
static struct {
	struct list_head modules;
	monitoring_module_t* cur_module;
	int nr_modules;
	int nr_ids;
	struct proc_dir_entry *proc_entry;
	struct semaphore sem;
} mm_manager;


/* Make a given monitoring module the default one */
int activate_monitoring_module(int module_id);
/*
 * Reload a monitoring module.
 * This function may be necessary in the event the
 * system topology has changed (CPUs enabled/disabled)
 * and a given monitoring module must be notified of that change.
 */
int reinitialize_monitoring_module(int module_id);

/** /proc interface exported by the monitoring module manager **/
static ssize_t proc_mm_manager_write(struct file *filp, const char __user *buff, size_t len, loff_t *off);
static ssize_t proc_mm_manager_read(struct file *filp, char __user *buf, size_t len, loff_t *off);

static const struct file_operations proc_mm_manager_fops = {
	.read = proc_mm_manager_read,
	.write = proc_mm_manager_write,
	.open = proc_generic_open,
	.release = proc_generic_close,
};


/*
 * This is the place to include external declarations
 * for the various monitoring modules
 * available in PMCTrack
 */

/** @@ Architecture-specific monitoring modules @@ **/
#if defined(CONFIG_PMC_CORE_2_DUO) || defined(CONFIG_PMC_AMD)
extern monitoring_module_t ipc_sampling_sf_mm;
#elif defined(CONFIG_PMC_CORE_I7)
extern monitoring_module_t ipc_sampling_sf_mm;
extern monitoring_module_t intel_cmt_mm;
extern monitoring_module_t intel_rapl_mm;
#elif defined(CONFIG_PMC_ARM) || defined(CONFIG_PMC_ARM64)
extern monitoring_module_t ipc_sampling_sf_mm;
#ifndef ODROID
extern monitoring_module_t vexpress_sensors_mm;
#endif
#endif
#ifdef CONFIG_SMART_POWER
extern monitoring_module_t spower_mm;
#endif
#ifdef CONFIG_SMART_POWER_2
extern monitoring_module_t spower2_mm;
#endif


/* Init monitoring module manager */
int init_mm_manager(struct proc_dir_entry* pmc_dir)
{
	int ret=0;
	/* Init fields */
	mm_manager.nr_modules=0;
	mm_manager.cur_module=NULL;
	mm_manager.nr_ids=0;
	INIT_LIST_HEAD(&mm_manager.modules);
	sema_init(&mm_manager.sem,1);

	/* Create /proc/em_manager entry */
	mm_manager.proc_entry = proc_create_data( "mm_manager", 0666, pmc_dir, &proc_mm_manager_fops, NULL );

	if(mm_manager.proc_entry  == NULL) {
		printk(KERN_INFO "Couldn't create 'mm_manager' proc entry\n");
		return -ENOMEM;
	}

	/* Register dummy est. module */
	load_monitoring_module(&dummy_mm);

	if ((ret=activate_monitoring_module(0))) {
		remove_proc_entry("mm_manager", pmc_dir);
		printk(KERN_INFO "Couldn't activate dummy monitoring module\n");
		return ret;
	}

#ifdef CONFIG_SMART_POWER
	if ((ret=spower_register_driver())) {
		remove_proc_entry("mm_manager", pmc_dir);
		printk(KERN_INFO "Couldn't register Odroid Smart Power USB driver\n");
		return ret;
	}
#endif


	/*
	 * This is the place where the various
	 * monitoring modules available in PMCTrack
	 * are loaded (with load_monitoring_module())
	 */

	/** @@ Architecture-specific monitoring modules @@ **/
#if defined(CONFIG_PMC_CORE_2_DUO) || defined(CONFIG_PMC_AMD)
	load_monitoring_module(&ipc_sampling_sf_mm);
#elif defined(CONFIG_PMC_CORE_I7)
	load_monitoring_module(&ipc_sampling_sf_mm);
	load_monitoring_module(&intel_cmt_mm);
	load_monitoring_module(&intel_rapl_mm);
#elif defined(CONFIG_PMC_ARM) || defined(CONFIG_PMC_ARM64)
	load_monitoring_module(&ipc_sampling_sf_mm);
#ifndef ODROID
	load_monitoring_module(&vexpress_sensors_mm);
#endif
#endif
#ifdef CONFIG_SMART_POWER
	load_monitoring_module(&spower_mm);
#endif
#ifdef CONFIG_SMART_POWER_2
	load_monitoring_module(&spower2_mm);
#endif

	return 0;
}

/*
 * Free up resources allocated by the monitoring module manager.
 * This is a very dangerous operation: we must first unload estimation modules
 * to make sure nothing bad happens
 */
void destroy_mm_manager(struct proc_dir_entry* pmc_dir)
{
	/* Disable current modules */
	if (mm_manager.cur_module) {
		mm_manager.cur_module->disable_module();
		mm_manager.cur_module=NULL;
	}
#ifdef CONFIG_SMART_POWER
	spower_unregister_driver();
#endif
	remove_proc_entry("mm_manager", pmc_dir);
}

/* /proc/pmc/mm_manager read callback */
static ssize_t proc_mm_manager_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{

	char* kbuf=NULL;
	char* dest;
	monitoring_module_t* cur_entry=NULL;
	struct list_head* mylist=&mm_manager.modules;
	struct list_head *cur_node= mylist->next;
	ssize_t nbytes=0;

	if (*off>0)
		return 0;

	if (len>PAGE_SIZE)
		len=PAGE_SIZE;

	if ((kbuf=vmalloc(len))==NULL)
		return -ENOMEM;

	if (down_interruptible(&mm_manager.sem))
		return -EINTR;

	dest=kbuf;

	while (cur_node!= mylist) {
		cur_entry = list_entry(cur_node, monitoring_module_t, links);
		cur_node=cur_node->next;

		if (cur_entry==mm_manager.cur_module) {
			dest+=sprintf(dest,"[*] %d - %s\n",cur_entry->id,cur_entry->info);
		} else {
			dest+=sprintf(dest,"[ ] %d - %s\n",cur_entry->id,cur_entry->info);
		}
	}

	up(&mm_manager.sem);

	nbytes=dest-kbuf;

	if (copy_to_user(buf,kbuf,nbytes))
		nbytes=-EFAULT;
	else
		(*off)+=nbytes;

	vfree(kbuf);
	return nbytes;
}


#define MAX_STR_MM_MANAGER 50

/* /proc/pmc/mm_manager write callback */
static ssize_t proc_mm_manager_write(struct file *filp, const char __user *buff, size_t len, loff_t *off)
{
	char line[MAX_STR_MM_MANAGER+1]="";
	int val;
	int ret=0;
	int retval=0;

	if (len>MAX_STR_MM_MANAGER)
		return -ENOMEM;

	if (copy_from_user(line,buff,len))
		return -EINVAL;

	line[len]='\0';
	ret=len;

	if (down_interruptible(&mm_manager.sem))
		return -EINTR;

	if (sscanf(line,"activate %i",&val)==1) {
		if ((retval=activate_monitoring_module(val))<0) {
			printk(KERN_ALERT "Unable to activate the desired module");
			ret=retval;
			goto up_semaphore;
		}
	} else if (strncmp(line,"deactivate",10)==0) {
		if ((retval=activate_monitoring_module(-1))<0) {
			printk(KERN_ALERT "Unable to deactivate the desired module");
			ret=retval;
			goto up_semaphore;
		}
	} else if (sscanf(line,"reinitialize %i",&val)==1) {
		if ((retval=reinitialize_monitoring_module(val))) {
			printk(KERN_ALERT "Unable to reinitialize the desired module");
			ret=retval;
			goto up_semaphore;
		}
	}

up_semaphore:
	up(&mm_manager.sem);
	return ret;
}

/* Make a given monitoring module the default one */
int activate_monitoring_module(int module_id)
{
	monitoring_module_t* cur_entry=NULL;
	struct list_head *cur_node;
	struct list_head* mylist=&mm_manager.modules;
	int old_module_id=0;
	int error=0;

	/* All disabled so far */
	if (module_id < 0) {
		if (mm_manager.cur_module==NULL) {
			return -1;
		} else {
			old_module_id=mm_manager.cur_module->id;
			mm_manager.cur_module=NULL;
			return old_module_id;
		}
	} else if ((mm_manager.cur_module !=NULL) && (mm_manager.cur_module->id==module_id)) {
		/* Already enabled */
		return module_id;
	} else {

		cur_node = mylist->next; /* Retrieve first element */

		while (cur_node!= mylist) {
			/* cur_entry points to the structure in which the list is embedded */
			cur_entry = list_entry(cur_node, monitoring_module_t, links);
			cur_node=cur_node->next; /* Keep track of it before anything else */

			if (cur_entry->id==module_id) {
				/* Try to enable the new module
				  right before disabling the old one
				  and return an error if something went wrong
				  */
				if ((error=cur_entry->enable_module()))
					return error; //
				/* Disable curmodule if not null */
				if (mm_manager.cur_module)
					mm_manager.cur_module->disable_module();
				/* Update the pointer with the new module !! */
				mm_manager.cur_module=cur_entry;
				return module_id;
			}
		}

		/* Not possible */
		return -1;
	}

}

/* This is necessary to upgrade the configuration after a change in the system topology, and core type ... */
int reinitialize_monitoring_module(int module_id)
{
	monitoring_module_t* cur_entry=NULL;
	struct list_head *cur_node;
	struct list_head* mylist=&mm_manager.modules;


	/* All disabled so far */
	if (module_id < 0) {
		return -1;
	} else if ((mm_manager.cur_module !=NULL) && (mm_manager.cur_module->id==module_id)) {
		/* Already enabled */
		mm_manager.cur_module->enable_module();
		return module_id;
	} else {

		cur_node = mylist->next; /* First item of the list */

		while (cur_node!= mylist) {
			/* cur_entry points to the structure in which the list is embedded */
			cur_entry = list_entry(cur_node, monitoring_module_t, links);
			cur_node=cur_node->next; /* Keep track of it before anything else */

			if (cur_entry->id==module_id) {
				/* Call enable, but it means it must reinitialize */
				cur_entry->enable_module();
				return module_id;
			}
		}

		/* Not found */
		return -1;
	}
}

/* Arbitrary offset value */
#define SECURITY_IDS_OFFSET 999

/* Get security code associated with current monitoring module */
int current_monitoring_module_security_id(void)
{
	if (mm_manager.cur_module ==NULL)
		return -1;
	else
		return SECURITY_IDS_OFFSET+mm_manager.cur_module->id;
}

/* Get pointer to descriptor to current monitoring module */
monitoring_module_t* current_monitoring_module(void)
{
	return mm_manager.cur_module;
}

/* Load a specific monitoring module */
int load_monitoring_module(monitoring_module_t* module)
{
	int ret=-EINVAL;

	/* Check possible errors */
	if (module==NULL ||  module->id>0 ||
	    (module->probe_module && (ret=module->probe_module())) )
		return ret;

	module->id=mm_manager.nr_ids++;
	list_add_tail(&module->links,&mm_manager.modules);
	mm_manager.nr_modules++;
	return module->id;
}

/* Unload the monitoring module with associated ID */
int unload_monitoring_module(int module_id)
{
	monitoring_module_t* cur_entry=NULL;
	struct list_head *cur_node;
	struct list_head* mylist=&mm_manager.modules;

	/* Can't unload the module we're currently using */
	if ((module_id < 0) ||
	    ((mm_manager.cur_module !=NULL) && (mm_manager.cur_module->id==module_id))
	   ) {
		return -1;
	} else {

		cur_node = mylist->next; /* First item of the list */

		while (cur_node!= mylist) {
			/* cur_entry points to the structure in which the list is embedded */
			cur_entry = list_entry(cur_node, monitoring_module_t, links);

			if (cur_entry->id==module_id) {
				list_del(cur_node);
				mm_manager.nr_modules--;
				return module_id;
			}
			cur_node=cur_node->next;
		}

		/* Not possible */
		return -1;
	}
}

static inline int mod_task_is_curr(pmon_prof_t* prof)
{
	return prof->task_mod!=NULL && mm_manager.cur_module==prof->task_mod;
}

static inline int is_valid_monitoring_module(monitoring_module_t* module)
{
	monitoring_module_t* cur_entry=NULL;

	if (!module)
		return 0;

	list_for_each_entry(cur_entry, &mm_manager.modules, links) {
		if (cur_entry==module)
			return 1;
	}

	return 0;
}

/**
 * Wrapper functions for every operation
 * in the monitoring_module_t interface
 */

int mm_on_read_config(char* str, unsigned int len)
{
	if (mm_manager.cur_module && mm_manager.cur_module->on_read_config)
		return mm_manager.cur_module->on_read_config(str,len);
	return 0;
}


int mm_on_write_config(const char *str, unsigned int len)
{
	if (mm_manager.cur_module && mm_manager.cur_module->on_write_config)
		return mm_manager.cur_module->on_write_config(str,len);
	return 0;
}

int mm_on_fork(unsigned long clone_flags, pmon_prof_t* prof)
{
	if (mm_manager.cur_module && mm_manager.cur_module->on_fork)
		return mm_manager.cur_module->on_fork(clone_flags, prof);
	return 0;
}

void mm_on_exec(pmon_prof_t* prof)
{
	if ( mod_task_is_curr(prof) && mm_manager.cur_module->on_exec)
		mm_manager.cur_module->on_exec(prof);
}

int mm_on_new_sample(pmon_prof_t* prof,int cpu,pmc_sample_t* sample,int flags,void* data)
{
	if ( mod_task_is_curr(prof) && mm_manager.cur_module->on_new_sample)
		return mm_manager.cur_module->on_new_sample(prof,cpu,sample,flags,data);
	return 0;
}

void mm_on_tick(pmon_prof_t* prof,int cpu)
{
	if ( mod_task_is_curr(prof) && mm_manager.cur_module->on_tick)
		mm_manager.cur_module->on_tick(prof,cpu);
}

void mm_on_migrate(pmon_prof_t* prof, int prev_cpu, int new_cpu)
{
	if ( mod_task_is_curr(prof) && mm_manager.cur_module->on_migrate)
		mm_manager.cur_module->on_migrate(prof,prev_cpu,new_cpu);
}

void mm_on_exit(pmon_prof_t* prof)
{
	if ( mod_task_is_curr(prof) && mm_manager.cur_module->on_exit)
		return mm_manager.cur_module->on_exit(prof);
}

void mm_on_free_task(pmon_prof_t* prof)
{
	if ( mod_task_is_curr(prof) && mm_manager.cur_module->on_free_task)
		mm_manager.cur_module->on_free_task(prof);
	else if (prof && prof->monitoring_mod_priv_data)	{
		/* Free up memory if the monitoring module
			was changed in between */
		monitoring_module_t* task_mod=prof->task_mod;
		if (is_valid_monitoring_module(task_mod) && task_mod->on_free_task)
			task_mod->on_free_task(prof);
	}
}

void mm_on_switch_in(pmon_prof_t* prof)
{
	if ( mod_task_is_curr(prof) && mm_manager.cur_module->on_switch_in)
		mm_manager.cur_module->on_switch_in(prof);
}

void mm_on_switch_out(pmon_prof_t* prof)
{
	if ( mod_task_is_curr(prof) && mm_manager.cur_module->on_switch_out)
		mm_manager.cur_module->on_switch_out(prof);
}

int mm_get_current_metric_value(pmon_prof_t* prof, int key, uint64_t* value)
{
	int ret=0;
	if ( mod_task_is_curr(prof) && mm_manager.cur_module->get_current_metric_value) {
		ret=mm_manager.cur_module->get_current_metric_value(prof,key,value);
#ifdef DEBUG
		if (key>7)
			trace_printk("Requested key %d -> value=%llu ,status=%d\n",key,*value,ret);
#endif
		return ret;
	} else
		return -1;
}

void mm_module_counter_usage(monitoring_module_counter_usage_t* usage)
{
	if (mm_manager.cur_module && mm_manager.cur_module->module_counter_usage)
		return mm_manager.cur_module->module_counter_usage(usage);
	else {
		/* Return default info (no counter is used) */
		usage->hwpmc_mask=0;
		usage->nr_virtual_counters=0;
		usage->nr_experiments=0; /* No event multiplexing either */
	}
}

int mm_on_syswide_start_monitor(int cpu, unsigned int virtual_mask)
{
	if (mm_manager.cur_module && mm_manager.cur_module->on_syswide_start_monitor)
		return mm_manager.cur_module->on_syswide_start_monitor(cpu,virtual_mask);
	else
		return -ENOSYS;
}

void mm_on_syswide_stop_monitor(int cpu, unsigned int virtual_mask)
{
	if (mm_manager.cur_module && mm_manager.cur_module->on_syswide_stop_monitor)
		mm_manager.cur_module->on_syswide_stop_monitor(cpu,virtual_mask);
}

void mm_on_syswide_refresh_monitor(int cpu, unsigned int virtual_mask)
{
	if (mm_manager.cur_module && mm_manager.cur_module->on_syswide_refresh_monitor)
		mm_manager.cur_module->on_syswide_refresh_monitor(cpu,virtual_mask);
}

void mm_on_syswide_dump_virtual_counters(int cpu, unsigned int virtual_mask,pmc_sample_t* sample)
{
	if (mm_manager.cur_module && mm_manager.cur_module->on_syswide_dump_virtual_counters)
		mm_manager.cur_module->on_syswide_dump_virtual_counters(cpu,virtual_mask,sample);
}


int proc_generic_open(struct inode *inode, struct file *filp)
{
	try_module_get(THIS_MODULE);
	return 0;
}

int proc_generic_close(struct inode *inode, struct file *filp)
{
	module_put(THIS_MODULE);
	return 0;
}
