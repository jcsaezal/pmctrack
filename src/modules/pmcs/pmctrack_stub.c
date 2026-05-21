
/*
 *  pmctrack_stub.c
 *
 *  Code for dynamic live patching of the kernel
 *  enabling PMCTrack to work on vanilla kernels
 *
 *  Copyright (c) 2021 Lazaro Clemen Palafox <lazarocl@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/ptrace.h>
#include <linux/preempt.h>

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/kallsyms.h>
#include <linux/rwlock.h>
#include <linux/rcupdate.h>

#include <linux/string.h>
#include <linux/err.h>
#include <linux/module.h>
#include <pmc/pmctrack_stub.h>
#include <linux/kprobes.h>
#if defined CONFIG_X86
#include <pmc/intel_ehfi.h>
#include <asm/nmi.h>
#endif

/** ## Compatibility code for patch
 *
 * https://lore.kernel.org/lkml/20201113171939.162178036@goodmis.org/
 *
 */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(5,15,0)
typedef struct pt_regs ftrace_regs_t;
static __always_inline struct pt_regs *ftrace_get_regs(ftrace_regs_t *fregs)
{
	if (!fregs)
		return NULL;

	return fregs;
}
#else
typedef struct ftrace_regs ftrace_regs_t;
#endif


#if defined(CONFIG_X86) && defined(CONFIG_KPROBES) && !defined(CONFIG_PMCTRACK)
#define USE_BLOCKING_KPROBE
#endif
/*
* regs_get_kernel_argument introduced in following kernels
* - x86: Linux 4.20.0
* - arm64: Linux 5.2.0
*/
#if defined(__aarch64__) || defined (__arm__)
#ifdef __arm__
/*
 * Read a register given an architectural register index r.
 * This handles the common case where 31 means XZR, not SP.
 */
static inline unsigned long pt_regs_read_reg(const struct pt_regs *regs, int r)
{
	return (r == 31) ? 0 : regs->uregs[r];
}
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5,2,0)) || defined(__arm__)
/**
 * regs_get_kernel_argument() - get Nth function argument in kernel
 * @regs:	pt_regs of that context
 * @n:		function argument number (start from 0)
 *
 * regs_get_argument() returns @n th argument of the function call.
 *
 * Note that this chooses the most likely register mapping. In very rare
 * cases this may not return correct data, for example, if one of the
 * function parameters is 16 bytes or bigger. In such cases, we cannot
 * get access the parameter correctly and the register assignment of
 * subsequent parameters will be shifted.
 */
static inline unsigned long regs_get_kernel_argument(struct pt_regs *regs,
        unsigned int n)
{
#define NR_REG_ARGUMENTS 8
	if ( regs && (n < NR_REG_ARGUMENTS))
		return pt_regs_read_reg(regs, n);
	return 0;
}
#endif
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,20,0)

/**
 * regs_get_kernel_argument() - get Nth function argument in kernel
 * @regs:	pt_regs of that context
 * @n:		function argument number (start from 0)
 *
 * regs_get_argument() returns @n th argument of the function call.
 * Note that this chooses most probably assignment, in some case
 * it can be incorrect.
 * This is expected to be called from kprobes or ftrace with regs
 * where the top of stack is the return address.
 */
static inline unsigned long regs_get_kernel_argument(struct pt_regs *regs,
        unsigned int n)
{
	static const unsigned int argument_offs[] = {
#ifdef __i386__
		offsetof(struct pt_regs, ax),
		offsetof(struct pt_regs, dx),
		offsetof(struct pt_regs, cx),
#define NR_REG_ARGUMENTS 3
#else
		offsetof(struct pt_regs, di),
		offsetof(struct pt_regs, si),
		offsetof(struct pt_regs, dx),
		offsetof(struct pt_regs, cx),
		offsetof(struct pt_regs, r8),
		offsetof(struct pt_regs, r9),
#define NR_REG_ARGUMENTS 6
#endif
	};

	if (n >= NR_REG_ARGUMENTS) {
		n -= NR_REG_ARGUMENTS - 1;
		return regs_get_kernel_stack_nth(regs, n);
	} else
		return regs_get_register(regs, argument_offs[n]);
}


