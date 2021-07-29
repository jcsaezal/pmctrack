/*
 *  mc_experiments.c
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 *
 *  Adaptation of the architecture-independent code for patchless PMCTrack
 *  by Lazaro Clemen and Juan Carlos Saez
 *  (C) 2021 Lazaro Clemen Palafox <lazarocl@ucm.es>
 *  (C) 2021 Juan Carlos Saez <jcsaezal@ucm.es>
 */

#include <pmc/mc_experiments.h>
#include <pmc/hl_events.h>
#include <linux/module.h>
#include <pmc/pmu_config.h>
#include <pmc/data_str/phase_table.h>

#if defined(_DEBUG_USER_MODE)
#include <printk.h>
#elif defined(__SOLARIS__)
#include <sys/pghw.h> /* For topology */
#include <sys/cpuvar.h> /* For topology */
#include <sys/amp.h>
#endif /* __LINUX__ && _DEBUG_USER_MODE */
#if defined(__LINUX__)
#include <linux/kernel.h>
#include <asm/div64.h>          /* For do_div(). */
#else	/* Solaris kernel */
/* Nothing at all */
#endif

/* Initialize core_experiment_t structure */
void init_core_experiment_t(core_experiment_t* c_exp,int exp_idx)
{
	int i=0;
	c_exp->size = 0;   		/* Empty set (no nodes)*/
	c_exp->used_pmcs = 0;   	/* For now no pmcs are used */
	c_exp->ebs_idx=-1;		/* EBS disabled by default -1 */
	c_exp->need_setup = 1;          /* Requires configuration on related CPU*/
	c_exp->exp_idx=exp_idx;

	for (i=0; i<MAX_LL_EXPS; i++) {
		c_exp->log_to_phys[i]=-1;
		c_exp->phys_to_log[i]=-1;
		c_exp->nr_overflows[i]=0;
	}
}

#ifndef CONFIG_PMC_PERF
/* Monitor resets the Global PMU context (per CPU) ==> for 'old' events */
void restore_context_perfregs ( core_experiment_t* ce )
{
	unsigned int j;

	for ( j=0; j<ce->size; j++ ) {
		low_level_exp* lle=&ce->array[j];
		__stop_count ( lle );
		__clear_count ( lle );
	}
}

/* Restart PMCs used by a core_experiment_t */
void mc_restart_all_counters(core_experiment_t* core_experiment)
{

	unsigned int j;

	/* Counters Reset action (new hardware events)*/
	for(j=0; j<core_experiment->size; j++) {
		low_level_exp* lle = &core_experiment->array[j];
		__restart_count(lle);
	}

	/* Current CPU PMU Context is ready => flag cleared */
	core_experiment->need_setup = 0;
	reset_overflow_status();
}

/* Stop PMCs used by a core_experiment_t */
void mc_stop_all_counters(core_experiment_t* core_experiment)
{
	unsigned int j;

	/* Counters Reset action (new hardware events)*/
	for(j=0; j<core_experiment->size; j++) {
		low_level_exp* lle = &core_experiment->array[j];
		__stop_count(lle);
	}
	reset_overflow_status();
}


/* Clear PMCs used by a core_experiment_t */
void mc_clear_all_counters(core_experiment_t* core_experiment)
{
	unsigned int j;
	/* Counters Reset action (new hardware events)*/
	for(j=0; j<core_experiment->size; j++) {
		low_level_exp* lle = &core_experiment->array[j];
		__clear_count(lle);
	}
	reset_overflow_status();
}

/* For EBS: Save context associated with PMCs (context switch out) */
void mc_save_all_counters(core_experiment_t* core_experiment)
{
	unsigned int j;

	/* Counters Reset action (new hardware events)*/
	for(j=0; j<core_experiment->size; j++) {
		low_level_exp* lle = &core_experiment->array[j];
		__save_context_event(lle);
	}

	/* Current CPU PMU Context is ready => flag cleared */
	core_experiment->need_setup = 0;
}

