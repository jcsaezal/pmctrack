/*
 *  cache_part.h
 *
 *  OS-level resource-management framework based on
 *  HW extensions for cache-partitioning and memory-bandwidth limitation
 *
 *  Copyright (c) 2021 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef PMC_CACHE_PART_H
#define PMC_CACHE_PART_H

#include <pmc/data_str/sized_list.h>
#include <pmc/intel_rdt.h>

struct cache_part_set;

/* Structures to manage cache partitions */
typedef struct {
	unsigned int low_way; 	/* Lowest way assigned to the partition */
	unsigned int high_way;	/* Highest way assigned to the partition */
	unsigned int nr_ways;	/* Size of the partition (Number of ways) */
	int clos_id; 	/* CLOS assigned to the partition (assigned at the beginning) */
	int part_id;	/* ID of the partition (changes over time) */
	int part_index; /* To make it easier PART ASSIGNMENT */
	unsigned int part_mask;
	unsigned int part_old_mask;
	unsigned int nr_apps;	/* Number of applications assigned to that partition */
	unsigned char has_extra_way; /* For the way bouncing algorithm */
	int bias; /* Counter that indicates the number of cycles the partition was assigned an extra way */
	struct list_head links;	/* To store the partition in a linked list */
	struct list_head links_sorted;
	struct list_head links_removal;
	sized_list_t assigned_applications;
	/* Temporal fields for partitioning algs */
	int nr_light_sharing;
	int marked_for_removal;
	struct cache_part_set* pset; /* Pointer to set of cache partition this partition belongs to */
} cat_cache_part_t;


enum {
	CACHE_CLASS_UNKOWN=-1,
	CACHE_CLASS_LIGHT=0,
	CACHE_CLASS_STREAMING=1,
	CACHE_CLASS_SENSITIVE=2,
	NR_CACHE_CLASSES=3
};

/* Static properties for input file */
struct benchmark_properties {
	char *name;
	unsigned char type;
	int bizarre;
	int* curve;
	int* space_curve;
};

extern struct benchmark_properties foo_properties;

#define MAX_CACHE_WAYS 20 /* Debussy setting */

/** For flags field ***/
#define ISOLATED_STREAMING 0x1

/* Cacheman */
enum {
	CMCLASS_EXCESS=0,
	CMCLASS_POOR,
	CMCLASS_ADEQUATE,
	CMCLASS_OTHER,
	CMCLASS_NR_CLASSES
};

enum {
	CMCHANGE_NONE=0,
	CMCHANGE_UP,
	CMCHANGE_DOWN,
};

#define TMON_MAX_THREADS 64

typedef struct {
	/* Time spent with thread count */
	ktime_t stats[TMON_MAX_THREADS+1];
	ktime_t last_threadcount_update;
	unsigned long nr_changes;
	unsigned long count_mask;
	/* Max thread count reached during exploration */
	unsigned int nr_max_threads;
	ktime_t aggregate_cpu_usage;
	unsigned char track_cpu_usage;
	ktime_t last_cpu_usage_reset;
} thread_count_stats_t;


