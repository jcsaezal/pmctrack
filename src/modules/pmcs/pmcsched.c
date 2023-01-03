/*
 *  pmcsched.c
 *
 *  This is PMCSched headquarters
 *
 *  Copyright (c) 2022  Carlos Bilbao <cbilbao@ucm.es> and Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#include <pmc/hl_events.h>
#include <pmc/mc_experiments.h>
#include <pmc/data_str/sized_list.h>
#include <pmc/pmu_config.h>
#include <pmc/intel_rdt.h>
#include <linux/ftrace.h>
#include <linux/kthread.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
#include <linux/sched.h>
#else
#include <uapi/linux/sched/types.h>
#endif

#include <linux/cpumask.h>
#include <linux/limits.h>
#include <linux/ptrace.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/irqflags.h>
#include <linux/cpu.h> /* To inhibit CPU hot-plug and async profiling */
#include <linux/types.h>
#include <linux/smp.h>
#include <linux/hrtimer.h>
#include <linux/vmalloc.h>
#include <linux/ktime.h>
#include <linux/kallsyms.h>
//#include <asm/syscall.h>
#include <linux/syscalls.h>
#include <uapi/linux/mempolicy.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <pmc/intel_ehfi.h>

#define SCHED_PROTOTYPE_STRING "PMCSched"


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
#include <linux/sched/task.h>      /* for get_task_struct()/put_task_struct() */
#include <linux/sched/signal.h>    /* For send_sig_info()                     */
#endif

#include <pmc/pmcsched.h>

#define DEBUG
#define PMCSCHED_DEBUG

/*##### Non-plugin-specific GLOBAL VARIABLES #### */

/* Turn on via /proc/pmc/config for informative dmesg logs */
unsigned int active_scheduler_verbose = 0;

sched_thread_group_t sched_thread_groups[MAX_GROUPS_PLATFORM];

sched_thread_group_t* pmcsched_gbl=&sched_thread_groups[0];

pmcsched_config_t pmcsched_config;

/* Global variables to keep track of Intel CAT and CMT global parameters      */

intel_cmt_support_t pmcs_cmt_support;
intel_cat_support_t pmcs_cat_support;

static spinlock_t schedulers_lock;

static sized_list_t schedulers;

/* Currently active scheduler (default dummy) */
sched_ops_t * active_scheduler = NULL;

unsigned char pmcsched_rdt_capable=0;
unsigned char pmcsched_ehfi_capable=0;

static void sched_timer_periodic(struct timer_list* timer);
static int sched_kthread_periodic(void *arg);

/* Private proc entry */
extern struct proc_dir_entry *pmc_dir;
struct proc_dir_entry *pmcsched_proc=NULL;
struct proc_dir_entry *schedctl_proc=NULL;

static ssize_t pmcsched_proc_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);
static ssize_t pmcsched_proc_read(struct file *filp, char __user *buf, size_t len, loff_t *off);

static pmctrack_proc_ops_t fops = {
	.PMCT_PROC_READ = pmcsched_proc_read,
	.PMCT_PROC_WRITE = pmcsched_proc_write,
	.PMCT_PROC_LSEEK = default_llseek
};

/* Schedctl specific definitions */

static int  schedctl_proc_open(struct inode *inode, struct file *filp);
static int  schedctl_proc_release(struct inode *inode, struct file *filp);
static ssize_t schedctl_proc_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static int  schedctl_proc_mmap(struct file *filp, struct vm_area_struct *vma);

static pmctrack_proc_ops_t ctl_fops = {
	.PMCT_PROC_OPEN = schedctl_proc_open,
	.PMCT_PROC_READ =  schedctl_proc_read,
	.PMCT_PROC_RELEASE = schedctl_proc_release,
	.PMCT_PROC_MMAP=  schedctl_proc_mmap,
	.PMCT_PROC_LSEEK = default_llseek
};


static void schedctl_mmap_open(struct vm_area_struct *vma);
static void schedctl_mmap_close(struct vm_area_struct *vma);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,4,0)
static vm_fault_t schedctl_nopage( struct vm_fault *vmf);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
static int schedctl_nopage( struct vm_fault *vmf);
#endif

/* Instantiation of the VMOPS interface */
struct vm_operations_struct mmap_schedctl_ops = {
	.open =    schedctl_mmap_open,
	.close =   schedctl_mmap_close,
	.fault =   schedctl_nopage,
};

typedef struct {
	schedctl_t *schedctl;	/* Pointer to the page shared between user and kernel space */
	int nr_references;
} schedctl_handler_t;

/*
 * PMCSched general locking routines
 */

sched_thread_group_t* get_cpu_group_sched(int cpu)
{
	if (active_scheduler->flags & PMCSCHED_GLOBAL_LOCK) {
		return pmcsched_gbl;
	} else {
		int idx=get_group_id_cpu(cpu);
		return &sched_thread_groups[idx];
	}
}

sched_thread_group_t* get_group_sched_by_id(int id)
{
	return &sched_thread_groups[id];
}

sched_thread_group_t* get_cur_group_sched(void)
{
	return get_cpu_group_sched(smp_processor_id());
}

static inline spinlock_t* pmcsched_acquire_lock(unsigned long* flags)
{
	unsigned long local_flags=0;
	unsigned long sched_flags=active_scheduler->flags;
	spinlock_t* lock=NULL;

	if (sched_flags & PMCSCHED_GLOBAL_LOCK) {
		lock=&pmcsched_gbl->lock;
	} else if (sched_flags & PMCSCHED_CPUGROUP_LOCK) {
		lock=&get_cur_group_sched()->lock;
	}

	if (lock)
		spin_lock_irqsave(lock,local_flags);

	(*flags)=local_flags;

	return lock;
}

static inline  void pmcsched_release_lock(spinlock_t* lock, unsigned long flags)
{
	if (lock)
		spin_unlock_irqrestore(lock,flags);
}

/* Use a tasklet to send the signal */
//#define TASKLET_SIGNALS_PMCSCHED
#ifdef TASKLET_SIGNALS_PMCSCHED

void __send_signal_tasklet(unsigned long arg)
{
	struct task_struct *p;
	pmcsched_thread_data_t *t;
	int ret = 0, sig_num;
	struct tasklet_struct* tasklet = (struct tasklet_struct*) arg;

	/* Retrieve the task struct from the tasklet data */
	t = container_of(tasklet,pmcsched_thread_data_t,signal_tasklet);
	p = t->prof->this_tsk;
	sig_num = t->pending_signal;

	/* Check we can send the signal without problems */
	if (!spin_trylock(&p->sighand->siglock)) {
		trace_printk("Task %d had sighand lock acquired\n",p->pid);
		return;
	} else spin_unlock(&p->sighand->siglock);

	ret = send_sig_info(sig_num, SEND_SIG_PRIV,  p);

	if (ret < 0) {
		trace_printk("Error sending signal to PID %d (return %d).\n",
		             p->pid,ret);
		return;
	}

	if (active_scheduler_verbose) {
		switch(sig_num) {
		case SIGSTOP:
			/* Atomic operations shouldn't have lock acquired */
			trace_printk("%s: wait signal sent to thread %d,state %ld\n",
			             __func__,p->pid,get_task_state(p));
			break;
		case SIGCONT:
			trace_printk("%s: wake signal sent to thread %d in state %ld\n",
			             __func__,p->pid,get_task_state(p));
			break;
		case SIGKILL:
			trace_printk("%s: kill signal sent to thread %d state %ld\n",
			             __func__,p->pid,get_task_state(p));
			break;
		default:
			trace_printk("Signal %d was sent to thread %d state %ld\n",
			             sig_num,p->pid,get_task_state(p));
		}
	}

	return;
}

int __send_signal(int sig_num, pmcsched_thread_data_t *t)
{
	t->pending_signal = sig_num;
	tasklet_schedule(&t->signal_tasklet);

	return 1;
}

#else

int __send_signal(int sig_num, pmcsched_thread_data_t *t)
{
	struct task_struct *p;
	int ret = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,14,0)
	const char* trace_fmt="%s: %s signal sent to thread %d,state %ld\n";
	const char* trace_fmt2="Signal %d sent to thread %d,state %ld\n";
#else
	const char* trace_fmt="%s: %s signal sent to thread %d,state %u\n";
	const char* trace_fmt2="Signal %d sent to thread %d,state %u\n";
#endif
	p = t->prof->this_tsk;

	/* Check we can send the signal without problems */
	if (!spin_trylock(&p->sighand->siglock)) {
		trace_printk("Task %d had sighand lock acquired\n",p->pid);
		return -1;
	} else spin_unlock(&p->sighand->siglock);

	ret = send_sig_info(sig_num, SEND_SIG_PRIV,  p);

	if (ret < 0) {
		trace_printk("Error sending signal to PID %d (return %d).\n",
		             p->pid,ret);
		return -1;
	}

	if (active_scheduler_verbose) {
		switch(sig_num) {
		case SIGSTOP:
			/* Atomic operations shouldn't have lock acquired */
			trace_printk(trace_fmt,"wait",
			             __func__,p->pid,get_task_state(p));
			break;
		case SIGCONT:
			trace_printk(trace_fmt,"wake",
			             __func__,p->pid,get_task_state(p));
			break;
		case SIGKILL:
			trace_printk(trace_fmt,"kill",
			             __func__,p->pid,get_task_state(p));
			break;
		default:
			trace_printk(trace_fmt2,
			             sig_num,p->pid,get_task_state(p));
		}
	}

	return 1;
}

#endif

/* Forward declaration */
void __add_to_pending_signals(sized_list_t* list, int mode);
void check_pending_signals_group(sched_thread_group_t* group);

#define ON_ENABLE_MODULE 0
#define ON_SCHEDULER_CHANGED 1
#define ON_DISABLE_MODULE 2