#endif
#endif


#define FOR_EACH_INTEREST(i) \
    for (i = 0; i < sizeof(interests) / sizeof(struct tracepoints_table); i++)

/**
 * struct ftrace_hook - Data structure to store ftrace functions information
 * @name:	kernel function to install a probe
 * @ops:	ops structure that holds the function for profiling
 */
struct ftrace_hook {
	unsigned char *name;
	struct ftrace_ops ops;
};

#ifndef CONFIG_PMCTRACK
/**
 * struct tracepoints_table - Data structure to store tracepoints information
 * @name:	name of the tracepoint
 * @fct:	pointer to the probe function code
 * @value:	tracepoint
 * @init:	flag to know if a probe function is registered
 */
struct tracepoints_table {
	const char *name;
	void *fct;
	struct tracepoint *value;
	char init;
};

static pmc_ops_t* pmc_ops_mod = NULL; /* No implementation is registered by default */
static struct module* implementer = NULL;
DEFINE_RWLOCK(pmc_ops_lock);

/*
 * PMCTrack's kernel module invokes this function to register
 * an implementation of the pmc_ops_t interface
 */
int register_pmc_module(pmc_ops_t* pmc_ops_module, struct module* module)
{
	int ret=0;
	unsigned long flags;

	write_lock_irqsave(&pmc_ops_lock,flags);

	/* Module has been installed already */
	if (implementer!=NULL) {
		ret=-EPERM;
	} else {
		implementer = module;
		rcu_assign_pointer(pmc_ops_mod,pmc_ops_module);
	}

	write_unlock_irqrestore(&pmc_ops_lock,flags);
	return ret;
}

/* PMCTrack's kernel module invokes this function when unloaded */
int unregister_pmc_module(pmc_ops_t* pmc_ops_module, struct module* module)
{
	int ret=0;
	unsigned long flags;

	write_lock_irqsave(&pmc_ops_lock,flags);

	if(implementer!=module) {
		ret=-EPERM;
	} else {
		implementer=NULL;
		rcu_assign_pointer(pmc_ops_mod,NULL);
	}
	write_unlock_irqrestore(&pmc_ops_lock,flags);

	/*
	 * If the operation succeeded wait for all readers to complete.
	 * Since synchronize_rcu() may block, this has to be done
	 * without the spin lock held
	 */
	if (ret==0)
		synchronize_rcu();

	return ret;
}

EXPORT_SYMBOL(register_pmc_module);
EXPORT_SYMBOL(unregister_pmc_module);

/*
 * Wrapper function for the various pmc_ops_t operations
 */

/* Invoked when forking a process/thread */
int pmcs_alloc_per_thread_data(unsigned long clone_flags, struct task_struct *p)
{
	int ret=0;
	pmc_ops_t* pmc_ops= NULL;
	unsigned long flags;

	read_lock_irqsave(&pmc_ops_lock,flags);

	/*
	 * If there is no implementer module or it's being removed
	 * from the kernel, return immediately.
	 */
	if (!implementer || !try_module_get(implementer)) {
		read_unlock_irqrestore(&pmc_ops_lock,flags);
		return 0;
	}

	read_unlock_irqrestore(&pmc_ops_lock,flags);

	/* Now it's safe to dereference pmc_ops_mod */
	pmc_ops=pmc_ops_mod;

	/* Invoke the allocation operation (may block) */
	if(pmc_ops!=NULL && pmc_ops->pmcs_alloc_per_thread_data!=NULL) {
		ret=pmc_ops->pmcs_alloc_per_thread_data(clone_flags,p);
	}

	/* Allow the module to be removed now */
	module_put(implementer);

	return ret;
}

/* Invoked when a context switch out takes place */
void pmcs_save_callback(struct task_struct* tsk, int cpu)
{
	pmc_ops_t* pmc_ops= NULL;

	if (!implementer)
		return;

	rcu_read_lock();

	pmc_ops=rcu_dereference(pmc_ops_mod);

	if(pmc_ops!=NULL && pmc_ops->pmcs_save_callback!=NULL)
		pmc_ops->pmcs_save_callback(get_prof(tsk), cpu);

	rcu_read_unlock();
}