/* Structure that represents a single- or multi- threaded application */
typedef struct app {
	sized_list_t app_active_threads;
	struct list_head link_active_apps;
	struct list_head link_in_partition;
	struct list_head link_app_type; /* Temporal link for the partitioning algorithm */
	struct list_head link_moved; /* To transfer apps from partitions about to be removed and so on */
	struct list_head link_defered_cos_assignment; /* To update COS later with interrupts enabled */
	intel_cmt_thread_struct_t app_cmt_data; /* Global (per application, data) */
	cat_cache_part_t* cat_partition; /* Pointer to the cache partition associated with the application */
	int last_partition; /* To be used as a hint by the repartitioning algorithm */
	int is_memory_intensive; /* Temporary field for the algorithm */
	int was_moved; /* Temporary field for repartitioning algorithm */
	/* Attributes for LFOC */
	struct list_head link_class;
	struct list_head link_cluster;
	int app_id; /* For debugging */
	char app_comm[TASK_COMM_LEN];
	int type; /* Application class ... */
	int prev_type; /* Last App class (history) */
	int curve[MAX_CACHE_WAYS+1]; /* First item indicates the number of items in queue */
	int space_curve[MAX_CACHE_WAYS+1];
	struct benchmark_properties* sprops;
	int static_type;
	int force_class; /* For debugging */
	int sensitive_id; /* WATCH OUT: Id for pair_clustering (internal naming!) */
	struct task_struct* process; /* For debugging with stap */
	struct task_struct* master_thread; /* For basic support for MT apps */
	struct list_head link_profiling;
	int profiling_scheduled;
	unsigned long next_periodic_profile;
	unsigned long last_profiling;
	int critical_point;
	atomic_t ref_counter; /* Apparently it remains unused in PMCSched */
	atomic_t thread_class_counters[NR_CACHE_CLASSES];
	unsigned char externally_managed_cos_id; /* To avoid dynamic cos assignments */
	unsigned long last_llc_cbm;				/* For efficiency in updating CAT registers */
	atomic_t sensitivity_acum;
	unsigned long sensitivity;
	unsigned long last_inactivity;
	unsigned long flags; /* Extended field for special extensions */
	/* Cacheman fields */
	unsigned int cm_class;
	unsigned int cm_level; /* Max= way count */
	int cm_llco_base;
	int cm_llco_last;
	int cm_excess; /* To sort for distribution */
	unsigned char cm_level_change;
	/* Reuse link_profiling for lists */
	int active_count;
	int inactive_count;
	thread_count_stats_t tcstats;
} app_t;


typedef struct cache_part_set {
	sized_list_t assigned_partitions;
	sized_list_t free_partitions;
	sized_list_t defered_cos_assignment; /* To update COS later with interrupts enabled */
	cat_cache_part_t* partition_pool;
	cat_cache_part_t* default_partition; /* Default partition for newcomers */
	unsigned int nr_bouncing_ways; /* Number of ways moving along */
	intel_cat_support_t* cat_support; /* Global CAT properties (it is convenient to store the pointer here) */
} cache_part_set_t;


/* Per-CPU structure to implement IPI-based partition migrations */
struct clos_cpu {
	int cpu;
	unsigned int cos_id;
	unsigned int rmid;
	unsigned char used;
	struct list_head link;
};

/* Operations on individual partitions */
void remove_application_from_partition(app_t* app);
static inline int get_load_partition(cat_cache_part_t* partition)
{
	return partition->nr_apps;
}


void reconfigure_partition(cat_cache_part_t* part, unsigned int ways_assigned, unsigned int low_way);
void reconfigure_partition_gen(cat_cache_part_t* part, unsigned int ways_assigned, unsigned int low_way, unsigned char update_hw);

static inline unsigned int part_available(cat_cache_part_t* part, unsigned int available, unsigned int reserved)
{
	unsigned int avail=(1<<part->part_index) & available;

	return avail & (~reserved);
}

static inline unsigned int part_available_idx(int idx, unsigned int available, unsigned int reserved)
{
	unsigned int avail=(1<<idx) & available;

	return avail & (~reserved);
}
/* Operations on cache partition sets  */
int init_cache_part_set(cache_part_set_t* part_set, intel_cat_support_t* cat_support);
void free_up_part_set(cache_part_set_t* part_set);
cat_cache_part_t* allocate_new_partition(cache_part_set_t* pset, unsigned int nr_ways, int hint_id);
cat_cache_part_t* allocate_empty_partition(cache_part_set_t* part_set);
cat_cache_part_t* __get_partition_by_index(cache_part_set_t* part_set, int idx);
void move_app_to_partition(app_t* app, cat_cache_part_t* new_partition);
void assign_partition_to_application(cache_part_set_t* part_set,app_t* app, cat_cache_part_t* forced);
void print_partition_info(cache_part_set_t* part_set);
void deallocate_partition_no_resize(cache_part_set_t* pset, cat_cache_part_t* partition);
void remove_empty_partitions(cache_part_set_t* pset, int auto_resize);
static inline void update_default_partition(cache_part_set_t* pset, cat_cache_part_t* new_default)
{
	pset->default_partition=new_default;
}