__attribute__((used))
static int init_sched_thread_groups(int mode)
{
	int cpu_group_count;
	cpu_group_t* cpu_groups=get_platform_cpu_groups(&cpu_group_count);
	sched_thread_group_t* cur_group;
	int i=0,j=0;
	unsigned long flags;

#ifdef PRINT_GROUP_MASKS
	char buf[256];
#endif

	for (i=0; i<cpu_group_count; i++) {

		cur_group=&sched_thread_groups[i];

		/* Deal with special case ON_DISABLE_MODULE */
		if (mode==ON_DISABLE_MODULE) {
			spin_lock_irqsave(&cur_group->lock,flags);

			if (sized_list_length(&cur_group->stopped_threads)
			    || sized_list_length(&cur_group->active_threads)) {

				cur_group->disabling_mode = 1;

				trace_printk("Shouldn't change modules with pending threads...\n");
				trace_printk("Trying to kill them... No guarantee of success.\n");

				__add_to_pending_signals(&cur_group->stopped_threads, KILL_PENDING);
				__add_to_pending_signals(&cur_group->active_threads,  KILL_PENDING);
			}

			check_pending_signals_group(cur_group);

			spin_unlock_irqrestore(&cur_group->lock,flags);
		}

		cur_group->cpu_group=&cpu_groups[i];

		cur_group->profiled=NULL;

		cur_group->disabling_mode = 0;

		cur_group->scheduling_mode = NORMAL;

		spin_lock_init(&cur_group->lock);

		/* Initialize lists */
		init_sized_list (&cur_group->active_threads,
		                 offsetof(pmcsched_thread_data_t,link_active_threads_gbl));

		init_sized_list (&cur_group->active_apps,
		                 offsetof(app_t_pmcsched,link_active_apps));

		init_sized_list (&cur_group->stopped_apps,
		                 offsetof(app_t_pmcsched,link_stopped_apps));

		init_sized_list (&cur_group->stopped_threads,
		                 offsetof(pmcsched_thread_data_t,link_stopped_threads_gbl));

		init_sized_list (&cur_group->pending_signals_list,
		                 offsetof(pmcsched_thread_data_t,signal_links));

		init_sized_list (&cur_group->pending_profile_list,
		                 offsetof(pmcsched_thread_data_t,profile_links));

		init_sized_list (&cur_group->profiler_stopped_list,
		                 offsetof(pmcsched_thread_data_t,profiler_stopped_links));

		init_sized_list (&cur_group->all_threads,
		                 offsetof(pmcsched_thread_data_t,link_all_threads));

		init_sized_list (&cur_group->migration_list,
		                 offsetof(pmcsched_thread_data_t,global_migration_link));

		init_sized_list (&cur_group->clos_to_update,
		                 offsetof(struct clos_cpu,link));

		/* It will be manually activated if timer callback not implemented */
		cur_group->activate_kthread=0;
		cur_group->timer_period=PMCSCHED_PERIOD_DEFAULT;
		cur_group->last_migration_thread_activation=jiffies;

		switch (mode) {
		case ON_ENABLE_MODULE:

			/* Create thread asleep */
			cur_group->kthread = kthread_create(sched_kthread_periodic, NULL, "scheduler_kthread_%i",i);

			if (!cur_group->kthread) {
				for (j=0; j<i; j++) {
					del_timer_sync(&sched_thread_groups[j].timer);
					kthread_stop(sched_thread_groups[j].kthread);
				}
				return -ENOMEM;
			}

			/* Clear private data */
			for (j=0; j<NUM_SCHEDULERS; j++)
				cur_group->private_data[j]=NULL;

			/* Set affinity correctly */
			set_cpus_allowed_ptr(cur_group->kthread,&cur_group->cpu_group->shared_cpu_map);
#ifdef PRINT_GROUP_MASKS
			cpumap_print_to_pagebuf(1,buf,&cur_group->cpu_group->shared_cpu_map);
			trace_printk("%s\n",buf);
#endif

			/* Launch it for the first time for proper initialization */
			wake_up_process(cur_group->kthread);

			timer_setup(&cur_group->timer,sched_timer_periodic,0);
			cur_group->timer.expires=jiffies+DELAY_KTHREAD+i; /* add offset to reduce lock contention */
			/* TODO: Potential race condition if timer starts too soon before initialization */
			add_timer_on(&cur_group->timer,cur_group->cpu_group->cpus[1]);

			break;
		case ON_SCHEDULER_CHANGED:
			break;
		case ON_DISABLE_MODULE:
			del_timer_sync(&cur_group->timer);
			if (cur_group->kthread)
				kthread_stop(cur_group->kthread);
			break;
		}


	}
	return 0;
}

/* =============  API for sched_app_t ... =============  */
__attribute__((hot))
static inline sched_app_t* create_sched_app(struct task_struct* p)
{
	sched_app_t* sched_app=NULL;
	app_t_pmcsched* app=NULL;
	int i = 0;
	int j=0;
	int nr_cpu_groups=0;
	app_t* app_cache;

	get_platform_cpu_groups(&nr_cpu_groups);

	sched_app = kmalloc(sizeof(sched_app_t),GFP_KERNEL);

	if (!sched_app)
		return NULL;

	atomic_set(&sched_app->ref_counter,1);
	rwlock_init(&sched_app->app_lock);

	sched_app->is_multithreaded=0;

	for (i=0; i<nr_cpu_groups; i++) {
		app=&sched_app->pmc_sched_apps[i];
		app_cache=&app->app_cache;

		/* Initialize the various fields... */
		init_sized_list (&app->app_active_threads,
		                 offsetof(pmcsched_thread_data_t,link_active_threads_apps));

		init_sized_list (&app->app_stopped_threads,
		                 offsetof(pmcsched_thread_data_t,link_stopped_threads_apps));

		init_sized_list (&app_cache->app_active_threads,
		                 offsetof(pmcsched_thread_data_t,
		                          link_active_threads_apps_cache));
		app->state = NO_QUEUE;

		app->sa=sched_app;

		/* For LFOC (extracted from dyn_cache_part_mm.c:create_app_t()) */
		app_cache->cat_partition=NULL;
		app_cache->last_partition=-1; /* No "old" partition */
		app_cache->app_id=-1;
		app_cache->type=CACHE_CLASS_UNKOWN;
		app_cache->critical_point=1; /* one way */

		app_cache->curve[0]=0;

		for (j=1; j<MAX_CACHE_WAYS+1; j++) {
			/*To avoid divide by zero */
			app_cache->curve[j]=1;
		}

#ifdef CONFIG_X86
		app_cache->sprops=&foo_properties;
#else
		app_cache->sprops=NULL;
#endif
		app_cache->static_type=CACHE_CLASS_UNKOWN;
		app_cache->force_class=0;
		app_cache->process=p;
		app_cache->profiling_scheduled = 0;
		app_cache->next_periodic_profile=0; /* Disabled by default */
		/* Initialization of master thread */
		app_cache->master_thread=NULL; /* To be activated on active */

		app_cache->app_cmt_data.rmid=0;
		app_cache->app_cmt_data.cos_id=0;
		strcpy(app_cache->app_comm,"unknown");
	}

	return sched_app;
}

static inline void get_sched_app(sched_app_t* app)
{
	atomic_inc(&app->ref_counter);
}

static inline int get_refs_sched_app(sched_app_t* app)
{
	return atomic_read(&app->ref_counter);
}

static inline void put_sched_app(sched_app_t* app)
{
	app_t_pmcsched* legacy_app=&app->pmc_sched_apps[0];

	if (atomic_dec_and_test(&app->ref_counter)) {

		/* If we invoked this function because the developer forced the module
		 * switching we do not need to do this as the list were already
		 * initialized again. */
		if (legacy_app->state == STOP_QUEUE &&
		    sized_list_length(&pmcsched_gbl->stopped_apps)) {
			remove_sized_list(&pmcsched_gbl->stopped_apps,legacy_app);
		} else if (legacy_app->state == ACTIVE_QUEUE &&
		           sized_list_length(&pmcsched_gbl->active_apps)) {
			remove_sized_list(&pmcsched_gbl->active_apps,legacy_app);
		}

		kfree(app);
	}
}

/* PMCTrack Counters */

static void pmcsched_module_counter_usage(monitoring_module_counter_usage_t* usage)
{
	pmcsched_counter_config_t* cc=active_scheduler->counter_config;
	int i;

	if (cc) {
		usage->hwpmc_mask=cc->counter_usage.hwpmc_mask;
		usage->nr_virtual_counters=cc->counter_usage.nr_virtual_counters;
		usage->nr_experiments=cc->counter_usage.nr_experiments;

		for (i=0; i<cc->counter_usage.nr_virtual_counters; i++)
			usage->vcounter_desc[i]=cc->counter_usage.vcounter_desc[i];

	} else {
		usage->hwpmc_mask=0;
		usage->nr_virtual_counters=0;
		usage->nr_experiments=0;
	}
}

/* -----------------------------------------------------------------------------
   * @mode can be used to specify the type of signal you want to be pending. The
   * Scheduler can use this if the decision was not taken by the plugin. For
   * example, if the module is forcedly disabled with pending threads.
   -----------------------------------------------------------------------------
*/
void __add_to_pending_signals(sized_list_t* list, int mode)
{
	pmcsched_thread_data_t *elem;
	sched_thread_group_t* cur_group=get_cur_group_sched();
	int already_on_list;
	int task, N;

	/* ----------------------------------------------------------------------
	 * When a thread is just activated or the deactivated the processor is in
	 * a Context Change state that does not allow interruptions. Hence, this
	 * should be postponed and checked by the periodic k-thread.
	 * ----------------------------------------------------------------------
	 */

	if (!sized_list_length(list)) return;

	elem = head_sized_list(list);

	N = sized_list_length(list);

	for (task = 0 ; task < N && elem != NULL; task++) {

		already_on_list = (signal_pending_th(elem)) ? 1: 0;

		if (mode) {
			/* Protection against kill -s 9 <thread> */
			if (mode == KILL_PENDING && elem->state ==TASK_KILLED) goto next_l;
			pmcsched_set_state(elem, mode);
		}

		if (already_on_list) goto next_l;

		if (active_scheduler_verbose) {
			trace_printk("Task %d added to list of pending signals (state %d).\n",
			             elem->prof->this_tsk->pid, elem->state);
		}

		insert_sized_list_tail(&cur_group->pending_signals_list,elem);
next_l:
		elem = next_sized_list(list,elem);
	}
}

/*
  -------------------------------------------------------------------------
  * If the plugin has added a thread to the list of threads that need to be
  * profiled this will be checked and managed here.
  * THIS FUNCTION SHOULD BE CALLED WITH LOCK ACQUIRED.
  -------------------------------------------------------------------------
*/
void check_pending_profiling_group(sched_thread_group_t* group)
{
	pmcsched_thread_data_t *elem, *aux;
	int task = 0, jumped = 0, selected = 0, state, N;

	/* A thread is already being profiled */
	if (group->scheduling_mode == PROFILING)
		return;

	if (!active_scheduler->profile_thread && !active_scheduler->on_new_sample)
		return;

	/* We need to find now one thread to profile */
	if (!sized_list_length(&group->pending_profile_list)) return;

	elem = head_sized_list(&group->pending_profile_list);

	N = sized_list_length(&group->pending_profile_list);

	for (; task < N && elem!=NULL; task++) {

		if (!profiling_pending(elem) || dead_task(elem) || !elem->runnable) {
			aux = elem;
			elem = next_sized_list(&group->pending_profile_list,elem);

			if (dead_task(aux)) {
				remove_sized_list(&group->pending_profile_list,aux);
			} else {
				jumped = 1;
			}
			continue;
		}

		group->profiled = elem;
		group->scheduling_mode = PROFILING;

		if (unlikely(!active_scheduler->light_weight_scenario)) {
			trace_printk("Active plugin has no sampling mode prepared.\n");
		} else {
			active_scheduler->light_weight_scenario(group->profiled);
		}

		remove_sized_list(&group->pending_profile_list,elem);

		/* Profiling function will be called when a new sample is collected */

		if (active_scheduler_verbose) {
			trace_printk("Task %d removed from pending profiles.\n",
			             elem->prof->this_tsk->pid);
		}

		selected = 1;

		break;
	}

	/* All the threads have been profiled */
	if (!jumped && task == sized_list_length(&group->pending_profile_list)) {

		init_sized_list (&group->pending_profile_list,
		                 offsetof(pmcsched_thread_data_t,profile_links));
	}

	/* Most likely the thread is still running since the list of pending signals
	* is managed after the profiling & profiling is usually done at activation.
	*/
	state = get_task_state(group->profiled->prof->this_tsk);

	if (selected && unlikely(state != TASK_RUNNING && state!= TASK_WAKING)) {

		/* Check we can send the signal without problems */
		if (!spin_trylock(&group->profiled->prof->this_tsk->sighand->siglock)) {
			trace_printk("ERROR: Thread %d for profile not awaken\n",elem->prof->this_tsk->pid);
			return;
		} else spin_unlock(&group->profiled->prof->this_tsk->sighand->siglock);

		aux->signal_sent = 1;

		if (__send_signal(SIGCONT,group->profiled) < 0) {
			trace_printk("%s: Signal failed! This should stop.\n", __func__);
		}
	}
}