/* Invoked when a context switch in takes place */
void pmcs_restore_callback(struct task_struct* tsk, int cpu)
{
	pmc_ops_t* pmc_ops= NULL;

	if (!implementer)
		return;

	rcu_read_lock();

	pmc_ops=rcu_dereference(pmc_ops_mod);

	if(pmc_ops!=NULL && pmc_ops->pmcs_restore_callback!=NULL)
		pmc_ops->pmcs_restore_callback(get_prof(tsk), cpu);

	rcu_read_unlock();
}

/* Invoked from scheduler_tick() */
void pmcs_tbs_tick(struct task_struct* tsk, int cpu)
{
	pmc_ops_t* pmc_ops= NULL;

	if (!implementer)
		return;

	rcu_read_lock();

	pmc_ops=rcu_dereference(pmc_ops_mod);

	if(pmc_ops!=NULL && pmc_ops->pmcs_tbs_tick!=NULL)
		pmc_ops->pmcs_tbs_tick(get_prof(tsk), cpu);

	rcu_read_unlock();
}

/* Invoked when a process calls exec() */
void pmcs_exec_thread(struct task_struct* tsk)
{
	pmc_ops_t* pmc_ops= NULL;

	if (!implementer)
		return;

	rcu_read_lock();

	pmc_ops=rcu_dereference(pmc_ops_mod);

	if(pmc_ops!=NULL && pmc_ops->pmcs_exec_thread!=NULL)
		pmc_ops->pmcs_exec_thread(tsk);

	rcu_read_unlock();
}

/* Invoked when the kernel frees up the process descriptor */
void pmcs_free_per_thread_data(struct task_struct* tsk)
{
	pmc_ops_t* pmc_ops= NULL;
	unsigned long flags;

	read_lock_irqsave(&pmc_ops_lock,flags);

	/*
	 * If there is no implementer module or it's being removed
	 * from the kernel, return immediately.
	 */
	if (!implementer || !try_module_get(implementer)) {
		read_unlock_irqrestore(&pmc_ops_lock,flags);
		return;
	}

	read_unlock_irqrestore(&pmc_ops_lock,flags);

	/* Now it's safe to dereference pmc_ops_mod */
	pmc_ops=pmc_ops_mod;

	if(pmc_ops!=NULL && pmc_ops->pmcs_free_per_thread_data!=NULL)
		pmc_ops->pmcs_free_per_thread_data(tsk);

	/* Allow the module to be removed now */
	module_put(implementer);
}

/* Invoked when a process exits */
void pmcs_exit_thread(struct task_struct* tsk)
{
	pmc_ops_t* pmc_ops= NULL;
	unsigned long flags;

	read_lock_irqsave(&pmc_ops_lock,flags);

	/*
	 * If there is no implementer module or it's being removed
	 * from the kernel, return immediately.
	 */
	if (!implementer || !try_module_get(implementer)) {
		read_unlock_irqrestore(&pmc_ops_lock,flags);
		return;
	}

	read_unlock_irqrestore(&pmc_ops_lock,flags);

	/* Now it's safe to dereference pmc_ops_mod */
	pmc_ops=pmc_ops_mod;

	if(pmc_ops!=NULL && pmc_ops->pmcs_exit_thread!=NULL)
		pmc_ops->pmcs_exit_thread(tsk);

	/* Allow the module to be removed now */
	module_put(implementer);
}

/*
 * Invoked from the code of experimental scheduling classes that leverage per-thread performance
 * counter data when making scheduling decisions.
 * The source code of these scheduling classes is not provided along with this patch, though.
 */
int pmcs_get_current_metric_value(struct task_struct* task, int key, uint64_t* value)
{
	int ret=-1;
	pmc_ops_t* pmc_ops= NULL;

	if (!implementer)
		return ret;

	rcu_read_lock();

	pmc_ops=rcu_dereference(pmc_ops_mod);

	if(pmc_ops!=NULL && pmc_ops->pmcs_get_current_metric_value!=NULL)
		ret=pmc_ops->pmcs_get_current_metric_value(task,key,value);

	rcu_read_unlock();

	return ret;
}
#endif

/*** Ftrace setup ***/