static inline cat_cache_part_t* get_default_partition(cache_part_set_t* pset)
{
	return pset->default_partition;
}

static inline sized_list_t* get_assigned_partitions(cache_part_set_t* pset)
{
	return &pset->assigned_partitions;
}

static inline sized_list_t* get_deferred_cos_assignment(cache_part_set_t* pset)
{
	return &pset->defered_cos_assignment;
}

static inline intel_cat_support_t* get_cat_support(cache_part_set_t* pset)
{
	return pset->cat_support;
}

/* Explicitly update registers if necessary (change ocurred) */
void refresh_hw_partition_app(app_t* app);

#ifdef CONFIG_X86
/* To simplify implementation of IPI-based COS update operations */
struct clos_cpu* get_clos_pool_cpu(int cpu);
void initialize_clos_cpu_pool(void);
/* Update CLOS-related registers of those entries stored in the list */
void update_clos_app_unlocked(sized_list_t* clos_cpu_list);
void release_clos_cpu_list(sized_list_t* clos_cpu_list);
#else
static inline struct clos_cpu* get_clos_pool_cpu(int cpu)
{
	return NULL;
}
static inline void initialize_clos_cpu_pool(void) { }
static inline void update_clos_app_unlocked(sized_list_t* clos_cpu_list) { }
static inline void release_clos_cpu_list(sized_list_t* clos_cpu_list) { }
#endif




static inline void init_thread_count_stats(thread_count_stats_t* tstats)
{
	int i=0;
	ktime_t now=ktime_get();

	for (i=0; i<TMON_MAX_THREADS+1; i++) {
		tstats->stats[i]=0;
	}

	tstats->nr_changes=0;
	tstats->count_mask=0;
	tstats->last_threadcount_update=now;
	tstats->nr_max_threads=0;
	tstats->nr_max_threads=0;
	tstats->aggregate_cpu_usage=0;
	tstats->track_cpu_usage=0;
	tstats->last_cpu_usage_reset=now;
}


static inline void  update_thread_count_stats_time(thread_count_stats_t* tstats, int old_count, int new_count, ktime_t now)
{
	ktime_t delta;

	/* Ignore */
	if (old_count<0) {
		return;
	}

	/* Accumulate for old thread count */
	delta=ktime_sub(now,tstats->last_threadcount_update);
	tstats->last_threadcount_update=now;

	/* Accumulate for old thread count (raw value) */
	if (tstats->track_cpu_usage)
		tstats->aggregate_cpu_usage=ktime_add(tstats->aggregate_cpu_usage,
		                                      old_count * delta);

	/* Saturate counter */
	if (old_count>TMON_MAX_THREADS)
		old_count=TMON_MAX_THREADS;

	/* Be carfeful with this too as we might run into buffer overflow */
	if (new_count>TMON_MAX_THREADS)
		new_count=TMON_MAX_THREADS;

	/* Do not accumulate if a reset hapenned */
	if (old_count==0 || !(tstats->count_mask & (1ULL<<(old_count-1))))
		tstats->stats[old_count]=delta;
	else
		tstats->stats[old_count]+=delta;

	/* Enable the bit */
	if (old_count>0)
		tstats->count_mask|=(1ULL<<(old_count-1));

	/* Update changes counter if necessary */
	if (old_count!=new_count)
		tstats->nr_changes++;

	/* Update maximum */
	if (new_count>tstats->nr_max_threads)
		tstats->nr_max_threads=new_count;
}


static inline void  update_thread_count_stats(thread_count_stats_t* tstats, int old_count, int new_count)
{
	update_thread_count_stats_time(tstats, old_count, new_count, ktime_get());
}