/* Restore context associated with PMCs (context switch in) */
void mc_restore_all_counters(core_experiment_t* core_experiment)
{
	unsigned int j;

	/* Counters Reset action (new hardware events)*/
	for(j=0; j<core_experiment->size; j++) {
		low_level_exp* lle = &core_experiment->array[j];
		/* Start count now does the trick, since it loads whatever there was in PMCs before */
		__start_count(lle);
	}

	/* Current CPU PMU Context is ready => flag cleared */
	core_experiment->need_setup = 0;

	reset_overflow_status();
}
#endif


#ifdef CONFIG_PMC_PERF
int clone_core_experiment_set_t(core_experiment_set_t* dst,core_experiment_set_t* src, struct task_struct* p)
{
	int i=0,j=0,k=0;
	core_experiment_t* exp=NULL;
	int error=0;
	pmc_config_set_t* config;

	dst->nr_exps=0;
	dst->cur_exp=0;

	/* Just memory allocation */
	for (i=0; i<src->nr_configs; i++) {
		exp= (core_experiment_t*) kmalloc(sizeof(core_experiment_t), GFP_KERNEL);
		if (!exp) {
			error=-ENOMEM;
			goto free_allocated_experiments;
		}
		dst->exps[i]=exp;
		dst->nr_exps++;
		/* Copy configuration */
		memcpy(&dst->pmc_config[dst->nr_configs++],&src->pmc_config[i],sizeof(pmc_config_set_t));

	}

	/* No need to check user-provided configuration */
	/* Now proceed to setup config */
	for (i=0; i<dst->nr_configs; i++) {
		config=&dst->pmc_config[i];

		if (config->coretype==-1) {
			error=-ENOTSUPP;
			goto free_perf_events;
		}

		for (k=0; k<MAX_LL_EXPS; k++) {
			if ( config->used_pmcs & (0x1<<k)) {
				/* Update force enable flag */
				config->pmc_cfg[k].cfg_force_enable=1;
			}
		}

		error=do_setup_pmcs(config->pmc_cfg,config->used_pmcs,dst->exps[i],get_any_cpu_coretype(config->coretype),i,p);

		if (error)
			goto free_perf_events;
	}

	return 0;
free_perf_events:
	for (j=0; j<i; j++) {
		/* Release perf events as well */
		for (k=0; k<dst->exps[j]->size; k++) {
			if (dst->exps[j]->array[k].event.event)
				perf_event_release_kernel(dst->exps[j]->array[k].event.event);
		}
	}

free_allocated_experiments:
	for (i=0; i<dst->nr_exps; i++) {
		kfree(dst->exps[i]);
		dst->exps[i]=NULL;
	}
	return error;
}
#else
int clone_core_experiment_set_t(core_experiment_set_t* dst,core_experiment_set_t* src, struct task_struct* p)
{
	int i=0,j=0;
	core_experiment_t* exp=NULL;

	dst->nr_exps=0;
	dst->cur_exp=0;

	for (i=0; i<src->nr_exps; i++) {
		if (src->exps[i]!=NULL) {
			exp= (core_experiment_t*) kmalloc(sizeof(core_experiment_t), GFP_KERNEL);
			if (!exp)
				goto free_allocated_experiments;
			memcpy(exp,src->exps[i],sizeof(core_experiment_t));
			/* Do not inherit overflow counters */
			for (j=0; j<MAX_LL_EXPS; j++)
				exp->nr_overflows[j]=0;
			dst->exps[i]=exp;
			dst->nr_exps++;
		}

	}

	return 0;
free_allocated_experiments:
	for (i=0; i<dst->nr_exps; i++) {
		kfree(dst->exps[i]);
		dst->exps[i]=NULL;
	}
	return -ENOMEM;
}
#endif

#ifndef CONFIG_PMCTRACK
DEFINE_RWLOCK(gone_tasks_lock);
LIST_HEAD(gone_tasks);

void init_prof_exited_tasks(void)
{
	INIT_LIST_HEAD(&gone_tasks);
}