void check_pending_profiling(void)
{
	check_pending_profiling_group(get_cur_group_sched());
}

/*  -----------------------------------------------------------------------
     * This is an auxiliary function to send the same signal to all the
     * threads stored in the sized list. Signal inferred by the tasks
     * pending states.
     * THIS FUNCTION SHOULD BE CALLED WITH LOCK ACQUIRED.
    -----------------------------------------------------------------------
 */
void check_pending_signals_group(sched_thread_group_t* group)
{
	pmcsched_thread_data_t *elem, *aux;
	int task = 0, ret = 1, N;

	if (!sized_list_length(&group->pending_signals_list)) return;

	elem = head_sized_list(&group->pending_signals_list);
	N = sized_list_length(&group->pending_signals_list);

	for (; task < N && elem != NULL; task++) {
		if (!signal_pending_th(elem) ||
		    (elem->signal_sent && elem->state != KILL_PENDING)) {
			aux = elem;
			elem = next_sized_list(&group->pending_signals_list,elem);
			remove_sized_list(&group->pending_signals_list,aux);
			continue;
		}

		if (group->scheduling_mode == PROFILING) {

			/* In profiling only one thread (the profiled) can run! */
			if (group->profiled != elem && elem->state != STOP_PENDING) goto next_elem;

			if (group->profiled == elem && (elem->state == STOP_PENDING ||
			                                elem->state == KILL_PENDING)) {
				if (active_scheduler_verbose) {
					trace_printk("Thread %d should not be stopped during its profiling.\n",
					             elem->prof->this_tsk->pid);
				}
				goto next_elem;
			}
		}

		if (active_scheduler_verbose) {
			trace_printk("Checking signals of %d.\n",elem->prof->this_tsk->pid);
		}

		/* Check we can send the signal without problems */
		if (!spin_trylock(&elem->prof->this_tsk->sighand->siglock)) {
			trace_printk("Thread %d had sighand lock acquired\n",elem->prof->this_tsk->pid);
			goto next_elem;
		} else {
			spin_unlock(&elem->prof->this_tsk->sighand->siglock);
		}

		aux = elem;
		elem = next_sized_list(&group->pending_signals_list,elem);
		aux->signal_sent = 1;

		switch(aux->state) {
		case ACTIVE_PENDING:
			ret = __send_signal(SIGCONT,aux);
			break;
		case STOP_PENDING:
			ret = __send_signal(SIGSTOP,aux);
			break;
		case KILL_PENDING:
			ret = __send_signal(SIGKILL,aux);
			break;
		default:
			asm ("nop");
		}

		if (ret > 0 && !group->disabling_mode) {
			if (active_scheduler_verbose) {
				trace_printk("Task %d will be removed from pending signals.\n",
				             aux->prof->this_tsk->pid);
			}
			remove_sized_list(&group->pending_signals_list,aux);
		} else if (ret < 0) {
			aux->signal_sent = 0;
			trace_printk("%s: Signal failed! This might need to stop.\n", __func__);
		}

		if (0) {
next_elem:
			elem = next_sized_list(&group->pending_signals_list,elem);
		}
	}
}

void check_pending_signals(void)
{
	check_pending_signals_group(get_cur_group_sched());
}

static void __periodic_scheduling(void)
{
	unsigned long flags = 0;
	unsigned int task = 0;
	pmcsched_thread_data_t *elem;
	spinlock_t* lock=NULL;
	int N;
	sched_thread_group_t* cur_group=get_cur_group_sched();

	/*---------------------------------------------------------------------
	* Threads cannot be migrated in the periodic call as this is done in
	* atomic context. The plugins should store in these lists the threads
	* to migrate. Thread t will be masked to t->mask and so the plugin
	* should update it.
	* ---------------------------------------------------------------------
	*/
	sized_list_t migration_list;

	init_sized_list(&migration_list,
	                offsetof(pmcsched_thread_data_t, migration_links));

	if (cur_group->scheduling_mode == PROFILING) return;

	/*  -------------------------------------------------------------------
	*   Scheduling decisions
	*   The migration list will retrieve the tasks that need to be
	*   masked to a certain CPU (mask updated) and signal_threads
	*   will be the list of threads with a signal pending.
	*   -------------------------------------------------------------------
	*/

	/* Used to inhibit CPU hot-plugs operations */
	get_online_cpus();

	/* Do not grab lock in CPUGROUP_LOCK MODE */
	if (!(active_scheduler->flags & PMCSCHED_CPUGROUP_LOCK))
		lock=pmcsched_acquire_lock(&flags);

	if (active_scheduler->sched_kthread_periodic)
		active_scheduler->sched_kthread_periodic(&migration_list);

	/* Check if a thread needs to be labeled. This might have some overhead */
	check_pending_profiling();

	/*Check signals that are yet to be managed.*/
	check_pending_signals();

	pmcsched_release_lock(lock,flags);

	N = sized_list_length(&migration_list);

	for (elem = head_sized_list(&migration_list); elem != NULL && task < N ; task++) {

		if (set_cpus_allowed_ptr(elem->prof->this_tsk, elem->mask) < 0) {
			trace_printk("%s: set_cpus_allowed_ptr failed.\n",__func__);
			break;
		}
		elem = next_sized_list(&migration_list,elem);
	}

	put_online_cpus();
}


static void sched_timer_periodic(struct timer_list* timer)
{

	sched_thread_group_t* cur_group=get_cur_group_sched();
	spinlock_t* lock;
	unsigned long flags;
	unsigned long period=0;


	/* Direct call */
	if (active_scheduler->sched_timer_periodic) {
		lock=pmcsched_acquire_lock(&flags);
		active_scheduler->sched_timer_periodic();
		pmcsched_release_lock(lock,flags);
		/**
		 * Right after releasing the lock and with interrupts enabled
		 * update clos if necessary in this group
		 **/
		if (!is_empty_sized_list(&cur_group->clos_to_update)) {
			update_clos_app_unlocked(&cur_group->clos_to_update);
			lock=pmcsched_acquire_lock(&flags);
			release_clos_cpu_list(&cur_group->clos_to_update);
			pmcsched_release_lock(lock,flags);
		}
	}  else {
		/*
		 * Kthread is automatically activated if timer_periodic callback not
		 * implemented
		 */
		cur_group->activate_kthread=1;
	}

	/* Indirect call */
	if (active_scheduler->sched_kthread_periodic &&  cur_group->activate_kthread) {
		wake_up_process(cur_group->kthread);
		/* Clear flag for next time */
		cur_group->activate_kthread=0;
	}

	switch (cur_group->timer_period) {
	case PMCSCHED_PERIOD_DISABLE:
		period=-1;
		break;
	case PMCSCHED_PERIOD_DEFAULT:
		/* In PROFILING mode, the kthread will be invoked 20 times more!! */
		period=cur_group->scheduling_mode == PROFILING? DELAY_KTHREAD_PROFILING:DELAY_KTHREAD;
		break;
	default: /* Custom period */
		period=cur_group->timer_period;
	}


	if (period>0)
		mod_timer(&cur_group->timer,jiffies+period);

}

static int sched_kthread_periodic(void *arg)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,9,0)
	static const struct sched_param param =  {
		.sched_priority = MAX_USER_RT_PRIO/2,
	};

	/* Set real time priority to kick in no matter what...*/
	sched_setscheduler(current, SCHED_FIFO, &param);
#else
	sched_set_fifo(current);
#endif

	trace_printk("Periodic k-thread invoked.\n");

	while (!kthread_should_stop()) {

		set_current_state(TASK_INTERRUPTIBLE);
#ifdef OLD_STUFF
		/* This is used to approximate the time devoted to each profiling */
		//__compute_profiling_time(0);

		/* In PROFILING mode, the kthread will be invoked 20 times more!! */
		if (get_cur_group_sched()->scheduling_mode == PROFILING) {
			schedule_timeout(DELAY_KTHREAD_PROFILING); /* Sleep */
		} else {
			schedule_timeout(DELAY_KTHREAD);
		}
#else
		schedule();
#endif

		/* Avoid NULL pointers somewhere */
		//__purge_lists();

		__periodic_scheduling();
	}

	trace_printk("Periodic k-thread exited.\n");

	return 0;
}

static inline void init_sched_list(void)
{
	int i;
	struct sched_ops* cur_sched;

	spin_lock_init(&schedulers_lock);

	init_sized_list (&schedulers,
	                 offsetof(sched_ops_t, link_schedulers));

	/* Add only supported schedulers to the list */
	for (i=0; i<NUM_SCHEDULERS; i++) {
		cur_sched=available_schedulers[i];

		if (!cur_sched->probe_plugin || cur_sched->probe_plugin())
			insert_sized_list_tail(&schedulers,cur_sched);
	}

	active_scheduler = &dummy_plugin;
}

/* ipc_sampling estimation module */
static int pmcsched_enable_module(void)
{
	int retval;
	unsigned char cmt_cfg=0;
	unsigned char cat_cfg=0;
	unsigned char sched_structures_ready=0;

	pmcsched_rdt_capable=!qos_extensions_probe();
	pmcsched_ehfi_capable=!intel_ehfi_initialize();

	populate_topology_structures();

	/* Init configurable parameters */
	pmcsched_config.sched_period_normal=HZ/8;  /* Denoted in ticks (~125 ms) */
	pmcsched_config.sched_period_profiling=HZ/10;  /* Denoted in ticks (~125 ms) */
	pmcsched_config.rmid_allocation_policy = RMID_FIFO;

	/* Initialize list of schedulers and set default active scheduler */
	init_sched_list();

	/* For IPI-based update operations of cache-partition migrations */
	initialize_clos_cpu_pool();

	pmcsched_proc= proc_create("sched", 0666, pmc_dir, &fops);

	if (!pmcsched_proc) {
		retval=-ENOMEM;
		goto init_error_path;
	}

	schedctl_proc= proc_create("schedctl", 0666, pmc_dir, &ctl_fops);

	if (!schedctl_proc) {
		retval=-ENOMEM;
		goto init_error_path;
	}

	if (pmcsched_rdt_capable) {
		if ((retval = intel_cmt_initialize(&pmcs_cmt_support))) {
			trace_printk("CMT initialization failed.\n");
			goto init_error_path;
		}

		cmt_cfg=1;

		if ((retval = intel_cat_initialize(&pmcs_cat_support))) {
			trace_printk("CAT initialization failed.\n");
			goto init_error_path;
		}

		cat_cfg=1;
	}

	/*
	 * Maybe on future versions, add power consumption monitoring edp
	 *  if ((retval=edp_initialize_environment())) { (...)
	 */

	/* (Init global structures for scheduling */
	if ((retval=init_sched_thread_groups(ON_ENABLE_MODULE))) {
		trace_printk("Sched thread groups initialization failed.\n");
		goto init_error_path;
	}

	sched_structures_ready=1;

	trace_printk("%s loaded successfully\n",SCHED_PROTOTYPE_STRING);
	return 0;
init_error_path:
	if (cat_cfg)
		intel_cat_release(&pmcs_cat_support);
	if (cmt_cfg)
		intel_cmt_release(&pmcs_cmt_support);
	if (sched_structures_ready)
		init_sched_thread_groups(ON_DISABLE_MODULE);
	if (schedctl_proc)
		remove_proc_entry("schedctl", pmc_dir);
	if (pmcsched_proc)
		remove_proc_entry("sched", pmc_dir);
	return retval;
}

