/*
 *  cache_partitioning.h
 *
 *  OS-level resource-management framework based on
 *  HW extensions for cache-partitioning and memory-bandwidth limitation
 *
 *  Copyright (c) 2021 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef CACHE_PARTITIONING_PMC_H
#define CACHE_PARTITIONING_PMC_H

#include <pmc/cache_part.h>

/* Forward declaration of Opaque data type */
struct cluster_set;
typedef struct cluster_set cluster_set_t;

/* Allocate structures to play around with partitioning algorithms */
cluster_set_t* get_global_cluster_set(void);
cluster_set_t* get_group_specific_cluster_set(cluster_set_t* sets, int idx);
cluster_set_t* allocate_cluster_sets(int nr_sets);
void free_up_cluster_sets(cluster_set_t* sets);

/* Debugging function to trace_print_k resulting clustering solution */
void print_clustering_solution(cluster_set_t* clusters,int nr_apps);

/* Map specific clustering approach to actual HW partitions */
void enforce_cluster_partitioning(cache_part_set_t* part_set, cluster_set_t* clusters, unsigned char externally_managed_cos_ids);

/* LFOC and LFOC+ partitioning algorithms */
int lfoc_list(cluster_set_t* clusters, sized_list_t* apps, int nr_apps,  int nr_ways, int max_streaming, int use_pair_clustering, int max_nr_ways_streaming_part, int collide_streaming_parts);

typedef enum lfoc_mode {
	LFOC_LEGACY_MD,
	LFOC_PLUS_MD,
	LFOC_EVEN_SENSITIVE_MD,
	LFOC_NOPART_SENSITIVE_MD,
	LFOC_S_MD
} lfoc_mode_t;

int lfoc_gen(cluster_set_t* clusters, sized_list_t* apps, int nr_apps,  int nr_ways, int max_streaming, lfoc_mode_t mode, int max_nr_ways_streaming_part, int collide_streaming_parts, int use_full_cache_light);


/* Trivial partition set (all apps in a single partition) for debugging purposes  */

void trivial_part(cluster_set_t* clusters,sized_list_t* apps, int nr_apps,  int nr_ways);

/* Divide LLC into same-sized partitions (one for each app) */
void equal_part(cluster_set_t* clusters,sized_list_t* apps, int nr_apps,  int nr_ways);

void equal_part_intensive(cluster_set_t* clusters, sized_list_t* apps, int nr_apps,  int nr_ways);

#endif