void add_prof_exited_task(pmon_prof_t *prof)
{
	unsigned long flags;

	write_lock_irqsave(&gone_tasks_lock,flags);
	list_add_tail(&prof->links,&gone_tasks);
	write_unlock_irqrestore(&gone_tasks_lock,flags);
}

pmon_prof_t * get_prof_exited_task(struct task_struct* p)
{
	unsigned long flags;
	pmon_prof_t * prof=NULL;
	pmon_prof_t * next;

	read_lock_irqsave(&gone_tasks_lock,flags);

	list_for_each_entry(next,&gone_tasks,links) {
		if (next->this_tsk==p) {
			prof=next;
			break;
		}
	}
	read_unlock_irqrestore(&gone_tasks_lock,flags);

	return prof;
}

void del_prof_exited_task(pmon_prof_t *prof)
{
	unsigned long flags;

	write_lock_irqsave(&gone_tasks_lock,flags);
	list_del(&prof->links);
	write_unlock_irqrestore(&gone_tasks_lock,flags);
}

#endif

/******************** Functions related to the computation of high-level performance metrics **************************/

/*
 * This function computes the value of a performance metric (pmc_metric_t) according to its definition
 * and the values of the related events (operands)
 */
void compute_value(pmc_metric_t* metric, uint64_t* hw_events, pmc_metric_t* metric_vector)
{
	operand_t operands[2];
	uint64_t big_buffer = 0;
	uint64_t aux;

	switch(metric->mode) {
	case op_division:
		get_operands(metric, hw_events, metric_vector, operands, 2);
		if(operands[1].value != 0) {
			metric->count = operands[0].value;
			pmc_do_div(metric->count,operands[1].value);
		} else {
			/*Division by zero*/
			metric->count = 0;
		}
		break;
	case op_multiplication:
		get_operands(metric, hw_events, metric_vector, operands, 2);
		metric->count = operands[0].value*operands[1].value;
		break;
	case op_sum:
		get_operands(metric, hw_events, metric_vector, operands, 2);
		metric->count = operands[0].value+operands[1].value;
		break;
	case op_substract:
		get_operands(metric, hw_events, metric_vector, operands, 2);
		if(operands[0].value >= operands[1].value ) {
			metric->count = operands[0].value-operands[1].value;
		}
		metric->count=0;
		break;
	case op_rate:
		get_operands(metric, hw_events, metric_vector, operands, 2);
		if(operands[1].value != 0) {
			big_buffer = operands[0].value ;
			big_buffer *= metric->scale_factor;
			pmc_do_div(big_buffer,operands[1].value); /* big_buffer/=operands[1].value; */
			metric->count = big_buffer;
		} else {
			/*Division by zero*/
			metric->count=0;
		}
		break;
	case op_rate2:
		get_operands(metric, hw_events, metric_vector, operands, 2);
		if(operands[1].value != 0) {
			aux=operands[1].value;
			pmc_do_div(aux,metric->scale_factor);
			if (aux == 0) {
				metric->count=0;
			} else {
				big_buffer=operands[0].value;
				pmc_do_div(big_buffer,aux);
				metric->count = big_buffer;
			}
		} else {
			/*Division by zero*/
			metric->count=0;
		}
		break;
	case op_none:
		get_operands(metric, hw_events, metric_vector, operands, 1);

		if(operands[1].value != 0) {
			big_buffer = operands[0].value ;
			big_buffer *= metric->scale_factor;
			metric->count = big_buffer;
		} else {
			/*Division by zero*/
			metric->count=0;
		}
		break;
	default:
		metric->count = 0;
		break;
	}
}

/* This function builds a structure with the descriptors
	of hardware events and metrics related to a given metric (hle)  */