static inline void reset_thread_count_stats_time(thread_count_stats_t* tstats, int current_thread_count, ktime_t now)
{
	tstats->stats[0]=0;	/* Reset counter for 0 threads */
	tstats->nr_changes=0; /* No changes! */

	if (current_thread_count>TMON_MAX_THREADS)
		current_thread_count=TMON_MAX_THREADS;

	tstats->nr_max_threads=current_thread_count;
	tstats->last_threadcount_update=now;
	tstats->last_cpu_usage_reset=now;
	tstats->aggregate_cpu_usage=ktime_set(0,0);
	/* Reset mask */
	tstats->count_mask=0;
}

static inline void reset_thread_count_stats(thread_count_stats_t* tstats, int current_thread_count)
{
	reset_thread_count_stats_time(tstats,current_thread_count,ktime_get());
}


static inline unsigned int get_typical_thread_count(thread_count_stats_t* tstats, ktime_t threshold)
{
	ktime_t acum=ktime_set(0,0);
	int i=tstats->nr_max_threads;

	/**
	 * Traverse all thread counts from the maximum until
	 * a substantially significant thread count is observed
	*/
	while (i>0) {
		if ((tstats->count_mask & (1ULL<<(i-1)))) {
			acum=ktime_add(tstats->stats[i],acum);
			if (ktime_compare(tstats->stats[i],threshold)>0 || ktime_compare(acum,threshold)>0)
				return i;
		}

		i--;

	}

	if (i<0)
		i=0;

	return i;
}




static inline unsigned int get_cpu_usage_thread_count_stats(thread_count_stats_t* tstats, ktime_t now)
{
	ktime_t delta=ktime_sub(now,tstats->last_cpu_usage_reset);

	if (delta==0)
		return 1;

	/* Roundup the result */
	else return (tstats->aggregate_cpu_usage+delta-1)/delta;
}



static inline unsigned int get_typical_thread_count_percentile(thread_count_stats_t* tstats, ktime_t now, ktime_t *threshold_vector, unsigned int* thread_count, unsigned int thres_count)
{
	ktime_t acum=ktime_set(0,0);
	int idx_thresh=0;
	int i;

	/* default initialization */
	for (i=0; i<thres_count; i++)
		thread_count[i]=0;

	/* Reset i for histogram analysis */
	i=tstats->nr_max_threads;
	/**
	 * Retrieve required percentiles: 95 and 90 by default
	*/
	while (i>0) {
		if ((tstats->count_mask & (1ULL<<(i-1)))) {
			acum=ktime_add(tstats->stats[i],acum);
			if (ktime_compare(tstats->stats[i],threshold_vector[idx_thresh])>0 || ktime_compare(acum,threshold_vector[idx_thresh])>0) {

				thread_count[idx_thresh]=i;

				idx_thresh++;

				if (idx_thresh==thres_count)
					break;

			}
		}
		i--;
	}

	return get_cpu_usage_thread_count_stats(tstats, now);
}

static inline unsigned int get_cpu_usage_thread_count_stats_debug(thread_count_stats_t* tstats, ktime_t now)
{
	ktime_t delta=ktime_sub(now,tstats->last_cpu_usage_reset);
	ktime_t acum=ktime_set(0,0);
	unsigned int estimate1;
	unsigned int estimate2;
	int i=tstats->nr_max_threads;


	if (delta==0)
		return 1;

	/* Roundup the result */
	estimate1=(tstats->aggregate_cpu_usage+delta-1)/delta;

	/**
	 * Traverse all thread counts from the maximum until
	 * a substantially significant thread count is observed
	*/
	while (i>0) {
		if ((tstats->count_mask & (1ULL<<(i-1)))) {
			acum=ktime_add(tstats->stats[i]*i,acum);
		}

		i--;

	}
	estimate2=(acum+delta-1)/delta;

	trace_printk("e1: %d || e2: %d\n",estimate1,estimate2);

	return estimate1;
}

#endif