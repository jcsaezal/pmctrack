/*
 *  mc_experiments.c
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#include <pmc/mc_experiments.h>
#include <pmc/hl_events.h>
#include <linux/module.h>
#include <pmc/pmu_config.h>

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
		/*Just gets the value from the first related argument's descriptor*/
		metric->count = operands[0].value;
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