void get_operands(pmc_metric_t* metric,
                  uint64_t* hw_events,
                  pmc_metric_t* metric_vector,
                  operand_t* operands,
                  unsigned int num_ops
                 )
{
	unsigned int i, ref = 0;
	pmc_arg_t* argument = &metric->arg1;

	for(i=0; i<num_ops; i++) {
		ref=argument->index;
		switch(argument->type) {
		case hw_event_arg:
			operands[i].type = hw_event_arg;
			operands[i].value = hw_events[ref];
			break;
		case metric_arg:
			operands[i].type = metric_arg;
			operands[i].value = metric_vector[ref].count;
			break;

		}
		argument=&metric->arg2;
	}

}

/* Returns whether there has been a change or not in the average before updating */
int pmon_update_running_average(pmon_change_history_t* task_history, unsigned int last_value, int ra_factor,int ra_threshold, int percentage)
{
	int old_r_average=task_history->running_average;
	int cur_r_average=old_r_average;
	int cur_value=last_value;
	int axis=0;
	int epsilon; /* Variation radius */
	int vmax,vmin;

	cur_r_average=(((int) ra_factor) * (cur_r_average -cur_value)) / 100;

	/* Update the running average */
	cur_r_average=cur_r_average + cur_value ;

	/*TODO (not always good this update)
	         Update the running average */
	task_history->running_average=cur_r_average;

	/* Add ten for the low cases */
	cur_r_average+=10;
	old_r_average+=10;

	if (cur_r_average > old_r_average) {
		vmax=cur_r_average;
		vmin=old_r_average;
	} else {

		vmin=cur_r_average;
		vmax=old_r_average;
	}

	axis=(vmax+vmin)>>1; /* Divided by 2 */

	/* Check the variation computing the allowed variation */
	if (percentage)
		epsilon=(((int)ra_threshold)*axis) /100 ;
	else
		epsilon=(int)ra_threshold;


	if ((epsilon == 0) /* Metric really low (no variation will be detected) */
	    || ( (vmax <= axis + epsilon))
	   ) {
		return 0;
	} else {
		return 1; /* Variation on the running average detected*/
	}
}

#if !defined(PMCTRACK_QUICKIA) && !defined(SCHED_AMP)
int get_any_cpu_coretype(int coretype)
{
	int i=0;

	/* Search CPU */
	while (i<num_present_cpus() && coretype_cpu[i]!=coretype)
		i++;

	if (i==nr_cpu_ids)
		return -1;
	else
		return i;
}
#endif


void noinline pmc_samples_buffer_overflow(pmc_samples_buffer_t* sbuf)
{
	asm(" ");
}

/* Allocate a buffer with capacity 'size_bytes' */
pmc_samples_buffer_t* allocate_pmc_samples_buffer(unsigned int size_bytes)
{
	pmc_samples_buffer_t* pmc_samples_buf=NULL;

	pmc_samples_buf=kmalloc(sizeof(pmc_samples_buffer_t),GFP_KERNEL);

	if (!pmc_samples_buf)
		return NULL;

	pmc_samples_buf->pmc_samples=create_cbuffer_t(size_bytes);

	if (!pmc_samples_buf->pmc_samples) {
		kfree(pmc_samples_buf);
		return NULL;
	}

	sema_init(&pmc_samples_buf->sem_queue,0);
	spin_lock_init(&pmc_samples_buf->lock);
	atomic_set(&pmc_samples_buf->ref_counter,1);

	pmc_samples_buf->monitor_waiting=0;

	return pmc_samples_buf;
}


int estimate_sf_additive(uint64_t* metrics,int* adregression_spec,int correction_factor)
{
	int sf=adregression_spec[0]+correction_factor;
	int sfmin=adregression_spec[1];
	int sfmax=adregression_spec[2];
	int idx=4;
	int metric_id;

	while(adregression_spec[idx]!=-1) {
		metric_id=adregression_spec[idx];
		if (metrics[metric_id]<=adregression_spec[idx+1]) {
			sf+=adregression_spec[idx+2];
		} else {
			sf+=adregression_spec[idx+3];
		}
		idx+=4;
	}

	if (sf<sfmin) {
		sf=sfmin;
	} else if (sf>sfmax) {
		sf=sfmax;
	}

	return sf;
}