static void pmcsched_disable_module(void)
{
	/* Previous plugin might want to free up memory ...*/
	if (active_scheduler->destroy_plugin) {
		active_scheduler->destroy_plugin();
	}

	if (pmcsched_proc) {
		remove_proc_entry("sched", pmc_dir);
		pmcsched_proc = NULL;
	}

	if (schedctl_proc) {
		remove_proc_entry("schedctl", pmc_dir);
		schedctl_proc = NULL;
	}

	if (pmcsched_rdt_capable) {
		intel_cmt_release(&pmcs_cmt_support);
		intel_cat_release(&pmcs_cat_support);
	}

	if (pmcsched_ehfi_capable)
		intel_ehfi_release();

	init_sched_thread_groups(ON_DISABLE_MODULE);

	trace_printk("%s module unloaded!!\n",SCHED_PROTOTYPE_STRING);
}


#ifdef CONFIG_X86
typedef struct cpuinfo_x86 cpu_struct_t;

static inline cpu_struct_t* get_cpu_structure_by_idx(unsigned int idx)
{
	return &cpu_data(idx);
}

static inline cpu_struct_t* get_boot_cpu_structure(void)
{
	return &boot_cpu_data;
}

static inline unsigned int __get_socket_id(cpu_struct_t* cdata)
{
	return cdata->phys_proc_id;
}

static inline int __get_cpu_group_id(cpu_struct_t *cdata)
{
	unsigned char ccxs_present;

	if (get_nr_coretypes()>1)
		return get_coretype_cpu(cdata->cpu_index);

	ccxs_present=(cdata->x86_vendor == X86_VENDOR_AMD) && (cdata->x86 == 0x17); // && cdata->x86_model <= 0x1F);

	if (ccxs_present) {
		return cdata->apicid>>3;
	} else
		return cdata->logical_die_id;

}

void __printk_cpu_info(int cpu)
{
	char buf[256];
	if (!cpu_online(cpu)) {
		trace_printk("**CPU %i is offline**\n", cpu);
	} else {
		cpu_struct_t *c;
		cpu_group_t* group=get_cpu_group(cpu);
		c = get_cpu_structure_by_idx(cpu);
		trace_printk("==CPU %i==\n", cpu);
#define EXTENDED_TOPOLOGY_INFO
#ifdef EXTENDED_TOPOLOGY_INFO
		trace_printk("phys_proc_id=%d\tlogical_proc_id=%d\n",
		             c->phys_proc_id,c->logical_proc_id);
		trace_printk("cpu_core_id=%d\tcpu_die_id=%d\n",
		             c->cpu_core_id,c->cpu_die_id);
		trace_printk("logical_die_id=%d\tccpu_index=%d\n",
		             c->logical_die_id,c->cpu_index);
		trace_printk("x86=%d\tccx_id=%d\n",
		             c->x86_model,c->apicid>>3);
#endif
		if (group) {
			cpumap_print_to_pagebuf(1,buf,&group->online_cpu_map);
			trace_printk("group_id=%d\tsocket_id=%d\tcpus_group=%s\n",
			             group->group_id,group->socket_id,buf);
			trace_printk("group_nr_cpus=%d\tgroup_nr_online_cpus=%d\n",
			             group->nr_cpus,group->nr_online_cpus);
		}
		trace_printk("==========\n");
	}
}
#else
typedef struct cpu_topology cpu_struct_t;

static inline cpu_struct_t* get_cpu_structure_by_idx(unsigned int idx)
{
	return &cpu_topology[idx];
}

static inline cpu_struct_t* get_boot_cpu_structure(void)
{
	return get_cpu_structure_by_idx(0);
}

/* Just minimal support for UMA multicores */
static inline unsigned int __get_socket_id(cpu_struct_t* cdata)
{
	return 0; /* cdata->package_id (number of cluster in arm ) */
}

/**
 * The core_id field of struct cpu_topology does not
 * indicate the cpu number but the core local id within
 * the big or little cluster
*/
static inline int __get_cpu_number(cpu_struct_t *cdata)
{
	unsigned int cpu_number=cdata-get_cpu_structure_by_idx(0);
	return cpu_number;
}

static inline int __get_cpu_group_id(cpu_struct_t *cdata)
{

	if (get_nr_coretypes()>1)
		return get_coretype_cpu(__get_cpu_number(cdata));
	else
		return 0;
}


void __printk_cpu_info(int cpu)
{
	char buf[256];
	cpu_struct_t *c;
	cpu_group_t* group;

	if (!cpu_online(cpu)) {
		trace_printk("**CPU %i is offline**\n", cpu);
		return;
	}

	group=get_cpu_group(cpu);
	c = get_cpu_structure_by_idx(cpu);
	trace_printk("==CPU %i==\n", cpu);
	trace_printk("core_id=%d\n",
	             __get_cpu_number(c));
	if (group) {
		cpumap_print_to_pagebuf(1,buf,&group->online_cpu_map);
		trace_printk("group_id=%d\tsocket_id=%d\tcpus_group=%s\n",
		             group->group_id,group->socket_id,buf);
		trace_printk("group_nr_cpus=%d\tgroup_nr_online_cpus=%d\n",
		             group->nr_cpus,group->nr_online_cpus);
	}
	trace_printk("==========\n");

}
#endif

void printk_cpu_info(int cpu)
{

	int nr_cpus=num_online_cpus();
	int nr_cores;
	int i=0;
#ifdef CONFIG_X86
	struct cpuinfo_x86 *c=&cpu_data(smp_processor_id());
	nr_cores=c->booted_cores;
#else
	nr_cores=nr_cpus;
#endif

	trace_printk("Number of physical cores %d\n", nr_cores);
	trace_printk("Number of cpus (total threads in the system) %d\n", nr_cpus);

	if (cpu==-1)
		for (i=0; i<nr_cpus; i++)
			__printk_cpu_info(i);
	else
		__printk_cpu_info(cpu);
}

static ssize_t pmcsched_proc_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
	const int bufsiz=100;
	char kbuf[100];
	int val,i=0, found = 0, pending_threads, retval, ret;
	struct sched_ops *aux, *next, *old;
	unsigned long flags;
	int nr_schedulers=sized_list_length(&schedulers);

	if (len>=bufsiz)
		return -ENOSPC;

	if (copy_from_user(kbuf,buf,len))
		return -EFAULT;

	kbuf[len]='\0';

	spin_lock_irqsave(&schedulers_lock,flags);

	if (sscanf(kbuf,"scheduler %d",&val) && val<nr_schedulers) {
		/* Extra check for specific case (legacy schedulers) */
		if (active_scheduler->flags & PMCSCHED_GLOBAL_LOCK) {
			pending_threads = sized_list_length(
			                      &pmcsched_gbl->stopped_threads)
			                  + sized_list_length(&pmcsched_gbl->active_threads);

			if (pending_threads) {
				trace_printk("Can't switch scheduler, %d pending threads.\n",
				             pending_threads);
				goto end_w;
			}
		}

		for (aux = head_sized_list(&schedulers); !found && aux != NULL; i++,aux =  next) {
			next = next_sized_list(&schedulers,aux);

			if (i == val) {

				found = 1;

				/* Previous plugin might want to free memory...*/
				if (active_scheduler->destroy_plugin) {
					active_scheduler->destroy_plugin();
				}

				trace_printk("SCHEDULER SET (%s)...\n",aux->description);
				old=active_scheduler;
				active_scheduler = aux;

				/* We don't want to step on NULL pointers       */
				BUG_ON(unlikely(
				           !active_scheduler->on_active_thread));
				BUG_ON(unlikely(
				           !active_scheduler->on_inactive_thread));

				/* (Plugins do not always need to implement a profiling) */

				/* Lists need to be reinitialized */
				init_sched_thread_groups(ON_SCHEDULER_CHANGED);

				if (active_scheduler->init_plugin) {
					if ((retval = active_scheduler->init_plugin()) < 0) {
						len = retval;
						trace_printk("init_plugin() failed, reverting to old active plugin");
						active_scheduler=old;
						/* The assumption is that initialization works in this case */
						if (active_scheduler->init_plugin)
							active_scheduler->init_plugin();

						goto end_w;
					}
				}

#ifndef CONFIG_PMC_CORE_2_DUO
				if (pmcsched_rdt_capable) {
					intel_cmt_release(&pmcs_cmt_support);

					if ((retval = intel_cmt_initialize(&pmcs_cmt_support))) {
						trace_printk("CMT initialization failed.\n");
						BUG();
					}
				}
#endif
			}
		}

		if (!found) {
			trace_printk("%s: Sorry, scheduler not found...\n",
			             __func__);
		}
	} else if (sscanf(kbuf,"verbose %d",&val) && (val == 1 || val == 0)) {
		active_scheduler_verbose = val;
	} else if (sscanf(kbuf,"sched_period_normal %d",&val)==1) {
		if (val<=0)
			return -EINVAL;

		pmcsched_config.sched_period_normal=msecs_to_jiffies(val);

	} else if (sscanf(kbuf,"sched_period_profiling %d",&val)==1) {
		if (val<=0)
			return -EINVAL;

		pmcsched_config.sched_period_profiling=msecs_to_jiffies(val);

	} else if (sscanf(kbuf,"rmid_alloc_policy %i",&val)==1 && val>=0
	           && val<NR_RMID_ALLOC_POLICIES) {
		pmcsched_config.rmid_allocation_policy=val;
		set_cmt_policy(pmcsched_config.rmid_allocation_policy);
	} else if (strncmp(kbuf,"plugin ",7)==0) { /* Delegate on the plugin                */
		if (active_scheduler->on_write_plugin) {
			if ((ret = active_scheduler->on_write_plugin(kbuf+7)) < 0) {
				len = ret;
				goto end_w;
			}
		} else if (active_scheduler_verbose) {
			trace_printk("Current plugin doesn't have a write func.\n");
		}
	} else if (sscanf(kbuf,"topo %d",&val)==1) {
		printk_cpu_info(val);
	} else {
		/* Otherwise assume is a plugin parameter */
		if  (active_scheduler->on_write_plugin) {
			ret=active_scheduler->on_write_plugin(kbuf);
			if (ret<0)
				len=ret;
		} else
			len = -EINVAL;
		goto end_w;
	}

end_w:

	spin_unlock_irqrestore(&schedulers_lock,flags);

	return len;
}

