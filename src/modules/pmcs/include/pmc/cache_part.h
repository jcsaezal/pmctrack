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
	CACHE_CLASS_SENSITIVE=2
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
	atomic_t ref_counter;
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

#endif