int estimate_sf_regression(uint64_t* metrics,int* regression_spec)
{
	int sf=regression_spec[0];
	int sfmin=regression_spec[1];
	int sfmax=regression_spec[2];
	int idx=3;
	int metric_id;
	int factor;

	while(regression_spec[idx]!=-1) {
		metric_id=regression_spec[idx];
		factor=(((int)metrics[metric_id])*regression_spec[idx+1])/regression_spec[idx+2];
		sf+=factor;
		idx+=3;
#ifdef DEBUG
		trace_printk("Factor=%i\n",factor);
#endif
	}

#ifdef DEBUG
	trace_printk("Estimated value=%i\n",sf);
#endif

	if (sf<sfmin) {
		sf=sfmin;
	} else if (sf>sfmax) {
		sf=sfmax;
	}

	return sf;
}


void fill_in_sf_metric_vector(metric_experiment_set_t* metric_set, uint64_t* sf_metric_vector,unsigned int* size)
{
	int i,j;
	unsigned int acum_ipc=0;
	metric_experiment_t* mex;
	int dst_idx=1; /* Ipc goes later */

	for (i=0; i<metric_set->nr_exps; i++) {
		mex=&metric_set->exps[i];
		acum_ipc+=mex->metrics[0].count; //Acum ipc (it's always metric zero )
		for (j=1; j<mex->size; j++)
			sf_metric_vector[dst_idx++]=mex->metrics[j].count;
	}

	sf_metric_vector[0]=acum_ipc/metric_set->nr_exps;
	(*size)=dst_idx;

}

/********* Implementation of operations on the phase table ************/
#include <linux/list.h>
#include <linux/vmalloc.h>

/* Generic nodes for the doubly linked list used in the table  */
typedef struct {
	struct list_head links; 	/* for the linked list */
	int idx;					/* Index for the allocator */
	void* data; 				/* Phase object */
} phase_node_t;

struct phase_table {
	struct list_head phases;	/* List of phases */
	int nr_phases;				/* Current number of phases */
	int max_phases;				/* Max phases to be stored on the list */
	size_t phase_struct_size;	/* Size of the phase structure */
	void* phase_pool;			/* Memory pool (pre-allocated table entries) */
	phase_node_t* node_pool;	/* Memory pool (pre-allocated table entries) */
	unsigned long bitmap;		/* bitmask to keep track of free and occupied entries (1 means free, 0 -> occupied) */
	int (*compare)(void*,void*,void*); /* Comparison operation (returns 0 if equals or Manhatan distance) */
};

/* Create a phase table */
phase_table_t* create_phase_table(unsigned int max_phases, size_t phase_struct_size, int (*compare)(void*,void*,void*))
{
	phase_table_t* table=NULL;
	const unsigned int max_bits=sizeof(unsigned long)*8;
	void* phase_pool=NULL;
	void* node_pool=NULL;
	int i=0;

	/* Make sure we can keep track of everything on the bitmap */
	if (max_phases>max_bits)
		return NULL;

	/* Allocate memory */
	if ((phase_pool=vmalloc(phase_struct_size*max_phases))==NULL)
		goto free_up_resources;

	if ((node_pool=vmalloc(sizeof(phase_node_t)*max_phases))==NULL)
		goto free_up_resources;

	if ((table=vmalloc(sizeof(phase_table_t)))==NULL)
		goto free_up_resources;

	/* Initialize phase table */
	INIT_LIST_HEAD(&table->phases);
	table->nr_phases=0;
	table->max_phases=max_phases;
	table->phase_struct_size=phase_struct_size;
	table->phase_pool=phase_pool;
	table->node_pool=node_pool;
	if (max_phases==max_bits)
		table->bitmap=~(0); /* All 1s */
	else
		table->bitmap=(1<<max_phases)-1; /* 2^max_phases -1 */
	table->compare=compare;

	/* Prepare pointers (Dark pointer arithmetic operation */
	for (i=0; i<max_phases; i++) {
		table->node_pool[i].data=(((char*)phase_pool) + i*phase_struct_size);
		table->node_pool[i].idx=i;
	}

	return table;
free_up_resources:
	if (node_pool)
		vfree(node_pool);
	if (phase_pool)
		vfree(phase_pool);
	if (table)
		vfree(table);
	return NULL;
}

