/*
 *  include/pmc/hl_events.h
 *
 *	Data types and functions to deal with high-level events (performance metrics)
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef HL_EVENTS_H
#define HL_EVENTS_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <pmc/common/pmc_const.h>
#define MAX_REL_ARGS 2


/*
 * This enum allows the definition of diferents relations
 * among high-level and low-level events
 */
typedef enum {
	op_division,
	op_multiplication,
	op_sum,
	op_substract,
	op_rate,
	op_rate2,
	op_none,
	_AVAILABLE_RELATIONS
} pmc_relation_mode_t;


/* This enum describes the edge kind in a relation */
typedef enum {
	hw_event_arg,
	metric_arg,
} arg_type_t;


/* It defines an argument of a relation */
typedef struct {
	arg_type_t type;	/* Type: Describes the array where
					its descriptor can be found */
	unsigned int index; 	/* Index of the related event */
} pmc_arg_t;

/* Structure that represents a performance metric */
typedef struct {
	char id[MAX_EXP_ID];		/* Metric name (ID)*/
	pmc_relation_mode_t mode;	/* Relation mode between arguments */
	pmc_arg_t arg1;			/* Relation's first component */
	pmc_arg_t arg2;			/* Relation's second component */
	/* Extra parameters: */
	unsigned long scale_factor; 	/* Fixed point management field (rate) */
	uint64_t count;			/* Last computed value */
}
pmc_metric_t;

/* Helper macros to build simple metric sets */
#define PMC_ARG(arg) {.type=hw_event_arg,.index=(arg)}
#define PMC_METRIC(id,op,arg1,arg2,scale_factor) {id,op,PMC_ARG(arg1),PMC_ARG(arg2),scale_factor,0}

/* Helper type for evaluating the value of hl_events */
typedef struct {
	arg_type_t type;
	uint64_t value;
} operand_t;


/* Compute the value of a performance metric */
void compute_value(pmc_metric_t* metric, uint64_t* hw_events, pmc_metric_t* metric_vector);

/* Retrieve operands of to compute the value of a performance metric */
void get_operands(pmc_metric_t* metric,
                  uint64_t* hw_events,
                  pmc_metric_t* metric_vector,
                  operand_t* operands,
                  unsigned int num_ops
                 );



/* Initialization function for performance metrics */
static inline void init_pmc_metric ( pmc_metric_t* metric,
                                     const char* name,		/* event name */
                                     pmc_relation_mode_t mode,	/* relation type between arguments */
                                     pmc_arg_t* arguments, 		/* Argument pair */
                                     unsigned long scale_factor )
{
	strcpy(metric->id,name);
	metric->count=0;
	metric->mode=mode;
	metric->arg1=arguments[0];
	metric->arg2=arguments[1];
	metric->scale_factor=scale_factor;
}

#define MAX_METRICS_PER_SET 8
#define MAX_MULTIPLEX_EXP_PER_CORETYPE 4

/* Set of metrics associated with a experiment set */
typedef struct {
	pmc_metric_t	metrics[MAX_METRICS_PER_SET];	/* HW counters opaque descriptor */
	unsigned int	size;		/* Number of HW counters used for this event*/
	int 		exp_idx;
}
metric_experiment_t;

/*
 * Makes it possible to define various performance
 * metrics in a scenario where event multiplexing is
 * enabled
 */
typedef struct {
	metric_experiment_t exps[MAX_MULTIPLEX_EXP_PER_CORETYPE];
	int nr_exps;
} metric_experiment_set_t;


/* Events graph initialization function (per CPU)*/
static inline void init_metric_experiment_t(metric_experiment_t* m_exp,int exp_idx)
{
	m_exp->size = 0;   		/* Empty set (no nodes)*/
	m_exp->exp_idx=exp_idx;
}

static inline void clone_metric_experiment_t(metric_experiment_t* dst,
        metric_experiment_t* orig)
{
	int i=0;
	dst->size = orig->size;
	dst->exp_idx = orig->exp_idx;

	for (i=0; i<orig->size; i++)
		dst->metrics[i]=orig->metrics[i];
}

/* Init a set of performance metrics */
static inline void init_metric_experiment_set_t(metric_experiment_set_t* m_set)
{
	int i=0;
	m_set->nr_exps = 0;   		/* Empty set (no nodes)*/
	for (i=0; i<MAX_MULTIPLEX_EXP_PER_CORETYPE; i++)
		init_metric_experiment_t(&m_set->exps[i],i);
}

static inline void clone_metric_experiment_set_t(metric_experiment_set_t* dst,
        metric_experiment_set_t* orig)
{
	int i=0;
	dst->nr_exps = orig->nr_exps;
	for (i=0; i<orig->nr_exps; i++)
		clone_metric_experiment_t(&dst->exps[i],&orig->exps[i]);
}


/* For SF-enabled monitoring modules */
static inline int normalize_speedup_factor (int sf)
{
	/* Normalization before anything else is done  */
	if (sf>1000) {
		/* NORMALIZE 1.333 ~ 1333 format to integer percentage (33)*/
		sf-=1000;
		sf=sf/10;
	} else {
		sf=0;
	}
	return sf;
}