static struct ftrace_hook ftrace_hooks[] = {
#ifndef CONFIG_PMCTRACK
	{.name = "free_task"},
#ifndef USE_BLOCKING_KPROBE
#ifdef __arma__
	{.name = "sched_fork"},
#else
	{.name = "copy_semundo"}, 	// sched_fork
#endif
#endif
	{.name = "sched_exec"},
	{.name = "scheduler_tick"},
	{.name = "finish_task_switch"},
#endif
#if defined CONFIG_X86 && !defined CONFIG_PMC_PERF
	{.name = "perf_event_nmi_handler"}, // x86 and legacy only
#endif
#if defined CONFIG_X86
	{.name = "intel_thermal_interrupt"},
#endif
};

/**
 * ftrace - callback functions
 */
#ifndef CONFIG_PMCTRACK
static void notrace fh_ftrace_free_task(unsigned long ip, unsigned long parent_ip,
                                        struct ftrace_ops *ops, ftrace_regs_t *fregs)
{
	struct pt_regs *regs = ftrace_get_regs(fregs);
	pmcs_free_per_thread_data((void*) regs_get_kernel_argument(regs, 0));
}

static void notrace fh_ftrace_sched_fork(unsigned long ip, unsigned long parent_ip,
        struct ftrace_ops *ops, ftrace_regs_t *fregs)
{

	struct pt_regs *regs = ftrace_get_regs(fregs);
	struct task_struct* p=(struct task_struct*) regs_get_kernel_argument(regs, 1);
	preempt_enable_notrace();
	pmcs_alloc_per_thread_data(regs_get_kernel_argument(regs, 0), p);
	preempt_disable_notrace();
}

static void notrace fh_ftrace_sched_exec(unsigned long ip, unsigned long parent_ip,
        struct ftrace_ops *ops, ftrace_regs_t *fregs)
{
	pmcs_exec_thread(current);
}

static void notrace fh_ftrace_scheduler_tick(unsigned long ip, unsigned long parent_ip,
        struct ftrace_ops *ops, ftrace_regs_t *fregs)
{
	pmcs_tbs_tick(current, smp_processor_id());
}

static void notrace fh_ftrace_finish_task_switch(unsigned long ip, unsigned long parent_ip,
        struct ftrace_ops *ops, ftrace_regs_t *fregs)
{
	pmcs_restore_callback(current, smp_processor_id());
}
#endif

#if defined CONFIG_X86 && !defined CONFIG_PMC_PERF
/**
 * Livepatch version of per_mmni's function so it does
 * nothing returning an unkown non-maskable interrupt.
 */
static int livepatch_perf_event_nmi_handler(unsigned int cmd, struct pt_regs *regs)
{
	return NMI_UNKNOWN;
}

/**
 * Livepatch probe function to "steal" from perf-events the hardware
 * counter overflow interrupt registration operation.
 */
static void notrace fh_ftrace_perf_event_nmi_handler(unsigned long ip, unsigned long parent_ip,
        struct ftrace_ops *ops, ftrace_regs_t *fregs)
{
	struct pt_regs *regs = ftrace_get_regs(fregs);
	/* Allow patching functions where RCU is not watching */
	//preempt_disable_notrace();
	regs->ip = (unsigned long) livepatch_perf_event_nmi_handler;
	//preempt_enable_notrace();
}
#endif

#if defined CONFIG_X86
static void notrace fh_ftrace_intel_thermal_interrupt(unsigned long ip, unsigned long parent_ip,
        struct ftrace_ops *ops, ftrace_regs_t *fregs)
{
	intel_ehfi_process_event();
}
#endif

/**
 * fh_install_hook - register a function for profiling
 * @hook:	structure that holds the function for profiling.
 *
 * Returns 0 if ok, error value on error.
 */