/* Free up resources associated with a phase table */
void destroy_phase_table(phase_table_t* table)
{
	if (table->node_pool)
		vfree(table->node_pool);
	if (table->phase_pool)
		vfree(table->phase_pool);
	vfree(table);
}

/* Retrieve the most similar phase in a phase table */
void* get_phase_from_table(phase_table_t* table, void* key_phase, void* priv_data, int* similarity, int* index)
{
	phase_node_t* cur_phase;
	phase_node_t* selected_phase=NULL;

	struct list_head* p;
	int sim_test=0;
	int sim_min=INT_MAX;

	list_for_each(p,&table->phases) {
		cur_phase=list_entry(p,phase_node_t,links);

		/* Invoke comparator function */
		sim_test=table->compare(cur_phase->data,key_phase,priv_data);

		if (sim_test<sim_min) {
			sim_min=sim_test;
			selected_phase=cur_phase;

			/* Exact match found */
			if (sim_test==0)
				break;
		}
	}

	if (!selected_phase)
		return NULL;

	(*similarity)=sim_min;
	(*index)=selected_phase->idx;
	return selected_phase->data;
}

/* Move table entry in the index position to the beginning of the linked list */
int promote_table_entry(phase_table_t* table, int index)
{

	struct list_head* node;

	/* Check if index is valid and corresponds to a valid entry */
	if ((index<0) || (index>=table->max_phases) || ((1<<index) & table->bitmap))
		return -ENOENT;

	/* Point to the phase's node by accessing the node pool */
	node=&table->node_pool[index].links;

	/* If it is not the first one already, make this entry the first one */
	if (table->phases.next!=node) {
		list_del(node);
		list_add(node,&table->phases);
	}

	return 0;
}

/* Find a free entry in the phase table */
static phase_node_t* get_free_phase_loc(phase_table_t* table)
{
	int i=0;

	if (table->bitmap==0)
		return NULL;

	/* Locate first 1 in here */
	for (i=0; i<table->max_phases && !((1<<i) & table->bitmap); i++ ) {}

	if (i==table->max_phases)
		return NULL;
	else
		return &table->node_pool[i];
}

/* Insert a new phase into the phase table */
void* insert_phase_in_table(phase_table_t* table, void* phase)
{
	phase_node_t* free_phase_loc;

	/* Get free location */
	free_phase_loc=get_free_phase_loc(table);

	/* If it is full -> reuse tail (oldest phase is evicted) */
	if (free_phase_loc==NULL) {
		free_phase_loc=list_entry(table->phases.prev,phase_node_t,links);
	} else {
		table->bitmap&=~(1<<free_phase_loc->idx); /* Clear bit */
		table->nr_phases++;
		/* Newer phases inserted at the beginning */
		list_add(&free_phase_loc->links,&table->phases);
	}

	/* Copy data */
	memcpy(free_phase_loc->data,phase,table->phase_struct_size);
	return free_phase_loc->data;
}


extern struct pid *find_pid_ns(int nr, struct pid_namespace *ns);
extern struct task_struct *pid_task(struct pid *pid, enum pid_type type);
extern struct pid_namespace *task_active_pid_ns(struct task_struct *tsk);

struct task_struct *pmctrack_find_process_by_pid(pid_t pid)
{
	if (pid) {
		struct pid_namespace *ns = task_active_pid_ns(current);

		RCU_LOCKDEP_WARN(!rcu_read_lock_held(),
		                 "find_task_by_pid_ns() needs rcu_read_lock() protection");
		return pid_task(find_pid_ns(pid, ns), PIDTYPE_PID);
	} else {
		return current;
	}
}