static ssize_t pmcsched_proc_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
	int nr_bytes = 0;
	char *dest,*kbuf;
	int err,i;
	unsigned long flags;
	sched_ops_t *elem;

	if (*off>0)
		return 0;

	kbuf=vmalloc(PAGE_SIZE);

	if (!kbuf)
		return -ENOMEM;

	dest=kbuf;

	spin_lock_irqsave(&schedulers_lock,flags);

	dest+=sprintf(dest,
	              "---------------------------------------------------------------------\n");
	dest+=sprintf(dest,"Verbose option for kernel log currently ");

	if (active_scheduler_verbose) dest+=sprintf(dest,"ON.\n");
	else dest+=sprintf(dest,"OFF.\n");

	dest+=sprintf(dest,"The developed schedulers in PMCSched are:\n");

	for (elem = head_sized_list(&schedulers), i=0; elem != NULL;
	     ++i,elem = next_sized_list(&schedulers,elem)) {
		if (elem == active_scheduler)
			dest+=sprintf(dest,"[*] %d - %s",i,elem->description);
		else
			dest+=sprintf(dest,"[ ] %d - %s",i,elem->description);

		dest+=sprintf(dest,"\n");
	}

	dest+=sprintf(dest,
	              "- To change the active scheduler echo 'scheduler <number>'\n");
	dest+=sprintf(dest,
	              "- For switching the verbose option (kernel log) echo 'verbose <1/0>'\n");
	dest+=sprintf(dest,
	              "---------------------------------------------------------------------\n");

	/* PMCSched Configurable parameters */
	dest+=sprintf(dest,"sched_period_normal=%ums\n",
	              jiffies_to_msecs(pmcsched_config.sched_period_normal));
	dest+=sprintf(dest,"sched_period_profiling=%ums\n",
	              jiffies_to_msecs(pmcsched_config.sched_period_profiling));
	if (pmcsched_rdt_capable) {
		dest+=sprintf(dest,"rmid_alloc_policy=%d (%s)\n",
		              pmcsched_config.rmid_allocation_policy,
		              rmid_allocation_policy_str[pmcsched_config.rmid_allocation_policy]);
		dest+=sprintf(dest,"cat_nr_cos_available=%d\n",
		              pmcs_cat_support.cat_nr_cos_available);
		dest+=sprintf(dest,"cat_cbm_length=%d\n",
		              pmcs_cat_support.cat_cbm_length);
	}
	dest+=sprintf(dest,"verbose=%d\n",active_scheduler_verbose);

	if (active_scheduler->on_read_plugin) {

		dest+=sprintf(dest,"Plugin %d specific information:\n",
		              active_scheduler->policy);
		dest += active_scheduler->on_read_plugin(dest);
	}

	spin_unlock_irqrestore(&schedulers_lock,flags);

	nr_bytes=dest-kbuf;

	if (copy_to_user(buf, kbuf, nr_bytes) > 0) {
		err=-EFAULT;
		goto err_config_read;
	}

	(*off) += nr_bytes;

	vfree(kbuf);
	return nr_bytes;
err_config_read:
	vfree(kbuf);
	return err;
}

static int schedctl_proc_mmap(struct file *filp, struct vm_area_struct *vma)
{
	vma->vm_ops = &mmap_schedctl_ops;	/* Set up callbacks for this entry*/
	vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP |VM_MAYREAD|VM_MAYSHARE|VM_READ|VM_SHARED; /* Initialize flags*/
	vma->vm_private_data = filp->private_data; /* Make sure the private data matches the file's private data*/
	schedctl_mmap_open(vma);	/* Force open shared memory region */
	//printk("schedctl_mmap: Invoking mmap\n");
	return 0;
}

static int schedctl_proc_open(struct inode *inode, struct file *filp)
{
	schedctl_handler_t *handler =NULL;
	pmon_prof_t* prof=get_prof(current);
	pmcsched_thread_data_t* pdata;

	if (!prof || ! prof->monitoring_mod_priv_data)
		return -ENOTSUPP;

	pdata = prof->monitoring_mod_priv_data;

	/* Allocate memory for the schedctl handler */
	if ((handler=kmalloc(sizeof(schedctl_handler_t),  GFP_KERNEL))==NULL) {
		printk(KERN_ALERT "Can't allocate memory for schedctl handler");
		return -ENOMEM;
	}

	/* Per-thread field was initialized already */
	if (pdata->schedctl) {
		handler->schedctl = pdata->schedctl;
#ifdef SCHEDCTL_DEBUG
		trace_printk("Reusing schedctl structure=0x%p\n", pdata->schedctl);
#endif
		goto skip_allocation;
	}

	/* Allocate zero-filled page for schedctl */
	if ((handler->schedctl = (schedctl_t*)get_zeroed_page(GFP_KERNEL))==NULL) {
		printk(KERN_ALERT "Can't allocate memory for schedctl handler");
		kfree(handler);
		return -ENOMEM;
	}

	pdata->schedctl=handler->schedctl;
#ifdef SCHEDCTL_DEBUG
	trace_printk("Schedctl kernel pointer=0x%p\n",pdata->schedctl);
#endif

	/* Default initialization */
skip_allocation:
	pdata->schedctl->sc_coretype=get_coretype_cpu(smp_processor_id());
	pdata->schedctl->sc_spinning=0;
	pdata->schedctl->sc_prio=0;
	pdata->schedctl->sc_nfc=0;
	pdata->schedctl->sc_sf=200;

	filp->private_data = handler;
	return 0;
}


int schedctl_proc_release(struct inode *inode, struct file *filp)
{
	schedctl_handler_t *handler = filp->private_data;
	pmon_prof_t* prof=get_prof(current);
	pmcsched_thread_data_t* pdata;

	if (!prof || ! prof->monitoring_mod_priv_data)
		return 0; /* Not really a problem */

	pdata = prof->monitoring_mod_priv_data;

	/**
	 *  we do not free up the per-thread schedctl structure
	 * until global free up due to efficiency reasons.
	 * Essentially the runtime system would reuse this stuff.
	 *
	 **/
	if (handler) {
		handler->schedctl=NULL;
		kfree(handler);
	}

	filp->private_data = NULL;
	return 0;
}

static ssize_t schedctl_proc_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
	pmon_prof_t* prof=get_prof(current);
	pmcsched_thread_data_t* pdata;
	schedctl_t* schedctl;
	char kbuf[100]="";
	char* dest=kbuf;
	int nr_bytes=0;

	if (!prof || ! prof->monitoring_mod_priv_data)
		return -EINVAL;

	pdata=prof->monitoring_mod_priv_data;
	schedctl=pdata->schedctl;

	if (! schedctl)
		return -ENOENT;

#if defined(CONFIG_PMC_PERF_X86) || defined(CONFIG_PMC_CORE_I7)
	if (len==sizeof(unsigned int)) {
		/* Store TD prediction on shared memory region */
		struct ehfi_thread_info tinfo;
		int err;

		/* Use class zero as last class by default */
		if ((err=get_current_ehfi_info(0,&tinfo)))
			return err;

		schedctl->sc_sf=tinfo.perf_ratio;
		return 0;
	}
#endif

	/* Normal read operation */
	if (*off>0)
		return 0;

	dest+=sprintf(dest,"coretype=%u\n",schedctl->sc_coretype);
	dest+=sprintf(dest,"spinning=%u\n",schedctl->sc_spinning);
	dest+=sprintf(dest,"nfc=%d\n",schedctl->sc_nfc);
	dest+=sprintf(dest,"prio=%d\n",schedctl->sc_prio);
	dest+=sprintf(dest,"sf=%d\n",schedctl->sc_sf);

	nr_bytes=dest-kbuf;

	if (copy_to_user(buf,kbuf,nr_bytes))
		return -EFAULT;

	(*off)+=nr_bytes;
	return nr_bytes;
}

/* Open shared memory region */
void schedctl_mmap_open(struct vm_area_struct *vma)
{
	schedctl_handler_t *handler =  (schedctl_handler_t*)vma->vm_private_data;
	handler->nr_references++;
}

void schedctl_mmap_close(struct vm_area_struct *vma)
{
	schedctl_handler_t *handler =  (schedctl_handler_t*)vma->vm_private_data;
	handler->nr_references--;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,4,0)