int fh_install_hook(struct ftrace_hook *hook)
{
	int err;

#ifndef CONFIG_PMCTRACK
	if(strcmp(hook->name, "free_task") == 0) {
		hook->ops.func = fh_ftrace_free_task;
		hook->ops.flags = FTRACE_OPS_FL_SAVE_REGS;
	} else
#ifdef __arma__
		if(strcmp(hook->name, "sched_fork") == 0) {
#else
		if(strcmp(hook->name, "copy_semundo") == 0) {
#endif
			hook->ops.func = fh_ftrace_sched_fork;
			hook->ops.flags = FTRACE_OPS_FL_SAVE_REGS;
		} else if(strcmp(hook->name, "sched_exec") == 0) {
			hook->ops.func = fh_ftrace_sched_exec;
			hook->ops.flags = FTRACE_OPS_FL_RCU;
		} else if(strcmp(hook->name, "scheduler_tick") == 0) {
			hook->ops.func = fh_ftrace_scheduler_tick;
			hook->ops.flags = FTRACE_OPS_FL_RCU;
		} else if(strcmp(hook->name, "finish_task_switch") == 0) {
			hook->ops.func = fh_ftrace_finish_task_switch;
			hook->ops.flags = FTRACE_OPS_FL_RCU;
		} else
#endif
#if defined CONFIG_X86 && !defined CONFIG_PMC_PERF
			if(strcmp(hook->name, "perf_event_nmi_handler") == 0) {
				hook->ops.func = fh_ftrace_perf_event_nmi_handler;
				hook->ops.flags = FTRACE_OPS_FL_SAVE_REGS
				                  | FTRACE_OPS_FL_IPMODIFY;
			}
#else
			;
#endif
#if defined CONFIG_X86
	if(strcmp(hook->name, "intel_thermal_interrupt") == 0) {
		hook->ops.func = fh_ftrace_intel_thermal_interrupt;
		hook->ops.flags = FTRACE_OPS_FL_RCU;
	}
#endif
	err = ftrace_set_filter(&hook->ops, hook->name, strlen(hook->name), 0);
	if (err<0) {
		printk("ftrace - set filter for %s failed. Error code %d\n", hook->name, err);
		return err;
	}

	err = register_ftrace_function(&hook->ops);
	if (err) {
		printk("ftrace - register of %s function() failed. Error code %d\n", hook->name, err);
		ftrace_set_notrace(&hook->ops, hook->name, strlen(hook->name), 0);
		return err;
	}

	return 0;
}

/**
 * fh_remove_hook - unregister a function for profiling.
 *
 * @hook:	structure that holds the function for profiling.
 */
void fh_remove_hook(struct ftrace_hook *hook)
{
	int err;

	err = unregister_ftrace_function(&hook->ops);
	if (err) {
		printk("ftrace - unregister of %s function failed. Error code %d\n", hook->name,  err);
	}

	ftrace_set_notrace(&hook->ops, hook->name, strlen(hook->name), 0);
	if (err) {
		printk("ftrace - unset filter for %s failed. Error code %d\n", hook->name, err);
	}
}

/**
 * fh_install_hooks - register @count functions for profiling
 * @hooks:	array that holds the functions for profiling.
 * @count:	number of functions
 *
 * Returns 0 if ok, error value on error.
 */
int fh_install_hooks(struct ftrace_hook *hooks, size_t count)
{
	int err;
	size_t i;

	for (i = 0; i < count; i++) {
		err = fh_install_hook(&hooks[i]);
		if (err) {
			while (i != 0) {
				fh_remove_hook(&hooks[--i]);
			}
			if (err>0)
				err=-ENOTSUPP;
			return err;
		}
	}

	return 0;
}

/**
 * fh_remove_hooks - unregister @count functions from being profiled
 * @hooks:	array that holds the functions being profiled.
 * @count:	number of functions
 */
void fh_remove_hooks(struct ftrace_hook *hooks, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		fh_remove_hook(&hooks[i]);
}


/*** Tracepoints setup **/

#ifndef CONFIG_PMCTRACK
/**
 * Tracepoint probe functions
 */
static void probe_sched_switch(void *data, bool preempt, struct task_struct *prev,
                               struct task_struct *next)
{
	pmcs_save_callback(prev, smp_processor_id());
}

static void probe_sched_process_exit(void *data, struct task_struct *p)
{
#if defined(__aarch64__) || defined (__arm__)
	preempt_enable_notrace();
#endif
	pmcs_exit_thread(p);
#if defined(__aarch64__) || defined (__arm__)
	preempt_disable_notrace();
#endif
}

struct tracepoints_table interests[] = {
	{.name = "sched_switch", .fct = probe_sched_switch},
	{.name = "sched_process_exit", .fct = probe_sched_process_exit},
};

/**
 * trace_event_exit - Disconnect all probe functions from the tracepoints
 */
static void trace_event_exit(void)
{
	int i;

	// Cleanup the tracepoints
	FOR_EACH_INTEREST(i) {
		if (interests[i].init) {
			tracepoint_probe_unregister(interests[i].value, interests[i].fct,
			                            NULL);
		}
	}
	/* Make sure there is no caller executing a probe when it is freed. */
	tracepoint_synchronize_unregister();
}

static void lookup_tracepoints(struct tracepoint *tp, void *ignore)
{
	int i;
	FOR_EACH_INTEREST(i) {
		if (strcmp(interests[i].name, tp->name) == 0) interests[i].value = tp;
	}
}

/**
 * trace_event_init - Connect each probe function to its tracepoint
 */
int trace_event_init(void)
{
	int i;

	// Install the tracepoints
	for_each_kernel_tracepoint(lookup_tracepoints, NULL);

	FOR_EACH_INTEREST(i) {
		if (interests[i].value == NULL) {
			printk("tracepoint - %s not found\n", interests[i].name);
			// Unload previously loaded
			trace_event_exit();
			return -1;
		}

		tracepoint_probe_register(interests[i].value, interests[i].fct, NULL);
		interests[i].init = 1;
	}

	return 0;
}
#else
static void trace_event_exit(void) {}
#endif

#ifdef USE_BLOCKING_KPROBE
/* kprobe pre_handler: called just before the probed instruction is executed */
static int __kprobes handler_prefork(struct kprobe *kprobe, struct pt_regs *regs)
{
	/* A dump_stack() here will give a stack backtrace */

	struct task_struct* p=(struct task_struct*) regs_get_kernel_argument(regs, 1);

	preempt_enable_notrace();
	pmcs_alloc_per_thread_data(regs_get_kernel_argument(regs, 0), p);

#ifdef DEBUG
	trace_printk("<%s> p->addr = 0x%p, ip = %lx, flags = 0x%lx\n",
	             kprobe->symbol_name, kprobe->addr, regs->ip, regs->flags);
#endif
	preempt_disable_notrace();

	return 0;
}

/* kprobe post_handler: called after the probed instruction is executed */
static void __kprobes handler_postfork(struct kprobe *p, struct pt_regs *regs,
                                       unsigned long flags)
{
#ifdef DEBUG
	trace_printk("<%s> p->addr = 0x%p, flags = 0x%lx\n",
	             p->symbol_name, p->addr, regs->flags);
#endif
}

/* For each probe you need to allocate a kprobe structure */
static struct kprobe kp = {
	.symbol_name	= "copy_semundo",
	.pre_handler = handler_prefork,
	.post_handler = handler_postfork,
};

#endif


/*** PMCTrack unpatched setup **/

int pmctrack_init(void)
{
	int err;

#if !defined CONFIG_FTRACE || !defined CONFIG_TRACEPOINTS || !defined CONFIG_DYNAMIC_FTRACE_WITH_REGS
	printk("No support for ftrace or tracepoints found in the kernel");
	return -1;
#endif

#ifndef CONFIG_PMCTRACK
	err = trace_event_init();
	if (err)
		return err;
#endif

	err = fh_install_hooks(ftrace_hooks, ARRAY_SIZE(ftrace_hooks));
	if (err) {
		trace_event_exit();
		return err;
	}

#ifdef USE_BLOCKING_KPROBE
	err= register_kprobe(&kp);
	if (err) {
		pr_err("register_kprobe failed, returned %d\n", err);
		fh_remove_hooks(ftrace_hooks, ARRAY_SIZE(ftrace_hooks));
		trace_event_exit();
		return err;
	}
#endif
	return 0;
}

void pmctrack_exit(void)
{
	fh_remove_hooks(ftrace_hooks, ARRAY_SIZE(ftrace_hooks));
	trace_event_exit();
#ifdef USE_BLOCKING_KPROBE
	unregister_kprobe(&kp);
#endif
}