static inline int denormalize_speedup_factor (int sf)
{
	if (sf>0) {
		sf=sf*10;
		sf+=1000;
	} else {
		sf=1000;
	}
	return sf;
}


/*** Common functions for monitoring modules ***/

/*
 * Obtain the values for a set of performance metrics
 * from HW event counts collected via PMCs (hw_events array)
 */
static inline void compute_performance_metrics(uint64_t* hw_events, metric_experiment_t* metric_exp)
{
	int i=0;

#ifdef DEBUG
	char buf[100]="";
	char* dst=buf;
#endif

	for (i=0; i<metric_exp->size; i++) {
		pmc_metric_t* metric=&metric_exp->metrics[i];
		compute_value(metric,hw_events,metric_exp->metrics);
#ifdef DEBUG
		dst+=sprintf(dst,"%s=%llu ",metric->id,metric->count);
#endif
	}

#ifdef DEBUG
	printk(KERN_ALERT "%s\n",buf);
#endif
}

/*
 * Macros, data structures and functions to
 * aid in detecting program-phase changes
 * based on high-level performance metrics collected
 * via hardware performance counters
 */

#define AMP_HISTORY_BIT_VECTOR_SIZE             10
#define AMP_HISTORY_BIT_VECTOR_MASK             0x3ff
#define AMP_HISTORY_LAST_BIT                    0x200
#define AMP_HISTORY_DEFAULT_STABLE_MASK         0x300   /* 2bits instead of three */

typedef struct {
	unsigned int change_vector;             /* History of last disabling actions (bit-buffer)
                                                  Init (all or nothing)*/
	unsigned int local_changes;             /* Number of last false disabling actions (bounded) */
	unsigned int local_stable_values;               /* Number of last true disabling actions  (bounded) */
	unsigned int cnt_changes;               /* Absolute counter */
	unsigned int cnt_stable_values;         /* Absolute counter */
	unsigned int running_average;            /* Inside, we also store the running average used for peak-detection */
} pmon_change_history_t;

int pmon_update_running_average(pmon_change_history_t* task_history, unsigned int last_value, int ra_factor,int ra_threshold, int percentage);

static inline void pmon_register_metric_change (pmon_change_history_t* task_history)
{
	unsigned int false_increment=(task_history->change_vector & 0x1);

	/*Update per-metric counters*/
	task_history->cnt_changes++;
	task_history->local_stable_values-=false_increment;
	task_history->local_changes+=false_increment;


	/*Update history (now is set to 0)*/
	task_history->change_vector=(task_history->change_vector>>1);

}

static inline void pmon_register_metric_stable_value (pmon_change_history_t* task_history)
{
	unsigned int true_increment=(task_history->change_vector & 0x1) ^ 0x1;


	/*Update per-metric counters*/
	task_history->cnt_stable_values++;
	task_history->local_stable_values+=true_increment;
	task_history->local_changes-=true_increment;


	/*Update history (now first bit is set to one)*/
	task_history->change_vector=((task_history->change_vector >>1 ) | AMP_HISTORY_LAST_BIT );


}


static inline void init_pmon_change_history(pmon_change_history_t* task_history)
{
	task_history->change_vector=AMP_HISTORY_BIT_VECTOR_MASK; /* No changes previously !! */
	/* History of last disabling actions (bit-buffer)
	                                  Init (all or nothing)*/
	task_history->local_changes=0;
	task_history->local_stable_values=AMP_HISTORY_BIT_VECTOR_SIZE;
	task_history->cnt_changes=0;
	task_history->cnt_stable_values=0;
	task_history->running_average=0; /* If zero, means that there will be a change in the beginning */
}

#include <pmc/data_str/mc_cbuffer.h>

typedef struct {
	mc_cbuffer cbuf;
	unsigned long acum;
	unsigned long average;
} metric_smoother_t;


static inline void init_metric_smoother(metric_smoother_t* mt, int sample_window_length, unsigned long initial_value)
{
	init_mc_cbuffer(&mt->cbuf,sample_window_length);
	mt->acum=mt->average=initial_value;
	push_mc_cbuffer(&mt->cbuf,initial_value);

}

static inline unsigned long add_new_metric_sample_mst(metric_smoother_t* mt,unsigned long cur_value)
{

	if (is_full_mc_cbuffer(&mt->cbuf)) {
		unsigned long prev=*head_mc_cbuffer(&mt->cbuf);
		mt->acum-=prev;
	}

	// Update average
	push_mc_cbuffer(&mt->cbuf,cur_value);
	mt->acum+=cur_value;
	mt->average=mt->acum/mt->cbuf.size;

	return mt->average;
}

static inline unsigned long get_cur_average_mst(metric_smoother_t* mt)
{
	return mt->average;
}

/* Helper functions for monitoring modules */
int estimate_sf_additive(uint64_t* metrics,int* adregression_spec,int correction_factor);
int estimate_sf_regression(uint64_t* metrics,int* regression_spec);
void fill_in_sf_metric_vector(metric_experiment_set_t* metric_set,
                              uint64_t* sf_metric_vector,unsigned int* size);

#endif