static vm_fault_t schedctl_nopage( struct vm_fault *vmf)
{
	struct vm_area_struct *vma=vmf->vma;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
static int schedctl_nopage( struct vm_fault *vmf) {
#endif
	schedctl_handler_t *handler;
	pgoff_t offset = vmf->pgoff;

	if (offset > vma->vm_end) {
		printk(KERN_ALERT "invalid address\n");
		return VM_FAULT_SIGBUS;
	}

	handler = (schedctl_handler_t *)vma->vm_private_data;
	/* Make sure data is there */
	if(!handler->schedctl) {
		printk(KERN_ALERT "No shared data in here\n");
		return VM_FAULT_SIGBUS;
	}

	/* Get physical address from virtual page */
	vmf->page = virt_to_page(handler->schedctl);
	/* Bring page to main memory */
	get_page(vmf->page);

	return 0;
}

noinline void trace_mt_path(pmon_prof_t* par_prof,pmcsched_thread_data_t* par_data) {
	asm(" ");
	//trace_printk("%p %p\n",par_prof,par_data);
}

static int pmcsched_on_fork(unsigned long clone_flags, pmon_prof_t* prof) {
	pmcsched_thread_data_t*  data;
	int j=0,k=0;
	unsigned int nr_coretypes=active_scheduler->flags&PMCSCHED_AMP_SCHED?get_nr_coretypes():1;
	int error=0;
	pmon_prof_t *pprof = get_prof(current);

	if (prof->monitoring_mod_priv_data!=NULL)
		return 0;

	data=kmalloc(sizeof (pmcsched_thread_data_t), GFP_KERNEL);

	if (data == NULL)
		return -ENOMEM;

	if (active_scheduler->counter_config) {
		pmcsched_counter_config_t* cc=active_scheduler->counter_config;
		/* Force PMC configuration if active ... */

		if (cc->pmcs_descr) {
			for (k=0; k<nr_coretypes; k++) {

				/**
				 * Two scenarios exist to avoid cloning performance counters
				 *  - User imposed counters
				 *  - A thread from a multithreaded program is created
				 *    (mod_alloc_per_thread_data already clones counters)
				 */
				if (!prof->pmcs_config)
					error=clone_core_experiment_set_t(&prof->pmcs_multiplex_cfg[k],
					                                  &cc->pmcs_descr[k],prof->this_tsk);
				if (error)
					goto out_err;

				if (cc->metric_descr[k])
					/* Clone Metrics from the corresponding metric vectors */
					clone_metric_experiment_set_t(&data->metric_set[k],cc->metric_descr[k]);
			}

			/* Enable counters thread. On AMPs start with small core events */
			prof->pmcs_config=get_cur_experiment_in_set(
			                      &prof->pmcs_multiplex_cfg[0]);

			prof->profiling_mode=cc->profiling_mode;
		}
	}


	data->runnable          = 0;
	data->actually_runnable = 0;
	pmcsched_set_state(data, NO_QUEUE);
	data->mask           = kmalloc_node(cpumask_size(),GFP_KERNEL,NUMA_NO_NODE);
	data->prof           = prof;

	data->signal_sent    = 0;

	data->stopped_during_profiling = 0;

	data->memory_profile = NO_LABEL_P;
	data->schedctl = NULL;
	data->cur_group=NULL;
	data->migration_data.state=MIGRATION_COMPLETED;

	data->force_per_thread = 0;

#ifdef TASKLET_SIGNALS_PMCSCHED
	/* Init tasklet, pass data = signal_tasklet */
	tasklet_init(&data->signal_tasklet,
	             __send_signal_tasklet,
	             (unsigned long) &data->signal_tasklet);
#endif

	/* Store pointer active scheduler */
	data->scheduler=active_scheduler;

	data->t_flags=0;

	if (is_new_thread(clone_flags) && get_prof_enabled(pprof)) {

		pmcsched_thread_data_t* par_data = pprof->monitoring_mod_priv_data;

		trace_mt_path(pprof,par_data);

		/* Check if this application should be treated by the scheduler as
		 * a bunch of unrelated threads
		 */
		if (par_data->force_per_thread) {
			data->force_per_thread = 1;
			goto single_threaded_processing;
		}

		get_sched_app(par_data->sched_app);
		data->sched_app=par_data->sched_app;

		/* Update multithreaded indicator */
		data->sched_app->is_multithreaded=1;

		/* Update legacy app pointer
		    Legacy old pointer is also used to keep track
		    of the application's unique RMID
		*/
		data->app=&par_data->sched_app->pmc_sched_apps[0];

		/* For Intel CMT/CAT */
		data->first_time=0;
		/* Point to per-application data */
		data->cmt_data=&data->app->app_cache.app_cmt_data;
		/* Make a copy structure for monitoring bandwidth and cache usage */
		data->cmt_monitoring_data=*data->cmt_data;
		if (pmcsched_rdt_capable)
			use_rmid(data->cmt_data->rmid); /* Increase ref counter */

		if (active_scheduler_verbose) {
			trace_printk("Thread %d assigned RMID %d.\n",
			             prof->this_tsk->pid, par_data->cmt_data->rmid);
		}
	} else {
single_threaded_processing:
		/* Create application structure for the thread */
		data->sched_app = create_sched_app(prof->this_tsk);

		if (!data->sched_app)
			panic("Sched module was not able to allocate memory for app structure");

		/* Update legacy app pointer */
		data->app=&data->sched_app->pmc_sched_apps[0];

		initialize_cmt_thread_struct(&data->app->app_cache.app_cmt_data);
		/* Point to per-application data */
		data->cmt_data=&data->app->app_cache.app_cmt_data;
		/* Make a copy structure for monitoring bandwidth and cache usage */
		data->cmt_monitoring_data=*data->cmt_data;



		data->first_time=1;
		data->cmt_data->rmid=0; /* It will be assigned in the first context switch */
	}

	//data->last_bw_update = ktime_get(); /* Time since last BW update */
	data->security_id = current_monitoring_module_security_id();
	/* Initialize data field in proc structure */
	prof->monitoring_mod_priv_data = data;

	/* Invoke only fork for the active scheduler */
	if (active_scheduler->on_fork_thread &&
	    (error=active_scheduler->on_fork_thread(data, !is_new_thread(clone_flags))) )
		goto out_err;

	return 0;

out_err:
	for (j=0; j<k; j++)
		free_experiment_set(&prof->pmcs_multiplex_cfg[j]);
	return error;
}

static void pmcsched_on_exec(pmon_prof_t* prof) {
	if (active_scheduler->on_exec_thread) {
		active_scheduler->on_exec_thread(prof);
	}
}

/* This should be changed.
 * Each scheduling plugin should take care of collecting its own metrics
 */
enum metric_indices {
	IPC_MET=0,
	RPKI_MET,
	MPKI_MET,
	MPKC_MET,
	STALLS_L2_MET,
	NR_METRICS,
};

#define L2_LINES 5

static int
pmcsched_on_new_sample(pmon_prof_t* prof,
                       int cpu,pmc_sample_t* sample,int flags,void* data) {
	pmcsched_thread_data_t* sfdata = prof->monitoring_mod_priv_data;
	int warm_up=0;
	unsigned long lock_flags;
	spinlock_t* lock;
	sched_thread_group_t* group=get_cur_group_sched();

	if (sfdata == NULL)
		return 0;

	/* Two ways to develop plugin profiling: on_new_sample_plugin and
	   profile_thread. */
	if (active_scheduler->on_new_sample) {
		/* The active plugin is in charge of returning the sampling mode to normal
		   with get_cur_group_sched()->scheduling_mode = NORMAL;
		*/
		if ((warm_up =
		         active_scheduler->on_new_sample(prof,cpu,sample,flags,data)) > 0) {
			if (active_scheduler_verbose) {
				trace_printk("Warm-up sample from thread %d collected.\n",
				             prof->this_tsk->pid);
			}
		} else if (warm_up != -1) {
			group->scheduling_mode = NORMAL;
		}
	} else if (active_scheduler->profile_thread && group->scheduling_mode == PROFILING
	           && group->profiled->prof == prof) {

		lock=pmcsched_acquire_lock(&lock_flags);

		/* The Plugin could now use the values collected and label the thread
		  the plugin might choose to let this one go, to have a warm-up period.
		*/

		if ((warm_up = active_scheduler->profile_thread(group->profiled))) {
			if (active_scheduler_verbose) {
				trace_printk("Warm-up sample from thread %d collected.\n",
				             prof->this_tsk->pid);
			}
		} else {
			__activate_stopped_by_profiling(&group->profiler_stopped_list,
			                                &group->pending_signals_list);

			group->scheduling_mode = NORMAL;
		}

		pmcsched_release_lock(lock,lock_flags);
	}

	return 0;
}

noinline void trace_pmcsched_migrate_thread(struct task_struct* p, pmon_prof_t* prof, int prev_cpu, int new_cpu) {
	asm(" ");
}

static void
pmcsched_on_migrate(pmon_prof_t* prof, int prev_cpu, int new_cpu) {
	trace_pmcsched_migrate_thread(prof->this_tsk,prof,prev_cpu,new_cpu);

	if (active_scheduler->on_migrate_thread && prof->monitoring_mod_priv_data) {
		pmcsched_thread_data_t* t=
		    (pmcsched_thread_data_t*)prof->monitoring_mod_priv_data;
		active_scheduler->on_migrate_thread(t,prev_cpu,new_cpu);
	}
}

static void pmcsched_on_exit(pmon_prof_t* prof) {
	int was_first_time=0;
	pmcsched_thread_data_t* t=
	    (pmcsched_thread_data_t*)prof->monitoring_mod_priv_data;
	unsigned long flags;
	spinlock_t* lock;

	if (!t || !get_prof_enabled(prof)) return;

	if (pmcsched_rdt_capable) {
		was_first_time = __give_rmid(t);
	} else {
		if (t->first_time &&
		    t->security_id == current_monitoring_module_security_id()) {

			t->first_time = 0;

			was_first_time = 1;
		}
	}

	if (t->state == KILL_PENDING) {
		pmcsched_set_state(t, TASK_KILLED);
		if (active_scheduler_verbose) {
			trace_printk("Thread %d was successfully killed.\n",prof->this_tsk->pid);
		}
	} else if (!was_first_time && !dead_task(t)) {

		lock=pmcsched_acquire_lock(&flags);
		if (active_scheduler->on_exit_thread)
			active_scheduler->on_exit_thread(t);
		pmcsched_set_state(t, TASK_KILLED);
		pmcsched_release_lock(lock,flags);
	}

#ifndef CONFIG_PMC_CORE_2_DUO
	if (pmcsched_rdt_capable && get_prof_enabled(prof)) {
		put_rmid(t->cmt_data->rmid);
	}
#endif
}

static void pmcsched_on_free_task(pmon_prof_t* prof) {
	pmcsched_thread_data_t* data=
	    (pmcsched_thread_data_t*)prof->monitoring_mod_priv_data;
	unsigned char is_last_thread=0;

	if (!prof || !data)
		return;

	/* Release application structure (memory will be freed if necessary)  */
	if (data->security_id==current_monitoring_module_security_id() &&
	    data->sched_app) {
		is_last_thread = (atomic_read(&(data->sched_app->ref_counter))==1);

		/* Invoke on free thread only for the same scheduler that was used on fork() */
		if (data->scheduler->on_free_thread)
			data->scheduler->on_free_thread(data, is_last_thread);

		put_sched_app(data->sched_app);
		data->sched_app=NULL;
	}

	/* Free schedctl on exit */
	if (data->schedctl)
		free_page((unsigned long)data->schedctl);

	kfree(data);

}

static int
pmcsched_get_current_metric_value(pmon_prof_t* p,int k,uint64_t* value) {
	return -1; /* NOT IMPLEMENTED */
}

static inline void active_or_inactive( void (*funct) (pmcsched_thread_data_t* t),
                                       pmcsched_thread_data_t* t) {
	unsigned long flags;
	spinlock_t* lock;
	lock=pmcsched_acquire_lock(&flags);
	funct(t);
	check_pending_profiling();
	check_pending_signals();
	pmcsched_release_lock(lock,flags);
}

noinline void trace_pmcsched_active_thread(struct task_struct* p, pmon_prof_t* prof, pmcsched_thread_data_t* pt) {
	asm(" ");
}

noinline void trace_pmcsched_inactive_thread(struct task_struct* p, pmon_prof_t* prof, pmcsched_thread_data_t* pt) {
	asm(" ");
}

static void trace_runnable(pmon_prof_t* prof, unsigned int switch_in) {
	pmcsched_thread_data_t* t=
	    (pmcsched_thread_data_t*)prof->monitoring_mod_priv_data;
	struct task_struct* p=prof->this_tsk;

	if (!switch_in) {
		/* Tracing point */
		if (t->actually_runnable && (get_task_state(p)  != TASK_RUNNING &&
		                             get_task_state(p) != TASK_WAKING)) {
			t->actually_runnable=0;
			trace_pmcsched_inactive_thread(p,prof,t);
		}
	} else {
		if (!t->actually_runnable && ((get_task_state(p)  == TASK_RUNNING) ||
		                              (get_task_state(p) == TASK_WAKING)) ) {
			t->actually_runnable=1;
			trace_pmcsched_active_thread(p,prof,t);
		}
	}
}

static inline int transition_period(pmon_prof_t* prof, unsigned int switch_in) {
	pmcsched_thread_data_t* t=
	    (pmcsched_thread_data_t*)prof->monitoring_mod_priv_data;
	struct task_struct* p=prof->this_tsk;

	if (!switch_in) {
		/*
		 * Did we put it to sleep successfully? We need to distinguish between
		 * a voluntary self-sleep by the thread and a stop signal sent by us.
		 */
		if (t->state == STOP_PENDING && t->signal_sent) {
			pmcsched_set_state(t, STOP_QUEUE);
			t->signal_sent = 0;
			if (active_scheduler_verbose) {
				trace_printk("Thread %d was successfully stopped. -- scheduler \n",
				             prof->this_tsk->pid);
			}
			return 1;
		}

		return 0;
	} else {
		/* We woke it up successfully */
		if (t->state == ACTIVE_PENDING && t->signal_sent) {
			pmcsched_set_state(t, ACTIVE_QUEUE);
			t->signal_sent = 0;
			if (active_scheduler_verbose) {
				trace_printk("Thread %d was successfully waked.\n",p->pid);
			}
			t->runnable = 1;
			return 1;
		}

		return 0;
	}
}

static inline int annoying_profiling_intruders(pmon_prof_t* prof,
        int first_time) {
	spinlock_t *lock;
	pmcsched_thread_data_t* t=
	    (pmcsched_thread_data_t*)prof->monitoring_mod_priv_data;
	struct task_struct* p=prof->this_tsk;
	sched_thread_group_t* cur_group=get_cur_group_sched();
	unsigned long flags = 0;

	/* We don't want the plugin to stop the thread that is being profiled */
	if (cur_group->scheduling_mode == PROFILING && t == cur_group->profiled) {
		BUG_ON(first_time);
		t->runnable = 1;
		return 1;
	}

	/* In profiling state, if a thread switch in it can either (A) Be a new one
	 * and maybe we shouldn't allow a plugin to let this new thread run (depends on
	 the plugin), (B) It has not yet being stopped by the Framework but it will soon so no worries.
	 */
	if (cur_group->scheduling_mode == PROFILING && t != cur_group->profiled &&
	    (get_task_state(p) == TASK_RUNNING || get_task_state(p) == TASK_WAKING)) {

		t->runnable = 1;

		if (first_time && active_scheduler->active_during_profile) {
			lock = pmcsched_acquire_lock(&flags);
			active_scheduler->active_during_profile(t);
			check_pending_profiling_group(cur_group);
			check_pending_signals_group(cur_group);
			pmcsched_release_lock(lock,flags);
		}
	}

	return 0;
}

void pmcsched_on_switch_in(pmon_prof_t* prof) {
	pmcsched_thread_data_t* t=
	    (pmcsched_thread_data_t*)prof->monitoring_mod_priv_data;
	struct task_struct* p=prof->this_tsk;
	int was_first_time = 0;
	sched_thread_group_t* cur_group=get_cur_group_sched();
	unsigned char ignore_thread=(!t || !get_prof_enabled(prof)
	                             || t->security_id!=current_monitoring_module_security_id());
	unsigned long sched_flags=active_scheduler->flags;

	if (active_scheduler->on_switch_in_thread)
		active_scheduler->on_switch_in_thread(prof,t,!ignore_thread);

	if (ignore_thread)
		return;

	if (pmcsched_rdt_capable) {
		was_first_time = __give_rmid(t);
	} else {
		if (t->first_time &&
		    t->security_id == current_monitoring_module_security_id()) {

			t->first_time = 0;

			was_first_time = 1;
		}
	}

	trace_runnable(prof, 1);

	if (sched_flags & PMCSCHED_COSCHED_PLUGIN) {

		/* Did we put wake it up ourselves? */
		if (transition_period(prof, 1))
			goto end_func_in;
		/*
		 * Special considerations must be made when threads come here during
		 * profiling
		*/
		if (annoying_profiling_intruders(prof, was_first_time))
			return;
	}

	if (cur_group->scheduling_mode != PROFILING && !t->runnable && (get_task_state(p)
	        == TASK_RUNNING || get_task_state(p) == TASK_WAKING)) {

		/* This function is called in two scenarios: The task becomes active,
		*  or the task is moved from the stopped queue, with the precondition of
		*  being runnable. Being runnable or not should be independent of the
		*  queue where the plugin located the task.
		*/
		t->runnable = 1;

		/* Update current core type (for the runtime system to see)*/
		if (t->schedctl)
			t->schedctl->sc_coretype=get_coretype_cpu(smp_processor_id());


		// Maybe worry about thread waking up on oversubscription
		active_or_inactive(active_scheduler->on_active_thread,t);
	}

end_func_in:

	if (pmcsched_rdt_capable) {
		__set_rmid_and_cos(t->cmt_data->rmid,t->cmt_data->cos_id);

		if (was_first_time) {
			uint_t llc_id = topology_physical_package_id(smp_processor_id());
			intel_cmt_update_supported_events(&pmcs_cmt_support,t->cmt_data,llc_id);
		}
	}
}

void pmcsched_on_switch_out(pmon_prof_t* prof) {
	pmcsched_thread_data_t* t=
	    (pmcsched_thread_data_t*)prof->monitoring_mod_priv_data;
	struct task_struct* p=prof->this_tsk;
	unsigned char ignore_thread=(!t || !get_prof_enabled(prof)
	                             || t->security_id!=current_monitoring_module_security_id());
	unsigned long sched_flags=active_scheduler->flags;

	if (active_scheduler->on_switch_out_thread)
		active_scheduler->on_switch_out_thread(prof,t,!ignore_thread);

	if (ignore_thread)
		return;

	trace_runnable(prof, 0);

	if (sched_flags & PMCSCHED_COSCHED_PLUGIN) {

		/* Did we put it to sleep ourselves? */
		if (transition_period(prof, 0))
			goto end_func_out;


		/* Is this the Framework just trying to make a Light-Weight scenario? */
		if (get_cur_group_sched()->scheduling_mode == PROFILING)              return;
	}

	if (t->runnable && get_prof_enabled(prof)
	    && (get_task_state(p) != TASK_RUNNING) &&
	    (get_task_state(p) != TASK_WAKING)) {

		/* Fix the mess if the developer killed the thread manually or it was
		*  killed by a third party (even in profiling!)
		*/
		if (!__euthanasia(t)) {

			/* Please notice we cannot assume the task is not runnable anymore as
			 * the plugin could have stopped the thread so we can safely disable
			 * the flag only for non-coscheduling plugins.
			 */
			if (!(sched_flags & PMCSCHED_COSCHED_PLUGIN))
				t->runnable = 0;
			active_or_inactive(active_scheduler->on_inactive_thread,t);
		}
	}

end_func_out:
	if (pmcsched_rdt_capable)
		__unset_rmid();
}

static void  pmcsched_on_tick(pmon_prof_t* p, int cpu) {
	pmcsched_thread_data_t* t=
	    (pmcsched_thread_data_t*)p->monitoring_mod_priv_data;

	if (active_scheduler->on_tick_thread
	    && t
	    &&  t->security_id==current_monitoring_module_security_id())
		active_scheduler->on_tick_thread(t,cpu);
}

monitoring_module_t pmcsched_mm= {
	.info=SCHED_PROTOTYPE_STRING,
	.id=-1,
	.enable_module=pmcsched_enable_module,
	.disable_module=pmcsched_disable_module,
//  .on_read_config=pmcsched_on_read_config,
//  .on_write_config=pmcsched_on_write_config,
	.on_fork=pmcsched_on_fork,
	.on_exec=pmcsched_on_exec,
	.on_new_sample=pmcsched_on_new_sample,
	.on_migrate=pmcsched_on_migrate,
	.on_exit=pmcsched_on_exit,
	.on_free_task=pmcsched_on_free_task,
	.get_current_metric_value=pmcsched_get_current_metric_value,
	.on_tick=pmcsched_on_tick,
	.module_counter_usage=pmcsched_module_counter_usage,
	.on_switch_in=pmcsched_on_switch_in,
	.on_switch_out=pmcsched_on_switch_out,
};

/* ----------------------AUXILIARY FUNCTIONS ---------------------------------*/

/*  This is an auxiliary function to switch members between lists. It assumes
    all the necessary locks have been acquired.
*/

/*  Set state of a task
*/
noinline void pmcsched_set_state(pmcsched_thread_data_t *t, state_t state) {
	t->state = state;
}

void switch_queues (sized_list_t *queue_1, sized_list_t *queue_2, void *val) {
	if (!val) return;

	if (queue_1) {
		/* Make sure we want to remove something that is in the list */
		remove_sized_list(queue_1,val);
	}
	if (queue_2) {
		/* Make sure not to insert something that is already there */
		insert_sized_list_tail(queue_2,val);
	}
}

/* This auxiliary function flags as zero the cores with running threads launched
 * with pmc-launch and returns the number of busy cores.
*/
int mark_busy_cores(int *free_cores, int num_cores) {
	unsigned int i = 0, found = 0, cpu, N;
	pmcsched_thread_data_t *aux;
	sched_thread_group_t* cur_group=get_cur_group_sched();

	if (!sized_list_length(&cur_group->active_threads)) return 0;

	for (; i < num_cores; ++i) {
		free_cores[i] = 1;
	}

	aux = head_sized_list(&cur_group->active_threads);
	N = sized_list_length(&cur_group->active_threads);

	for (i = 0; i < N && (aux != NULL) ; ++i) {

		if (aux->runnable && !signal_pending_th(aux) &&
		    (get_task_state(aux->prof->this_tsk) == TASK_RUNNING ||
		     get_task_state(aux->prof->this_tsk) == TASK_WAKING)) {

			cpu = task_cpu(aux->prof->this_tsk);

			/* I think it can happen that already free_cores[cpu] == 0 as the
			 * running tasks can be in that processor run-queue
			 */
			if (free_cores[cpu]) {
				found++;
				free_cores[cpu] = 0 ;
			}
		}

		aux = next_sized_list(&cur_group->active_threads,aux);
	}

	return found;
}

/* Auxiliary function to keep track of the next free cores within an array */
int next_free_cpu(int *free_cores, int max) {
	static int prev = -1, prev_2 = -1;
	int i = prev + 1;

	/* undo one (thread there) or reinitialize for next periodic call */
	if (!free_cores) {
		if (max == -1) {
			prev = prev-1;
		} else {
			prev = -1;
		}
		i = -1;
	} else {
		for (; i < max && !free_cores[i]; ++i) {}
		if (i == max) i = -1;
		prev_2 = prev;
		prev = i;
	}

	return i;
}

void check_active_app(struct app_pmcsched* app) {
	sched_thread_group_t* cur_group=get_cur_group_sched();

	if (sized_list_length(&app->app_active_threads)
	    && app->state != ACTIVE_QUEUE) {
		if (app->state == STOP_QUEUE) {
			switch_queues(&cur_group->stopped_apps,
			              &cur_group->active_apps,app);
		} else if (app->state == NO_QUEUE) {
			insert_sized_list_tail(&cur_group->active_apps,app);
		}
		//trace_printk(
		//     "An application just became active (One active thread).\n");
		app->state = ACTIVE_QUEUE;
	}
}

void check_inactive_app(struct app_pmcsched *app) {
	sched_thread_group_t* cur_group=get_cur_group_sched();

	if (!sized_list_length(&app->app_active_threads)
	    && app->state != STOP_QUEUE) {
		if (app->state == ACTIVE_QUEUE) {
			switch_queues(&cur_group->active_apps,
			              &cur_group->stopped_apps,app);
		} else {
			insert_sized_list_tail(&cur_group->stopped_apps,app);
		}
		if (active_scheduler_verbose) {
			trace_printk("An application just became inactive (zero active threads).\n");
		}
		app->state = STOP_QUEUE;
	} else if (active_scheduler_verbose) {
		trace_printk("A thread of a multi-threaded program just became inactive.\n");
	}
}

void __activate_stopped_by_profiling(sized_list_t* stopped_list,
                                     sized_list_t* signal_list) {
	int i = 0, N;
	pmcsched_thread_data_t *activated, *elem;
	sched_thread_group_t* cur_group=get_cur_group_sched();

	/* All the non core-fitting threads will be stopped  */

	activated = head_sized_list(stopped_list);
	N = sized_list_length(stopped_list);

	for (; activated != NULL && i < N; i++) {

		elem = activated;
		activated = next_sized_list(stopped_list,activated);

		/* Was this thread was killed by the developer manually? */
		if (dead_task(elem)) continue;

		if (elem->signal_sent) continue;

		if (elem->runnable) {

			switch_queues(&elem->app->app_stopped_threads,
			              &elem->app->app_active_threads,elem);
			switch_queues(&cur_group->stopped_threads,
			              &cur_group->active_threads,elem);

			/* Maybe the Framework did not have enough time to stop it */
			if (!signal_pending_th(elem)) {
				insert_sized_list_tail(signal_list,elem);
			}

			pmcsched_set_state(elem, ACTIVE_PENDING);
			elem->stopped_during_profiling = 0;
		}
	}

	init_sized_list (stopped_list,
	                 offsetof(pmcsched_thread_data_t,
	                          profiler_stopped_links));
}

int __give_rmid(pmcsched_thread_data_t* t) {
	int ret = 0;


	if (t->first_time &&
	    t->security_id == current_monitoring_module_security_id()) {
		int nr_cpu_groups=0;
		unsigned int rmid;
		int i;

		get_platform_cpu_groups(&nr_cpu_groups);

		/* Assign RMID */
		rmid=get_rmid();
		t->first_time = 0;

		/* Propagate Assigned RMID, through per-group structures */
		for (i=0; i<nr_cpu_groups; i++) {
			app_t* app_cache=&t->sched_app->pmc_sched_apps[i].app_cache;
			app_cache->app_cmt_data.rmid=rmid;
		}

		if (active_scheduler_verbose) {
			trace_printk("Thread %d assigned RMID %d.\n",
			             t->prof->this_tsk->pid, t->cmt_data->rmid);
		}
		ret = 1;
	}

	return ret;
}

/* Was the thread killed by a third party? */
int __euthanasia(pmcsched_thread_data_t* t) {
	int ret = 0;
	sched_thread_group_t* cur_group=get_cur_group_sched();

	if (get_task_state(t->prof->this_tsk) == TASK_DEAD && t->state != TASK_KILLED) {

		/* If the thread is killed but that is not recorded within the
		 * pmcsched_thread_data_t this means that the developer sent the
		 * killing signal himself. Hence, trying to manipulate the task
		 * is dangerous.
		 */
		trace_printk("%s: Killing via signals can lead to unexpected behavior!!\n",__func__);

		/* This could lead to unexpected behavior... */
		if (active_scheduler->on_exit_thread)
			active_scheduler->on_exit_thread(t);
		pmcsched_set_state(t, TASK_KILLED);
		ret = 1;

		if (t == cur_group->profiled) {

			trace_printk("The killed thread (%d) was being profiled!\n",
			             t->prof->this_tsk->pid);

			trace_printk("%s: Trying to fix this mess...\n",__func__);

			__activate_stopped_by_profiling(&cur_group->profiler_stopped_list,
			                                &cur_group->pending_signals_list);

			cur_group->scheduling_mode = NORMAL;
		}
	}
	return ret;
}

void assign_cpu(pmcsched_thread_data_t* elem, int core,
                sized_list_t* migration_list) {
	if (task_cpu(elem->prof->this_tsk) != core) {

		cpumask_clear(elem->mask);
		cpumask_set_cpu(core, elem->mask);
		/* Add it to the list of threads to migrate */
		insert_sized_list_tail(migration_list,elem);
	}
}

void __compute_profiling_time(int init) {
	static int repetitions = 0;
	static int total_t = 0;
	static int pid;
	int time;
	sched_thread_group_t* cur_group=get_cur_group_sched();

	if (init) {
		repetitions = 0;
		total_t = 0;
		return;
	}

	if (cur_group->scheduling_mode == PROFILING) {
		pid = cur_group->profiled->prof->this_tsk->pid;
		repetitions++;
	} else if (repetitions) {
		time = repetitions * 100;
		total_t += time;
		trace_printk("Thread %d profiling lasted ~%d msecs\n",pid,time);
		trace_printk("Total Profiling Time = ~%d milliseconds", total_t);
		repetitions = 0;
	}
}

/* This auxiliary function activates the thread if it was stopped */
void make_active_if_stopped(pmcsched_thread_data_t* activated,
                            sized_list_t* signal_list) {
	sched_thread_group_t* cur_group=get_cur_group_sched();

	if (activated->state == STOP_QUEUE) {

		switch_queues(&activated->app->app_stopped_threads,
		              &activated->app->app_active_threads,activated);
		switch_queues(&cur_group->stopped_threads,
		              &cur_group->active_threads,activated);

		pmcsched_set_state(activated, ACTIVE_PENDING);
		/* Add it to the list of threads to be waken up */
		insert_sized_list_tail(signal_list,activated);

		if (active_scheduler_verbose) {
			trace_printk("Task %d will be made active -- scheduler.\n",
			             activated->prof->this_tsk->pid);
		}

		/* Check if it became an active application */
		check_active_app(activated->app);
	} else {
		pmcsched_set_state(activated, REACTIVATED);
		if (active_scheduler_verbose) {
			trace_printk("Task %d already was not stopped! -- scheduler.\n",
			             activated->prof->this_tsk->pid);
		}
	}
}

extern int compare_fitness(void* t_1, void* t_2);

/* Topology detection features */
static cpu_socket_t platform_sockets[MAX_SOCKETS_PLATFORM];
static int nr_sockets_platform;
static cpu_group_t platform_cpu_groups[MAX_GROUPS_PLATFORM];
static int nr_groups_platform;

/*  x86-specific implementation */
void populate_topology_structures(void) {

	int nr_cpus=num_online_cpus(); /* num_present_cpus(); Hidden SMT shown with that other function... */
	unsigned long i=0;
	cpu_struct_t *cdata=get_boot_cpu_structure();
	unsigned long group_mask=0;
	unsigned long group_mask_uninitialized=0;
	unsigned long socket_mask=0;
	const int mask_bitwidth=sizeof(unsigned long)*8;
	cpu_socket_t* socket;
	cpu_group_t* group;
	int group_id;
	unsigned long group_idx;

	/* Populate group_mask and socket_mask */
	for (i=0; i<nr_cpus; i++) {
		cdata = get_cpu_structure_by_idx(i);
		socket_mask|=(1<<__get_socket_id(cdata));
		group_mask|=(1<<__get_cpu_group_id(cdata));
	}

	nr_sockets_platform=0;
	nr_groups_platform=0;
	group_mask_uninitialized=group_mask; /* Back it up */


	/* Populate groups and sockets */
	for (i=0; i<mask_bitwidth && socket_mask; i++) {
		if (socket_mask & (1<<i)) {
			socket=&platform_sockets[nr_sockets_platform++];
			socket_mask&=~(1<<i); /* Clear bit */
			/* Initialize socket structure */
			socket->nr_cpu_groups=0;
			socket->socket_id=i;
		}
	}

	for (i=0; i<mask_bitwidth && group_mask; i++) {
		if (group_mask & (1<<i)) {
			nr_groups_platform++;
			group_mask&=~(1<<i);
		}
	}

	trace_printk("Sockets detected:%d\tCore group count:%d\n",nr_sockets_platform,nr_groups_platform);

	for (i=0; i<nr_cpus; i++) {
		cdata = get_cpu_structure_by_idx(i);
		/* Retrieve socket structure */
		socket=&platform_sockets[__get_socket_id(cdata)];
		group_id=__get_cpu_group_id(cdata);
		group_idx=group_id;

		group=&platform_cpu_groups[group_id];

		if (group_mask_uninitialized & (1ULL<<group_idx)) {
			/* Add group to socket */
			socket->cpu_groups[socket->nr_cpu_groups++]=group;

			/* Initialize group structure */
			cpumask_clear(&group->shared_cpu_map);
			cpumask_clear(&group->online_cpu_map);
			group->nr_cpus=group->nr_online_cpus=0;
			group->socket_id=__get_socket_id(cdata);
			/* TODO: for now they are the same, but this may be configurable */
			group->group_id=group->llc_id=group_id;
			group->cpu_type=get_coretype_cpu(i); /* Using PMCTrack API */
			spin_lock_init(&group->lock);
			/* Clear mask to avoid future initialization */
			group_mask_uninitialized&=~(1ULL<<group_idx);
		}

		/* Add cpu to corresponding group */
		group->cpus[group->nr_cpus++]=i;
		group->nr_online_cpus++;
		cpumask_set_cpu(i,&group->shared_cpu_map);
		cpumask_set_cpu(i,&group->online_cpu_map);
	}

}

#ifdef CONFIG_X86
cpu_group_t* get_cpu_group(int cpu) {
	struct cpuinfo_x86 *cdata=&cpu_data(cpu);
	int group_id=__get_cpu_group_id(cdata);
	return  &platform_cpu_groups[group_id];
}
#else



cpu_group_t* get_cpu_group(int cpu) {
	return  &platform_cpu_groups[get_coretype_cpu(cpu)];
}
#endif

void cpu_group_on_cpu_off(int cpu) {
	unsigned long flags;
	cpu_group_t* group=get_cpu_group(cpu);

	if (!group)
		return;

	spin_lock_irqsave(&group->lock,flags);

	if (cpumask_test_and_clear_cpu(cpu,&group->online_cpu_map))
		group->nr_online_cpus--;

	spin_unlock_irqrestore(&group->lock,flags);
}

void cpu_group_on_cpu_on(int cpu) {
	unsigned long flags;
	cpu_group_t* group=get_cpu_group(cpu);

	if (!group)
		return;

	spin_lock_irqsave(&group->lock,flags);

	if (!cpumask_test_and_set_cpu(cpu,&group->online_cpu_map))
		group->nr_online_cpus++;

	spin_unlock_irqrestore(&group->lock,flags);
}


int get_group_id_cpu(int cpu) {
	cpu_group_t* group=get_cpu_group(cpu);

	if (!group)
		return -1;
	else
		return group->group_id;
}

int get_llc_id_cpu(int cpu) {
	cpu_group_t* group=get_cpu_group(cpu);

	if (!group)
		return -1;
	else
		return group->llc_id;
}

cpu_group_t* get_platform_cpu_groups(int *nr_cpu_groups) {
	(*nr_cpu_groups)=nr_groups_platform;
	return platform_cpu_groups;
}

cpu_socket_t* get_platform_cpu_sockets(int *nr_cpu_sockets) {
	(*nr_cpu_sockets)=nr_sockets_platform;
	return platform_sockets;
}

/* For LFOC-Multi*/
void set_group_timer_expiration(sched_thread_group_t* group, unsigned long new_period) {

	mod_timer(&group->timer, new_period);
}

static inline void __update_app_clos(app_t_pmcsched* app, sized_list_t* clos_list) {
	sized_list_t* list=&app->app_active_threads;
	pmcsched_thread_data_t* cur;
	int cpu_task;
	struct task_struct* p;
	struct clos_cpu* item;

	/* Update CLOS & RMID for each active thread */
	for (cur=head_sized_list(list); cur!=NULL; cur=next_sized_list(list,cur)) {
		p=cur->prof->this_tsk;
		cpu_task=task_cpu_safe(p);

		if (cpu_task==-1 || get_task_state(p)!=TASK_RUNNING)
			continue;

		item=get_clos_pool_cpu(cpu_task);

		if (!item) {
			trace_printk("Detected reuse of per-cpu clos structure (CPU %d, PID %d)\n",cpu_task,p->pid);
			continue;
		}

		item->cpu=cpu_task;
		item->rmid=app->app_cache.app_cmt_data.rmid;

		if (app->app_cache.cat_partition)
			item->cos_id=app->app_cache.cat_partition->clos_id;
		else
			item->cos_id=app->app_cache.app_cmt_data.cos_id;

		insert_sized_list_tail(clos_list,item);
	}
}

/*
 * Function to update clos of application that were moved from their
 * associated partition
*/
void populate_clos_to_update_list(sched_thread_group_t* group, cache_part_set_t* part_set) {
	sized_list_t* applist=get_deferred_cos_assignment(part_set);
	app_t* app=head_sized_list(applist);
	app_t_pmcsched *cur_app;

	app_t* next;

	if (sized_list_length(applist)==0)
		return;

	while (app!=NULL) {
		next=next_sized_list(applist,app);
		cur_app=get_app_pmcsched(app);
		__update_app_clos(cur_app,&group->clos_to_update);
		remove_sized_list(applist,app);
		app=next;
	}
}



/* Static trace point */
noinline void trace_amp_exit(struct task_struct* p, unsigned int tid, unsigned long ticks_big, unsigned long ticks_small) {
	asm(" ");
}

/* */
noinline void trace_td_thread_exit(struct task_struct* p,
                                   unsigned long noclass,
                                   unsigned long class0,
                                   unsigned long class1,
                                   unsigned long class2,
                                   unsigned long class3) {
	asm(" ");
}
