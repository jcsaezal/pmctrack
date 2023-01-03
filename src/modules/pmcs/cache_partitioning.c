/*
 *  cache_partitioning.c
 *
 *  OS-level resource-management framework based on
 *  HW extensions for cache-partitioning and memory-bandwidth limitation
 *
 *  Copyright (c) 2021 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */


#include <pmc/cache_partitioning.h>
#include <linux/vmalloc.h>

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define MAX_CLUSTERS 16 /* Based on the CLOS count of Debussy and Volta */
#define MAX_NR_WAYS 11
#define MAX_APPS 20
#define MAX_APP_CLUSTER 2
#define SAVE_PARTITIONED /* TODO remove define */


typedef struct cluster_info {
	int nr_ways;
	int nr_apps;
	sized_list_t apps;
	struct list_head link_cluster;
	int* curve; /* For max slowdown distance curve (point to right item of the big matrix) */
} cluster_info_t ;


/* Both matrix cell and "Cluster object" */
typedef struct candidate_cluster {
	int idx_cluster; /* Associated with min app */
	int nr_apps;
	short int buckets[MAX_NR_WAYS][MAX_APP_CLUSTER];
	short int per_app_slowdown[MAX_NR_WAYS][MAX_APP_CLUSTER];
#ifdef SAVE_PARTITIONED
	int partitioned_curve[MAX_NR_WAYS+1];  /* Partitioned curve */
#endif
	int max_slowdown[MAX_NR_WAYS+1];  /* Combined curve */
	int sum_slowdown[MAX_NR_WAYS+1];  /* For UCP */
	int distance;
	app_t* apps[MAX_APP_CLUSTER]; /* Apps that make up the cluster */
	struct list_head link[MAX_CLUSTERS]; /* One link for each clustering solution */
} candidate_cluster_t;


/* For pair_clustering (individual solutions explored) */
typedef struct clustering_solution {
	int nr_clusters;
	int per_app_cluster[MAX_APPS];
	int ways_per_cluster[MAX_APPS]; /* UCP way assignment  */
	int slowdown_per_app[MAX_APPS];	 /* To be calculated after UCP */
	/* as many entries as nr_clusters (point to matrix cell) */
	candidate_cluster_t* cluster_props[MAX_APPS];
	sized_list_t cluster_list;
	int unfairness;
} clustering_solution_t;


typedef struct distance1w_node {
	int app_i;
	int app_j;
	int distance; /* Slowdown cost */
	candidate_cluster_t* merged_cluster;
	struct list_head link;
} distance1w_node_t;


struct cluster_set {

	cluster_info_t pool[MAX_CLUSTERS];
	sized_list_t clusters;
	int nr_clusters;
	unsigned int default_cluster; /* Index of unkown applications */
	/*** Auxiliary global data for space savings ***/
	clustering_solution_t solutions[MAX_CLUSTERS]; /* As many solutions as the number of clusters we can create*/
	int distance_matrix[MAX_APPS][MAX_APPS];
	/* Full matrix for now
		* Items in the diagonal are considered as single-app clusters (optimization)
	*/
	candidate_cluster_t curves[MAX_APPS][MAX_APPS];
	distance1w_node_t dnode_pool[66]; /* 66=C_12,2 (Combinations) max platform */
	char buf[256];
};

/* ################################## */
/* Functions for cluster manipulation */
/* ################################## */


cluster_set_t* get_global_cluster_set(void)
{
	static cluster_set_t set;
	return &set;
}

cluster_set_t* get_group_specific_cluster_set(cluster_set_t* sets, int idx)
{
	return &sets[idx];
}

/* Allocate structures to play around with partitioning algorithms */
cluster_set_t* allocate_cluster_sets(int nr_sets)
{
	return vmalloc(sizeof(cluster_set_t)*nr_sets);
}

void free_up_cluster_sets(cluster_set_t* sets)
{
	if (sets)
		return vfree(sets);
}

static void init_cluster_set(cluster_set_t* set)
{
	int i=0;

	init_sized_list (&set->clusters,
	                 offsetof(cluster_info_t,
	                          link_cluster));

	set->nr_clusters=0;
	set->default_cluster=0;

	for (i=0; i< MAX_CLUSTERS; i++) {
		cluster_info_t * cluster=&set->pool[i];

		init_sized_list (&cluster->apps,
		                 offsetof(app_t,
		                          link_cluster));

		cluster->nr_ways=0;
		cluster->nr_apps=0;
	}

}

static cluster_info_t* allocate_new_cluster(cluster_set_t* set)
{
	cluster_info_t * cluster=&set->pool[set->nr_clusters];

	if (set->nr_clusters>=MAX_CLUSTERS)
		return NULL;

	insert_sized_list_tail(&set->clusters,cluster);
	set->nr_clusters++;
	return cluster;
}

static inline void add_app_to_cluster(cluster_info_t* cluster, app_t* app)
{
	insert_sized_list_tail(&cluster->apps,app);
	cluster->nr_apps++;
}


static inline cluster_info_t*  cluster_by_idx(cluster_set_t* set, int idx)
{
	cluster_info_t * cluster=&set->pool[idx];

	if (set->nr_clusters>=MAX_CLUSTERS)
		return NULL;

	return cluster;
}



/* #################### */
/* UCP-related routines */
/* #################### */

static inline int marginal_utility(int from_way, int to_way, int mpki_from, int mpki_to)
{
	return ((mpki_to - mpki_from)) / (from_way - to_way);
}


/* Returns a tuple (max_mu,mu_ways) */
void get_max_mu_gen(int* curve, int current_benchmark_ways, int available_ways, int max_nr_ways, int ret_val[])
{
	unsigned int current_benchmark_misses = curve[current_benchmark_ways];
	int max_mu = 0;
	int mu_ways = -1;
	int  best_metric_val=0;
	int exploration_ways = MIN(available_ways, max_nr_ways - current_benchmark_ways);
	int i=0;
	int nr_actual_entries=curve[0];
	int top_value=curve[nr_actual_entries];
	int new_misses,new_assigned_ways,mu;

	/* Special case: we store a partial version of the curve */

	for (i=1; i<=exploration_ways; i++) {
		new_assigned_ways = current_benchmark_ways + i;

		/* Control not beyond the limit */
		if (new_assigned_ways>nr_actual_entries)
			new_misses=top_value;
		else
			new_misses=curve[new_assigned_ways];

		mu = marginal_utility(current_benchmark_ways, new_assigned_ways, current_benchmark_misses, new_misses);

		if (mu > max_mu) {
			max_mu = mu;
			mu_ways = new_assigned_ways;
			best_metric_val=new_misses;
		}

	}
	ret_val[0]=max_mu;
	ret_val[1]=mu_ways;
	ret_val[2]=best_metric_val;
}

void lookahead_algorithm_list( sized_list_t* apps, int nr_apps,  int nr_ways, int* solution, int clustering)
{
	int available_ways = nr_ways;
	unsigned char there_is_maximal_marginal = 1;
	int i=0;
	int ret_val[3];
	void* cur_app;
	candidate_cluster_t* cand;

	/* Trivial case: one app */
	if (nr_apps==1) {
		solution[0]=nr_ways;
		return;
	}

	/* Deal with general case */

	/** Each app needs at least one 'slot' **/
	for (i=0; i<nr_apps; i++) {
		solution[i]=1;
		available_ways-=1;
	}


	while ( (available_ways > 0) && there_is_maximal_marginal) {

		int global_max_mu = 0;
		int global_mu_ways= -1;
		int selected_idx;

		/* Find maximal marginal utility across applications */
		for (i=0, cur_app=head_sized_list(apps); i<nr_apps; i++,cur_app=next_sized_list(apps,cur_app)) {
			int* curve;
			int max_mu;
			int  mu_ways;

			if (clustering) {
				cand=((candidate_cluster_t*)cur_app);
				/* Optimization: single-app's clusters must use app's curve field
				  as the max_slowdown curve is not really initialized
				  */
				if (cand->nr_apps==1)
					curve=cand->apps[0]->curve;
				else
					curve=cand->sum_slowdown;
			} else
				curve=((app_t*)cur_app)->curve;


			get_max_mu_gen(curve, solution[i], available_ways, nr_ways,ret_val);
			max_mu=ret_val[0];
			mu_ways=ret_val[1];

			if (max_mu > global_max_mu) {
				global_max_mu = max_mu;
				selected_idx = i;
				global_mu_ways = mu_ways;
			}
		}

		if (global_max_mu == 0) {
			there_is_maximal_marginal = 0;
		} else {
			/* Update partitions for selected benchmark and update available ways */
			available_ways -= global_mu_ways - solution[selected_idx];
			solution[selected_idx] = global_mu_ways;
		}

	}

	/* if there are available ways yet, allocate remaining ways fairly */
	if (available_ways > 0) {
		int nr_fair_ways_per_benchmark = available_ways / nr_apps;
		int remaining_ways = available_ways % nr_apps;
		i = 0;

		while ((i < nr_apps) && (available_ways > 0)) {
			int nr_extra_ways = nr_fair_ways_per_benchmark;

			if (remaining_ways > 0) {
				nr_extra_ways += 1;
				remaining_ways -= 1;
			}

			solution[i] += nr_extra_ways;
			available_ways -= nr_extra_ways;

			i += 1;
		}
	}
}


/** but using an array of curves as a parameter **/
void lookahead_algorithm(app_t** apps, int nr_apps,  int nr_ways, int* solution)
{
	int available_ways = nr_ways;
	unsigned char there_is_maximal_marginal = 1;
	int i=0;
	int ret_val[3];

	/** Each app needs at least one 'slot' **/
	for (i=0; i<nr_apps; i++) {
		solution[i]=1;
		available_ways-=1;
	}


	while ( (available_ways > 0) && there_is_maximal_marginal) {

		int global_max_mu = 0;
		int global_mu_ways= -1;
		int selected_idx;

		/* Find maximal marginal utility across applications */

		for (i=0; i<nr_apps; i++) {
			int* curve=apps[i]->curve;
			int max_mu;
			int  mu_ways;

			get_max_mu_gen(curve, solution[i], available_ways, nr_ways,ret_val);
			max_mu=ret_val[0];
			mu_ways=ret_val[1];

			if (max_mu > global_max_mu) {
				global_max_mu = max_mu;
				selected_idx = i;
				global_mu_ways = mu_ways;
			}
		}

		if (global_max_mu == 0) {
			there_is_maximal_marginal = 0;
		} else {
			/* Update partitions for selected benchmark and update available ways */
			available_ways -= global_mu_ways - solution[selected_idx];
			solution[selected_idx] = global_mu_ways;
		}

	}

	/* if there are available ways yet, allocate remaining ways fairly */
	if (available_ways > 0) {
		int nr_fair_ways_per_benchmark = available_ways / nr_apps;
		int remaining_ways = available_ways % nr_apps;
		i = 0;

		while ((i < nr_apps) && (available_ways > 0)) {
			int nr_extra_ways = nr_fair_ways_per_benchmark;

			if (remaining_ways > 0) {
				nr_extra_ways += 1;
				remaining_ways -= 1;
			}

			solution[i] += nr_extra_ways;
			available_ways -= nr_extra_ways;

			i += 1;
		}
	}
}

void cost_way_stealing(app_t** workload,
                       int nr_apps,
                       int idx_app,
                       unsigned long unmerged,
                       int* ways_assigned,
                       int* min_cost,
                       int* min_idx)
{

	/* static */ int slowdown_red[MAX_APPS];
	app_t *app=workload[idx_app];
	app_t *that_app;
	int app_assigned_ways=ways_assigned[idx_app];
	int those_ways;
	int* my_slowdown_curve=app->curve;
	int* slowdown_curve;
	int cur_slowdown=my_slowdown_curve[app_assigned_ways];
	int i=0;
	int min_val=INT_MAX;
	int min_index=-1;


	for (i=0; i<nr_apps; i++) {
		that_app=workload[i];
		those_ways=ways_assigned[i];
		slowdown_curve=that_app->curve;

		if (!(unmerged & 1<<i)
		    || i==idx_app
		    || those_ways==1
		    || slowdown_curve[those_ways-1]> cur_slowdown) {
			slowdown_red[i]=INT_MAX;
		} else {
			slowdown_red[i]=my_slowdown_curve[app_assigned_ways+1]-my_slowdown_curve[app_assigned_ways]-(slowdown_curve[those_ways]-slowdown_curve[those_ways-1]);

			if (slowdown_red[i]<min_val) {
				min_val=slowdown_red[i];
				min_index=i;
			}

		}
	}


	/* Return actual values */
	(*min_cost)=min_val;
	(*min_idx)=min_index;

	//return slowdown_red;
}



void ucp_unfairness(app_t** apps, int nr_apps,  int nr_ways, int* solution)
{

	int slowdown_vector[MAX_APPS];
	int i=0,j;
	unsigned long unmerged=(1<<nr_apps)-1;
	int max_idx,min_idx,min_cost,max_slowdown;

	lookahead_algorithm(apps,nr_apps,nr_ways,solution);

	/* Optimization phase to improve fairness */

	/* Calculate slowdown vector */
	for (i=0; i<nr_apps; i++)
		slowdown_vector[i]=apps[i]->curve[solution[i]];


	for (i=0; i<nr_apps; i++) {
		max_idx=-1;
		max_slowdown=-INT_MAX;

		/* Determine app with max slowdown */
		for (j=0; j<nr_apps; j++) {
			if (slowdown_vector[j]>max_slowdown) {
				max_idx=j;
				max_slowdown=slowdown_vector[j];
			}
		}

		if (!(unmerged & 1<<max_idx))
			break;

		cost_way_stealing(apps,nr_apps,max_idx,unmerged,solution,&min_cost,&min_idx);

		if (min_cost>70)
			break;

		/* Transfer one way */
		solution[max_idx]+=1;
		solution[min_idx]-=1;

		unmerged&=~(1<<max_idx);


		slowdown_vector[max_idx]=apps[max_idx]->curve[solution[max_idx]];
		slowdown_vector[min_idx]=apps[min_idx]->curve[solution[min_idx]];

	}
}


/* ############################# */
/* Functions for pair-clustering */
/* ############################# */

/* Apply interpolation when acces */
static inline int access_index(int* curve, int nr_ways, int space, int scale_factor )
{
	int r = space % scale_factor;
	int q= space / scale_factor;

	if (q>=nr_ways)
		return curve[q-1];
	else if (r==0 || q+1 > nr_ways)
		return curve[q];
	else
		return ((scale_factor-r)*curve[q] + r*curve[q+1]) / scale_factor;
}

/* Apply interpolation when acces */
static inline int access_index_acc(int* curve, int nr_ways, int space, int scale_factor )
{
	int r = space % scale_factor;
	int q= space / scale_factor;
	int value,delta;

	/* Get max (saturate) */
	if (q>=nr_ways)
		return curve[nr_ways-1];
	else if (r==0 && q >= 1)
		return curve[q-1];
	else if (q==0) {

		value=((scale_factor-r)*curve[0] + r*curve[1]) / scale_factor;
		delta=value-curve[1];
		/* Override value */
		value=curve[0]+delta;

		if (value < 0)
			value=1; /* To avoid divide by zero */
		return value;
	} else
		return ((scale_factor-r)*curve[q-1] + r*curve[q]) / scale_factor;
}



/* whirlpoolc_combine_ncurves_f */
void build_combined_curve(struct candidate_cluster* info, int nr_ways)
{
	int s0,s1,m0,m1,slow0,slow1,i,ms;
	int* space_curves[MAX_APP_CLUSTER];
	int* curves[MAX_APP_CLUSTER];
	const int scale_factor=100;

	space_curves[0]=info->apps[0]->space_curve+1; /* Point to item1 (first one is the size) */
	space_curves[1]=info->apps[1]->space_curve+1;
	curves[0]=info->apps[0]->curve+1;
	curves[1]=info->apps[1]->curve+1;
	info->max_slowdown[0]=info->sum_slowdown[0]=nr_ways; /* First item is the size (in preparation for UCP) */

	s0=s1=0;


	for (i=0; i<nr_ways; i++) {
		m0=access_index(space_curves[0],nr_ways,s0,scale_factor);
		m1=access_index(space_curves[1],nr_ways,s1,scale_factor);
		ms=m0+m1;

		s0+=(m0*scale_factor+(ms-1))/ms;
		s1+=(m1*scale_factor+(ms-1))/ms;

		/* Determine scaled slowdown values */
		slow0=access_index_acc(curves[0],nr_ways,s0,scale_factor);
		slow1=access_index_acc(curves[1],nr_ways,s1,scale_factor);

		/* Add to combined curve and per-app slowdown curves */
		info->per_app_slowdown[i][0]=slow0;
		info->per_app_slowdown[i][1]=slow1;
		info->max_slowdown[i+1]=MAX(slow0,slow1);
		info->sum_slowdown[i+1]=slow0+slow1;

		info->buckets[i][0]=s0;
		info->buckets[i][1]=s1;

	}

}

/*
 - List of benchmark_properties ....
 -
*/

int calculate_slowdown_distance(candidate_cluster_t* info, int nr_ways)
{
	int* combined_curve=info->max_slowdown +1; /* Skip size*/
#ifndef SAVE_PARTITIONED
	int partitioned_curve[MAX_NR_WAYS+1]= {11};
#endif
	int i=0,slow1,slow0;
	int ucp_solution[MAX_APP_CLUSTER];
	int distance=0;
	int value=0;

#ifdef SAVE_PARTITIONED
	info->partitioned_curve[0]=nr_ways;
#endif
	build_combined_curve(info, nr_ways);

	distance=0;
	/* Calculate partitioned curve and calculate distance with same processing */
	for (i=1; i<=nr_ways; i++) {
		/* Watch out special case for i=i (lookahead gives 1 to each app) */
		if (i>1) {
			lookahead_algorithm(info->apps,2,i,ucp_solution);
			slow0=info->apps[0]->curve[ucp_solution[0]];
			slow1=info->apps[1]->curve[ucp_solution[1]];
			value =MAX(slow0,slow1);
		} else {
			value=combined_curve[i-1];
		}

#ifdef SAVE_PARTITIONED
		info->partitioned_curve[i]=value;
#else
		partitioned_curve[i]=value;
#endif

		/* Distance calculation */
		distance += combined_curve[i-1] -
#ifdef SAVE_PARTITIONED
		            info->partitioned_curve[i];
#else
		            partitioned_curve[i];
#endif
	}

	return distance;
}


static inline void clear_distance_in_matrix(int matrix[MAX_APPS][MAX_APPS], int index, int nr_apps )
{
	int i=0;

	/* Clear rows and columns */
	for (i=0; i<nr_apps; i++) {
		matrix[index][i]=INT_MAX;
		matrix[i][index]=INT_MAX;
	}
}


/* Duplicate solution by traversing the cluster list and adding only what's necessary */
void build_new_solution(clustering_solution_t* prev_solution,candidate_cluster_t* merged, int min_i, int min_j, clustering_solution_t* new_solution)
{

	int i=0;
	candidate_cluster_t* cur_cluster;

	/* Insert merged upfront */
	insert_sized_list_tail(&new_solution->cluster_list,merged);
	new_solution->nr_clusters=1;

	for (i=0,cur_cluster=head_sized_list(&prev_solution->cluster_list);
	     cur_cluster!=NULL;
	     cur_cluster=next_sized_list(&prev_solution->cluster_list,cur_cluster),i++) {
		int idx_cluster=cur_cluster->idx_cluster;
		if ((idx_cluster==min_i) || (idx_cluster==min_j))
			continue;

		insert_sized_list_tail(&new_solution->cluster_list,cur_cluster);
		new_solution->nr_clusters++;
	}

}


/**/
void cluster_ucp(clustering_solution_t* solution, int nr_ways)
{

	candidate_cluster_t* cur_cluster;
	sized_list_t* cluster_list=&solution->cluster_list;
	int i,j;
	int max_slowdown=0;
	int min_slowdown=INT_MAX;
	int this_slowdown=0;

	/* Problem (how to invoke UCP) in this context -> we do not have a list of clusters directly, do we? now we do! (cluster_list )*/

	/* Invoke UCP on cluster list */
	lookahead_algorithm_list(cluster_list, solution->nr_clusters,nr_ways, solution->ways_per_cluster,1);

	/* Calculate unfairness and per-app slowdowns ... */

	for (i=0,cur_cluster=head_sized_list(cluster_list);
	     cur_cluster!=NULL;
	     cur_cluster=next_sized_list(cluster_list,cur_cluster),i++) {
		for (j=0; j<cur_cluster->nr_apps; j++) {
			app_t* app=cur_cluster->apps[j];

			/*
			 *  For single-app clusters the per_app_slowdown
			 * 	field is not initialized so grab the slowdown
			 * 	directly from the application's curve
			 */
			if (cur_cluster->nr_apps==1)
				this_slowdown=app->curve[solution->ways_per_cluster[i]];
			else
				this_slowdown=cur_cluster->per_app_slowdown[solution->ways_per_cluster[i]-1][j];

			solution->slowdown_per_app[app->sensitive_id]=this_slowdown;
			/* Take advantage of the processing to update this */
			solution->per_app_cluster[app->sensitive_id]=cur_cluster->idx_cluster;

			if (this_slowdown>max_slowdown)
				max_slowdown=this_slowdown;

			if (this_slowdown<min_slowdown)
				min_slowdown=this_slowdown;
		}
	}

	if (min_slowdown==0)
		solution->unfairness=INT_MAX;
	else
		solution->unfairness=(1000*max_slowdown)/min_slowdown;
}


void get_sorted_slowdowns(clustering_solution_t* cur_solution, int* index_vector)
{
	//static int  index_vector[MAX_APPS];
	unsigned long sel_bitmask=0; /* Just enabled for 64 bits */
	int i,j;
	int max_slowdown=0;
	int idx_max=0;
	int nr_apps=cur_solution->nr_clusters; /* Just valid for this case */
	int this_slowdown;

	/* Build index vector: sorting by insertion */

	for (i=0; i<nr_apps; i++) {
		max_slowdown=-1;

		for (j=0; j< nr_apps; j++) {

			/* This was already selected for maximum -> skip */
			if ((1<<j) & sel_bitmask)
				continue;

			this_slowdown=cur_solution->slowdown_per_app[j];

			if (this_slowdown>max_slowdown) {
				max_slowdown=this_slowdown;
				idx_max=j;
			}
		}
		sel_bitmask|=(1<<idx_max);
		index_vector[i]=idx_max; /* point to app */
	}

	//return index_vector;
}


clustering_solution_t*  pair_clustering_core(sized_list_t* apps, int nr_apps, int nr_ways, cluster_set_t* clusters)
{
	cluster_set_t* cg=clusters; /* Create alias pointer for simplicity */
	/* List of apps is crucial */
	/*static */  app_t* app_vector[MAX_APPS];
	app_t* cur_app;
	candidate_cluster_t* cur_cluster;
	int i,j;
	int idx_i,idx_j;
	int nr_solutions=0;
	int it=0;
	clustering_solution_t *cur_solution,*new_solution,*best_solution;
	unsigned long unmerged;
	int nr_clusters;
	//int* sorted_indexes;
	int best_unfairness;
	int  sorted_indexes[MAX_APPS];

	/* Initialize distance matrix to infinite */
	for (i=0; i<nr_apps; i++) {
		cg->distance_matrix[i][i]=INT_MAX;
		for (j=i+1; j<nr_apps; j++)
			cg->distance_matrix[i][j]=cg->distance_matrix[j][i]=INT_MAX;
	}

	/* Initial clustering solution */
	/* Point to solution "object" */
	cur_solution=&cg->solutions[nr_solutions];
	init_sized_list(&cur_solution->cluster_list, offsetof(candidate_cluster_t,link[nr_solutions]));
	nr_solutions++;

	/* Set up clusters to 0 (bug ...) */
	cur_solution->nr_clusters=0;

	/* Initialize per app slowdown vector with zeroes */
	memset(cur_solution->slowdown_per_app,0,sizeof(cur_solution->slowdown_per_app));

	for (i=0, cur_app=head_sized_list(apps); i<nr_apps; i++,cur_app=next_sized_list(apps,cur_app)) {
		cur_cluster=&cg->curves[i][i];	 /* Memory diagonal line of the big matrix ! */
		/* initialize app vector */
		app_vector[i]=cur_app;

		/* Assign id */
		cur_app->sensitive_id=i;

		/* Indicate the cluster this app belongs to */
		cur_solution->per_app_cluster[i]=i;
		/*
		   Using diagonal line of the big matrix !
			- For single-app clusters we do not have to initialize anything really (stale fields except for single_app)
		*/
		cur_solution->cluster_props[i]=cur_cluster;
		/*
		 Special case for single app (to point indirectly to slowdown table
		  already so that UCP can use directly )
		  */
		cur_cluster->nr_apps=1;
		cur_cluster->apps[0]=cur_app;
		cur_cluster->apps[1]=NULL;
		cur_cluster->idx_cluster=i;

		/* Add cluster to current solution  */
		insert_sized_list_tail(&cur_solution->cluster_list,cur_cluster);
		cur_solution->nr_clusters++;
	}

	/* Apply UCP to complete the other solution fields */
	cluster_ucp(cur_solution, nr_ways);

	/*
	 * Calculate initial distances and curves
	 * In this case distance matrix is complete for efficiency reasons
	 * Don't forget to update it right away
	 */

	/* Traverse app vector */
	for (i=0; i<nr_apps; i++) {
		/* Cluster ID */
		app_t* app0=app_vector[i];
		idx_i=app0->sensitive_id;

		for (j=i+1; j<nr_apps; j++) {
			app_t* app1=app_vector[j];
			int distance;
			cur_cluster=&cg->curves[i][j];
			idx_j=app1->sensitive_id;

			/* Initialize cluster*/
			cur_cluster->apps[0]=app0;
			cur_cluster->apps[1]=app1;
			cur_cluster->nr_apps=2;
			/* The ID of the cluster must be initialized (min value) */
			cur_cluster->idx_cluster=idx_i;

			/* Calculate distance and store in matrix */
			/* candidate_cluster_t (cell curve matrix) stores it all ! */
			distance=calculate_slowdown_distance(cur_cluster,nr_ways);
			cg->distance_matrix[idx_i][idx_j]=distance;
			cg->distance_matrix[idx_j][idx_i]=distance;
		}
	}

	/* Update best so far */
	best_solution=cur_solution;
	best_unfairness=cur_solution->unfairness;

	it=0;

	/* How many  clusters remain to be explored */
	nr_clusters=nr_apps;
	/* Initialize merged bit vector (all false) */
	unmerged=(1<<nr_apps)-1;


	/* Get sorted indexes in descending order by slowdown */
	get_sorted_slowdowns(cur_solution,sorted_indexes);


	for (i=0; i<nr_apps && unmerged ; i++) {
		int idxc=sorted_indexes[i];
		int idx_min, min_distance;
		int  min_i,min_j;

		if (!(unmerged & (1<<idxc)))
			continue;

		idx_min=0;
		min_distance=INT_MAX;

		/* Step 1: find closest cluster in terms of distance and do not merge if distance >=0 */
		for (j=0; j<nr_apps; j++) {
			int this_distance=cg->distance_matrix[idxc][j];

			if (this_distance<min_distance) {
				min_distance=this_distance;
				idx_min=j;
			}

		}

		if (min_distance>=0) {
			/* If merging is not possible (make sure this app will not be considered for merging) */
			unmerged&=~(1<<idxc);
			clear_distance_in_matrix(cg->distance_matrix,idxc,nr_apps);
			continue;
		}

		/* Step 2: See if merging the clusters contributes to reducing the maximum slowdown */
		/* Normalize indexes so that min_i < min_j*/

		if (idx_min > idxc) {
			min_i=idxc;
			min_j=idx_min;
		} else {
			min_i=idx_min;
			min_j=idxc;
		}

		/* This is the combined cluster already completed (pre merged) */
		cur_cluster=&cg->curves[min_i][min_j];

		/* Indicate they are merged */
		unmerged&=~(1<<min_i);
		unmerged&=~(1<<min_j);

		/* Clear items from distance matrix (all infinite) */
		clear_distance_in_matrix(cg->distance_matrix,min_i,nr_apps);
		clear_distance_in_matrix(cg->distance_matrix,min_j,nr_apps);


		/* Build new solution */
		new_solution=&cg->solutions[nr_solutions];
		init_sized_list(&new_solution->cluster_list, offsetof(candidate_cluster_t,link[nr_solutions]));
		nr_solutions++;

		/* Initialize structure */
		build_new_solution(cur_solution,cur_cluster,min_i,min_j,new_solution);
		/* Apply UCP clustering */
		cluster_ucp(new_solution, nr_ways);
		/* Update pointer */
		cur_solution=new_solution;

		/* Update best so far */
		if  (new_solution->unfairness<=best_unfairness) {
			best_solution=new_solution;
			best_unfairness=new_solution->unfairness;
		}

	} /* End pair clustering loop */


	/* Build cluster set appropriately to interact with LFOC */
	if (clusters) {
		sized_list_t* cluster_list=&best_solution->cluster_list;
		candidate_cluster_t* cand;
		cluster_info_t*  cur;

		for (i=0,cand=head_sized_list(cluster_list);
		     cand!=NULL;
		     cand=next_sized_list(cluster_list,cand),i++) {

			cur=allocate_new_cluster(clusters);
			cur->nr_ways=best_solution->ways_per_cluster[i];
			cur->nr_apps=cand->nr_apps;

			for (j=0; j<cand->nr_apps; j++)
				add_app_to_cluster(cur,cand->apps[j]);

		}
	}

	return best_solution;
}

int determine_slowdown_reductions(app_t** workload,
                                  int nr_apps,
                                  int max_ways,
                                  int idx_app,
                                  unsigned long unmerged,
                                  candidate_cluster_t curves[MAX_APPS][MAX_APPS],
                                  int* ways_assigned,
                                  int* slowdown_red)
{
	int app_assigned_ways=ways_assigned[idx_app];
	int i=0;
	int min_i, min_j;
	int total_ways=0;
	int slowdown_combined, slowdown_partitioned;
	candidate_cluster_t* cluster;
	app_t* app=workload[i];
	app_t *app0,*app1;

	for (i=0; i<nr_apps; i++) {
		app=workload[i];

		/* Check for null pointers */
		if (!app || !app->curve) {
			trace_printk("app: %p | curve: %p | i: %d | nr_apps: %d\n",
			             app, (app==NULL? NULL: app->curve),
			             i, nr_apps);
			return -EINVAL;
		}


		if (unmerged & 1<<i) {

			if (i==idx_app) {
				if (app_assigned_ways==max_ways)
					slowdown_red[i]=INT_MAX;
				else
					slowdown_red[i]=app->curve[app_assigned_ways+1]-app->curve[app_assigned_ways];

			} else {

				if (idx_app>i) {
					min_i=i;
					min_j=idx_app;
				} else {
					min_i=idx_app;
					min_j=i;
				}
				cluster=&curves[min_i][min_j];

				total_ways=ways_assigned[min_i]+ways_assigned[min_j];
				app0=workload[min_i];
				app1=workload[min_j];
				/* Sum slowdown */
				slowdown_combined=cluster->sum_slowdown[total_ways];
				slowdown_partitioned=app0->curve[ways_assigned[min_i]]+app1->curve[ways_assigned[min_j]];
				slowdown_red[i]=slowdown_combined-slowdown_partitioned;

			}

		} else
			slowdown_red[i]=INT_MAX;

	}
	return 0;
}

distance1w_node_t* get_min_distance_in_list(sized_list_t* distance_list, int filter_idx)
{

	distance1w_node_t* cur;
	distance1w_node_t* best=NULL;
	int min_distance=INT_MAX;

	for (cur=head_sized_list(distance_list); cur!=NULL; cur=next_sized_list(distance_list,cur)) {

		if ( (cur->app_i !=filter_idx) && (cur->app_j !=filter_idx) ) {


			if (!best || cur->distance<min_distance) {
				min_distance=cur->distance;
				best=cur;
			}

		}

	}

	return best;
}


void delete_distances(sized_list_t* distance_list, int filter_idx)
{

	distance1w_node_t* cur;
	distance1w_node_t* next;

	cur=head_sized_list(distance_list);

	while(cur!=NULL) {
		next=next_sized_list(distance_list,cur);

		if ((cur->app_i ==filter_idx) || (cur->app_j ==filter_idx))
			remove_sized_list(distance_list,cur);

		cur=next;
	}

}



/* Duplicate solution by traversing the cluster list and adding only what's necessary */
void build_new_solution_merged(clustering_solution_t* prev_solution,
                               candidate_cluster_t* merged,
                               int min_i, int min_j,
                               int* ways_assigned,
                               clustering_solution_t* new_solution)
{
	candidate_cluster_t* cur_cluster;
	sized_list_t* cluster_list;
	int i,j;
	int max_slowdown=0;
	int min_slowdown=INT_MAX;
	int this_slowdown=0;


	/* Insert merged upfront */
	insert_sized_list_tail(&new_solution->cluster_list,merged);
	new_solution->nr_clusters=1;
	/* Watch out, ways_per_cluster must appear in the order of the list */
	new_solution->ways_per_cluster[0]=ways_assigned[min_i];

	for (i=0,cur_cluster=head_sized_list(&prev_solution->cluster_list);
	     cur_cluster!=NULL;
	     cur_cluster=next_sized_list(&prev_solution->cluster_list,cur_cluster),i++) {
		int idx_cluster=cur_cluster->idx_cluster;
		if ((idx_cluster==min_i) || (idx_cluster==min_j))
			continue;

		insert_sized_list_tail(&new_solution->cluster_list,cur_cluster);

		new_solution->ways_per_cluster[new_solution->nr_clusters]=ways_assigned[idx_cluster];

		new_solution->nr_clusters++;
	}

	/* Traverse now new solution and update necessary fields */
	cluster_list=&new_solution->cluster_list;


	/* Calculate unfairness and per-app slowdowns ... */
	for (i=0,cur_cluster=head_sized_list(cluster_list);
	     cur_cluster!=NULL;
	     cur_cluster=next_sized_list(cluster_list,cur_cluster),i++) {
		for (j=0; j<cur_cluster->nr_apps; j++) {
			app_t* app=cur_cluster->apps[j];

			/*
			 *  For single-app clusters the per_app_slowdown
			 * 	field is not initialized so grab the slowdown
			 * 	directly from the application's curve
			 */
			if (cur_cluster->nr_apps==1)
				this_slowdown=app->curve[new_solution->ways_per_cluster[i]];
			else
				this_slowdown=cur_cluster->per_app_slowdown[new_solution->ways_per_cluster[i]-1][j];

			new_solution->slowdown_per_app[app->sensitive_id]=this_slowdown;
			/* Take advantage of the processing to update this */
			new_solution->per_app_cluster[app->sensitive_id]=cur_cluster->idx_cluster;

			if (this_slowdown>max_slowdown)
				max_slowdown=this_slowdown;

			if (this_slowdown<min_slowdown)
				min_slowdown=this_slowdown;
		}
	}

	if (min_slowdown==0)
		new_solution->unfairness=INT_MAX;
	else
		new_solution->unfairness=(1000*max_slowdown)/min_slowdown;
}

/* Duplicate solution by traversing the cluster list and adding only what's necessary */
void evaluate_new_solution(clustering_solution_t* new_solution,
                           int* ways_assigned)
{
	candidate_cluster_t* cur_cluster;
	sized_list_t* cluster_list;
	int i,j;
	int max_slowdown=0;
	int min_slowdown=INT_MAX;
	int this_slowdown=0;


	cluster_list=&new_solution->cluster_list;

	/* Calculate unfairness and per-app slowdowns ... */
	for (i=0,cur_cluster=head_sized_list(cluster_list);
	     cur_cluster!=NULL;
	     cur_cluster=next_sized_list(cluster_list,cur_cluster),i++) {
		int idx_cluster=cur_cluster->idx_cluster;

		/* Just update number of ways */
		new_solution->ways_per_cluster[i]=ways_assigned[idx_cluster];

		for (j=0; j<cur_cluster->nr_apps; j++) {
			app_t* app=cur_cluster->apps[j];

			/*
			 *  For single-app clusters the per_app_slowdown
			 * 	field is not initialized so grab the slowdown
			 * 	directly from the application's curve
			 */
			if (cur_cluster->nr_apps==1)
				this_slowdown=app->curve[new_solution->ways_per_cluster[i]];
			else
				this_slowdown=cur_cluster->per_app_slowdown[new_solution->ways_per_cluster[i]-1][j];

			new_solution->slowdown_per_app[app->sensitive_id]=this_slowdown;
			/* Take advantage of the processing to update this */
			new_solution->per_app_cluster[app->sensitive_id]=cur_cluster->idx_cluster;

			if (this_slowdown>max_slowdown)
				max_slowdown=this_slowdown;

			if (this_slowdown<min_slowdown)
				min_slowdown=this_slowdown;
		}
	}

	if (min_slowdown==0)
		new_solution->unfairness=INT_MAX;
	else
		new_solution->unfairness=(1000*max_slowdown)/min_slowdown;
}


clustering_solution_t*  pair_clustering_core2(sized_list_t* apps, int nr_apps, int nr_ways, cluster_set_t* clusters)
{
	cluster_set_t* cg=clusters; /* Create alias pointer for simplicity */
	sized_list_t distance_list; /* List of distances */
	int node_pool_index=0;
	/* List of apps is crucial */
	app_t* app_vector[MAX_APPS];
	app_t* cur_app;
	candidate_cluster_t* cur_cluster;
	int i,j;
	int idx_i,idx_j;
	int nr_solutions=0;
	int it=0;
	clustering_solution_t *cur_solution,*new_solution,*best_solution;
	unsigned long unmerged;
	int nr_clusters;
	//int* sorted_indexes;
	int best_unfairness;
	int ways_assigned[MAX_APPS];
	int  sorted_indexes[MAX_APPS];
	int slowdown_reductions[MAX_APPS];
	int error=0;

	init_sized_list(&distance_list,offsetof(distance1w_node_t,link));

	/* Initial clustering solution */
	/* Point to solution "object" */
	cur_solution=&cg->solutions[nr_solutions];
	init_sized_list(&cur_solution->cluster_list, offsetof(candidate_cluster_t,link[nr_solutions]));
	nr_solutions++;

	/* Set up clusters to 0 (bug ...) */
	cur_solution->nr_clusters=0;

	/* Initialize per app slowdown vector with zeroes */
	memset(cur_solution->slowdown_per_app,0,sizeof(cur_solution->slowdown_per_app));

	for (i=0, cur_app=head_sized_list(apps); i<nr_apps; i++,cur_app=next_sized_list(apps,cur_app)) {
		cur_cluster=&cg->curves[i][i];	 /* Memory diagonal line of the big matrix ! */
		/* initialize app vector */
		app_vector[i]=cur_app;

		/* Assign id */
		cur_app->sensitive_id=i;

		/* Indicate the cluster this app belongs to */
		cur_solution->per_app_cluster[i]=i;
		/*
		   Using diagonal line of the big matrix !
			- For single-app clusters we do not have to initialize anything really (stale fields except for single_app)
		*/
		cur_solution->cluster_props[i]=cur_cluster;
		/*
		 Special case for single app (to point indirectly to slowdown table
		  already so that UCP can use directly )
		  */
		cur_cluster->nr_apps=1;
		cur_cluster->apps[0]=cur_app;
		cur_cluster->apps[1]=NULL;
		cur_cluster->idx_cluster=i;

		/* Add cluster to current solution  */
		insert_sized_list_tail(&cur_solution->cluster_list,cur_cluster);
		cur_solution->nr_clusters++;
	}

#ifdef USE_CLUSTER_UCP
	/* Apply UCP to complete the other solution fields */
	cluster_ucp(cur_solution, nr_ways);

	/* Copy ways for the new algorithm */
	for(i=0; i<nr_apps; i++)
		ways_assigned[i]=cur_solution->ways_per_cluster[i];

#else
	ucp_unfairness(app_vector,nr_apps,nr_ways,ways_assigned);
	evaluate_new_solution(cur_solution,ways_assigned);
#endif

	/*
	 * Calculate initial clusters
	 * and list of 1-way distances
	 */


	node_pool_index=0;

	/* Traverse app vector */
	for (i=0; i<nr_apps; i++) {
		/* Cluster ID */
		app_t* app0=app_vector[i];
		idx_i=app0->sensitive_id;

		for (j=i+1; j<nr_apps; j++) {
			app_t* app1=app_vector[j];
			cur_cluster=&cg->curves[i][j];
			idx_j=app1->sensitive_id;

			/* Initialize cluster*/
			cur_cluster->apps[0]=app0;
			cur_cluster->apps[1]=app1;
			cur_cluster->nr_apps=2;
			/* The ID of the cluster must be initialized (min value) */
			cur_cluster->idx_cluster=idx_i;

			/* Initialize remaining info cluster */
			build_combined_curve(cur_cluster,nr_ways);

			/* One way for each app in the inital UCP solution */
			if ((ways_assigned[i]+ways_assigned[j])==2) {
				int slowdown_partitioned=app0->curve[1]+app1->curve[1];
				int slowdown_combined=cur_cluster->sum_slowdown[1];
				distance1w_node_t* dnode=&cg->dnode_pool[node_pool_index++];

				dnode->app_i=i;
				dnode->app_j=j;
				dnode->distance=slowdown_combined-slowdown_partitioned;
				dnode->merged_cluster=cur_cluster;

				insert_sized_list_tail(&distance_list,dnode);
			}
		}
	}


	/* Update best so far */
	best_solution=cur_solution;
	best_unfairness=cur_solution->unfairness;

	it=0;

	/* How many  clusters remain to be explored */
	nr_clusters=nr_apps;
	/* Initialize merged bit vector (all false) */
	unmerged=(1<<nr_apps)-1;


	/* Get sorted indexes in descending order by slowdown */
	get_sorted_slowdowns(cur_solution,sorted_indexes);


	for (i=0; i<nr_apps && unmerged ; i++) {
		int idxc=sorted_indexes[i];
		int idx_min, min_reduction;
		int  min_i,min_j;
		distance1w_node_t* min_distance_node;

		if (!(unmerged & (1<<idxc)))
			continue;

		/* No new solution for now */
		new_solution=NULL;


		error=determine_slowdown_reductions(app_vector,nr_apps,nr_ways,idxc,unmerged,cg->curves,ways_assigned,slowdown_reductions);

		if (error)
			return NULL;

		min_distance_node=get_min_distance_in_list(&distance_list,idxc);

		if (min_distance_node)
			slowdown_reductions[idxc]+=min_distance_node->distance;
		else
			slowdown_reductions[idxc]=INT_MAX;

		idx_min=-1;
		min_reduction=INT_MAX;

		/* Step 1: find closest cluster in terms of distance and do not merge if distance >=0 */
		for (j=0; j<nr_apps; j++) {
			int this_reduction=slowdown_reductions[j];

			if (this_reduction<min_reduction) {
				min_reduction=this_reduction;
				idx_min=j;
			}

		}

		/* It is best to steal a way */
		if (idx_min==idxc && min_reduction<0) {
			/* Let's steal a way -> Merge 2 1-way clusters */
			min_i=min_distance_node->app_i;
			min_j=min_distance_node->app_j;

			unmerged&=~(1<<min_i);
			unmerged&=~(1<<min_j);

			delete_distances(&distance_list,min_i);
			delete_distances(&distance_list,min_j);

			/* This is the combined cluster already completed (pre merged) */
			cur_cluster=&cg->curves[min_i][min_j];
			/* Not really necessary ... */
			ways_assigned[min_i]=ways_assigned[min_j]=1;
			ways_assigned[idxc]=ways_assigned[idxc]+1; /* Move the way */

			/* Build new solution */
			new_solution=&cg->solutions[nr_solutions];
			init_sized_list(&new_solution->cluster_list, offsetof(candidate_cluster_t,link[nr_solutions]));
			nr_solutions++;

			/* Initialize structure */
			build_new_solution_merged(cur_solution,cur_cluster,min_i,min_j,ways_assigned,new_solution);


			/* Update best and curr */
			cur_solution=new_solution;

			/* Update best so far */
			if  (new_solution->unfairness<best_unfairness) {
				best_solution=new_solution;
				best_unfairness=new_solution->unfairness;
			}

			/*** UPDATE FOR A POTENTIAL REMERGE ****/
			slowdown_reductions[min_i]=INT_MAX;
			slowdown_reductions[min_j]=INT_MAX;

			/* Recalculate */
			idx_min=-1;
			min_reduction=INT_MAX;

			/* Step 1: find closest cluster in terms of distance and do not merge if distance >=0 */
			for (j=0; j<nr_apps; j++) {
				int this_reduction=slowdown_reductions[j];

				if (this_reduction<min_reduction) {
					min_reduction=this_reduction;
					idx_min=j;
				}

			}
		}


		/* For now, we disable double merges.. but I feel the code would still  work */
		if (!new_solution && idx_min!=idxc && min_reduction<0) {

			if (idx_min > idxc) {
				min_i=idxc;
				min_j=idx_min;
			} else {
				min_i=idx_min;
				min_j=idxc;
			}

			unmerged&=~(1<<min_i);
			unmerged&=~(1<<min_j);

			/* Traverse distance list only if necessary */
			if (ways_assigned[min_i]==1)
				delete_distances(&distance_list,min_i);
			if (ways_assigned[min_j]==1)
				delete_distances(&distance_list,min_j);

			/* This is the combined cluster already completed (pre merged) */
			cur_cluster=&cg->curves[min_i][min_j];

			ways_assigned[min_i]+=ways_assigned[min_j]; /* Join ways */

			/* Build new solution */
			new_solution=&cg->solutions[nr_solutions];
			init_sized_list(&new_solution->cluster_list, offsetof(candidate_cluster_t,link[nr_solutions]));
			nr_solutions++;

			/* Initialize structure */
			build_new_solution_merged(cur_solution,cur_cluster,min_i,min_j,ways_assigned, new_solution);

			/* Update best and curr */
			cur_solution=new_solution;

			/* Update best so far */
			if  (new_solution->unfairness < best_unfairness) {
				best_solution=new_solution;
				best_unfairness=new_solution->unfairness;
			}

		}

	} /* End pair clustering loop */


	/* Build cluster set appropriately to interact with LFOC */
	if (clusters) {
		sized_list_t* cluster_list=&best_solution->cluster_list;
		candidate_cluster_t* cand;
		cluster_info_t*  cur;

		for (i=0,cand=head_sized_list(cluster_list);
		     cand!=NULL;
		     cand=next_sized_list(cluster_list,cand),i++) {

			cur=allocate_new_cluster(clusters);
			cur->nr_ways=best_solution->ways_per_cluster[i];
			cur->nr_apps=cand->nr_apps;

			for (j=0; j<cand->nr_apps; j++)
				add_app_to_cluster(cur,cand->apps[j]);

		}
	}

	return best_solution;
}



/* ########################################### */
/* LFOC and LFOC+ cache-clustering algorithms  */
/* ########################################### */


int  lfoc_list(cluster_set_t* clusters, sized_list_t* apps, int nr_apps,  int nr_ways, int max_streaming, int use_pair_clustering, int max_nr_ways_streaming_part, int collide_streaming_parts)
{
#define LIGHT_PER_STREAMING 2
#define MAX_WAYS_STREAMING 2
	const int streaming_part_size=max_nr_ways_streaming_part;
	app_t* app;
	clustering_solution_t* cs;
	int streaming_per_part,nr_reserved_ways;
	int nr_light_sharing=0,nr_clusters=0,nr_sensitive=0,nr_streaming=0;
	sized_list_t light_sharing_apps, streaming_apps, sensitive_apps;
	/* Bounded arrays */
	int pos_streaming[MAX_WAYS_STREAMING];
	int pos_sensitive[MAX_CLUSTERS];
	int ucp_solution[MAX_CLUSTERS];
	int i,j;
	int fewer_ways_cluster=-1;
	int min_ways_ucp=1000*nr_ways;
	int nr_sensitive_apps=0;
	int nr_streaming_partitions=0;
	int nr_unkown=0;
	int nr_actual_streaming=0;
	cluster_info_t*  cur;

	nr_light_sharing=nr_clusters=nr_sensitive=nr_streaming=0;

	init_cluster_set(clusters);

	/* Initialize class lists */
	init_sized_list (&light_sharing_apps,
	                 offsetof(app_t,
	                          link_class));

	init_sized_list (&streaming_apps,
	                 offsetof(app_t,
	                          link_class));

	init_sized_list (&sensitive_apps,
	                 offsetof(app_t,
	                          link_class));

	/* As many clusters as the number of sensitive benchmarks */
	for (i=0, app=head_sized_list(apps); i<nr_apps; i++,app=next_sized_list(apps,app)) {
		int classification=app->type;
		app->app_id=i; /* For later processing */


		if (classification==CACHE_CLASS_SENSITIVE) {
			insert_sized_list_tail(&sensitive_apps,app);
			nr_sensitive_apps++;
		} else {
			if (classification==CACHE_CLASS_LIGHT) {
				insert_sized_list_tail(&light_sharing_apps,app);
				nr_light_sharing++;
			} else {
				/* Streaming is 1 or any other (including unkown) */
				insert_sized_list_tail(&streaming_apps,app);
				nr_streaming++;
				if (classification==CACHE_CLASS_STREAMING)
					nr_actual_streaming++;
				else
					nr_unkown++;
			}
		}
	} /* End for */


#define TRACE_AND_FAIL
#ifdef TRACE_AND_FAIL
	if (nr_unkown)
		return -EINVAL;
#endif

	/* Special corner case (1): NO sensitive apps: Create a single cluster for everyone */
	if (nr_sensitive_apps==0) {
		cur=allocate_new_cluster(clusters);
		cur->nr_ways=nr_ways;
		clusters->default_cluster=0;

		for (app=head_sized_list(apps); app!=NULL; app=next_sized_list(apps,app))
			add_app_to_cluster(cur,app);

		goto end_of_story;
	}

	if (nr_streaming>0) {
		/* Calculate number of ways reserved for streaming */
		streaming_per_part=max_streaming;
		nr_reserved_ways=((nr_streaming+streaming_per_part-1)*streaming_part_size)/streaming_per_part;  /* Round up */


		if (nr_reserved_ways>MAX_WAYS_STREAMING) {
			nr_reserved_ways=MAX_WAYS_STREAMING;
			/* Raise cap */
			streaming_per_part=(nr_streaming+nr_reserved_ways-1)/(nr_reserved_ways/streaming_part_size);
		}
	} else {
		nr_reserved_ways=0;
	}

	if (use_pair_clustering && nr_sensitive_apps>1) {
		/* Invoke pair_clustering for sensitive */
		cs=pair_clustering_core2(&sensitive_apps,sized_list_length(&sensitive_apps),nr_ways-nr_reserved_ways,clusters);

		/* Check error case (corner case) */
		if (!cs)
			return -ENOSPC;

		/* Increment cluster count */
		nr_clusters+=cs->nr_clusters;
		/* Number of sensitive clusters ... */
		nr_sensitive=cs->nr_clusters;

		/* Determine cluster with fewer ways and update
			pos_sensitive array
		*/
		for (i=0; i<clusters->nr_clusters; i++) {
			cluster_info_t* cluster=cluster_by_idx(clusters,i);
			pos_sensitive[i]=i;

			if (min_ways_ucp>cluster->nr_ways) {
				min_ways_ucp=cluster->nr_ways;
				fewer_ways_cluster=i;
			}
		}
	} else {

		/* Add sensitive clusters right here */
		for (i=0, app=head_sized_list(&sensitive_apps); app!=NULL; i++,app=next_sized_list(&sensitive_apps,app)) {
			cur=allocate_new_cluster(clusters);
			add_app_to_cluster(cur,app);
			pos_sensitive[nr_sensitive]=nr_sensitive;
			nr_sensitive++;
			nr_clusters++;
		}

		/* TODO: FIX If too many ways (assert) */
		if (nr_clusters>nr_ways)
			return -ENOSPC;

		/** Apply UCP to set of sensitive benchmarks **/
		lookahead_algorithm_list(&sensitive_apps, nr_sensitive, nr_ways-nr_reserved_ways, ucp_solution,0);

		for (i=0; i<clusters->nr_clusters; i++) {
			cluster_info_t* cluster=cluster_by_idx(clusters,i);
			cluster->nr_ways=ucp_solution[i];

			if (min_ways_ucp>cluster->nr_ways) {
				min_ways_ucp=cluster->nr_ways;
				fewer_ways_cluster=i;
			}
		}

	}

	/* Default cluster is cluster with fewer ways */
	clusters->default_cluster=fewer_ways_cluster;

	/* Assign streaming benchmarks */
	if (nr_reserved_ways) {
		app_t* cur_app=head_sized_list(&streaming_apps);

		if (collide_streaming_parts) {
			cluster_info_t* cluster=allocate_new_cluster(clusters);
			cluster->nr_ways=nr_reserved_ways;

			for (j=0; j<nr_streaming; j++,cur_app=next_sized_list(&streaming_apps,cur_app))
				add_app_to_cluster(cluster,cur_app);

			/* Keep track of clusters */
			/* Default cluster is now the last streaming cluster */
			clusters->default_cluster=nr_clusters;
			pos_streaming[0]=nr_clusters;
			nr_streaming_partitions=1;
		} else {
			int remaining_streaming=nr_streaming;
			int proportional_streaming;
			int stream_to_assign;

			nr_streaming_partitions=nr_reserved_ways/streaming_part_size;
			/* Round it up */
			proportional_streaming=(nr_streaming+nr_streaming_partitions-1)/nr_streaming_partitions;

			/* For each streaming way ... */
			for (i=0; i<nr_reserved_ways && remaining_streaming>0; i++) {
				/* Create new cluster */
				cluster_info_t* cluster=allocate_new_cluster(clusters);
				cluster->nr_ways=streaming_part_size;
				stream_to_assign=MIN(proportional_streaming,remaining_streaming);

				for (j=0; j<stream_to_assign; j++,cur_app=next_sized_list(&streaming_apps,cur_app)) {
					add_app_to_cluster(cluster,cur_app);
					remaining_streaming--;
				}

				/* Keep track of clusters */
				/* Default cluster is now the last streaming cluster */
				clusters->default_cluster=nr_clusters+i;
				pos_streaming[i]=nr_clusters+i;
				stream_to_assign=MIN(streaming_per_part,remaining_streaming);
			}
		}
	}


	/** Assign lights  (load_balance())
	Traverse streaming first ... [Require sorted stuff] **/

	if (nr_light_sharing>0) {

		if (nr_reserved_ways>0) {
			int idx_stream=0;

			while  ((nr_light_sharing>0) && (idx_stream<nr_streaming_partitions)) {
				cluster_info_t* cluster=cluster_by_idx(clusters,pos_streaming[idx_stream]);
				/* Determine how many we can actually fit in here */
				int room=(max_streaming*cluster->nr_ways)-cluster->nr_apps+1; /* Make at least one gap */

				if (room>0) {
					int light_to_assign=MIN(room*LIGHT_PER_STREAMING,nr_light_sharing);

					for (i=0; i<light_to_assign; i++) {
						app_t* app=head_sized_list(&light_sharing_apps);
						remove_sized_list(&light_sharing_apps,app);
						add_app_to_cluster(cluster,app);
						nr_light_sharing--;
					}
				}
				idx_stream++;
			}
		}

		/** Go ahead LIGHT SHARING among rest (Round robin assignment) **/

		i=0;

		while (nr_light_sharing>0) {
			cluster_info_t* cluster=cluster_by_idx(clusters,pos_sensitive[i%nr_sensitive]);
			app_t* app=head_sized_list(&light_sharing_apps);
			remove_sized_list(&light_sharing_apps,app);
			add_app_to_cluster(cluster,app);
			nr_light_sharing--;
			i++; /* Point to next sensitive cluster ... */
		}

	}
end_of_story:
	//return &clusters;
	return 0;
}

void noinline trace_clustering_solution(char* msg)
{
	asm(" ");

#ifdef DEBUG
	trace_printk("%s\n",msg);
#endif
}

/* Debugging function to trace_print_k resulting clustering solution */
void print_clustering_solution(cluster_set_t* clusters,int nr_apps)
{
	int per_app_clustering[MAX_APPS];
	app_t* app_vector[MAX_APPS];
	int per_cluster_ways[MAX_CLUSTERS];
	char* buf=clusters->buf;
	char* dest=buf;

	/* Traverse clusters to determine each app cluster  */
	int i;
	app_t* app;
	dest[0]='\0';

	for (i=0; i<clusters->nr_clusters; i++) {
		cluster_info_t* cluster=cluster_by_idx(clusters,i);

		per_cluster_ways[i]=cluster->nr_ways;

		for (app=head_sized_list(&cluster->apps);
		     app!=NULL;
		     app=next_sized_list(&cluster->apps,app)) {
			per_app_clustering[app->app_id]=i;
			app_vector[app->app_id]=app;
		}
	}

	/* Print applications and then masks */
	for (i=0; i<nr_apps; i++)
		dest+=sprintf(dest,"%.5s(%d) ",app_vector[i]->app_comm,per_app_clustering[i]);

	dest+=sprintf(dest,";");

	for (i=0; i<clusters->nr_clusters; i++) {
		if (i==clusters->default_cluster)
			dest+=sprintf(dest,"*");

		dest+=sprintf(dest,"%d ",per_cluster_ways[i]);
	}

	trace_clustering_solution(buf);
}


void noinline trace_app_assignment(unsigned long timestamp, app_t* app, cat_cache_part_t* partition)
{
	asm(" ");
}


void trivial_part(cluster_set_t* clusters, sized_list_t* apps, int nr_apps,  int nr_ways)
{
	cluster_info_t*  cur;
	app_t* app;

	init_cluster_set(clusters);

	cur=allocate_new_cluster(clusters);
	cur->nr_ways=nr_ways;
	clusters->default_cluster=0;

	for (app=head_sized_list(apps); app!=NULL; app=next_sized_list(apps,app))
		add_app_to_cluster(cur,app);
}

/* Map specific clustering approach to actual HW partitions */
void enforce_cluster_partitioning(cache_part_set_t* part_set, cluster_set_t* clusters)
{
	unsigned int nr_clusters=clusters->nr_clusters;
	sized_list_t* part_list=get_assigned_partitions(part_set);
	unsigned int old_cluster_count=sized_list_length(part_list);
	unsigned int used_clusters=0; /* Bitmask */
	unsigned int reserved_clusters=0;
	int partitions_to_remove=old_cluster_count-nr_clusters;
	cat_cache_part_t* partition=NULL;
	//cat_cache_part_t* new_partitions[MAX_CLUSTERS];
	//int new_part_index=0;
	int necessary_clusters=0;
	int i=0;
	int j=0;
	app_t* app;
	int nr_ways=get_cat_support(part_set)->cat_cbm_length;
	int low_way=0;
	unsigned long now=jiffies;


	/* Initialize new_partitions[] */
	//memset(new_partitions,0,sizeof(cat_cache_part_t*)*MAX_CLUSTERS);

	/*STEP 1: Allocate necessary clusters and initialize used_clusters */

	for (partition=head_sized_list(part_list); partition!=NULL; partition=next_sized_list(part_list,partition))
		used_clusters|=(1<<partition->part_index);

	if (nr_clusters>old_cluster_count) {
		necessary_clusters=nr_clusters-old_cluster_count;

		for (i=0; i<necessary_clusters; i++) {
			partition=allocate_empty_partition(part_set);
			//new_partitions[i]=partition;
			used_clusters|=(1<<partition->part_index);
		}

	}


	/*STEP 2: Traverse cluster set, create partitions and move necessary applications
		* Do not forget to update pointer of default cluster
	*/

	for (i=0; i<clusters->nr_clusters; i++) {
		cluster_info_t* cluster=cluster_by_idx(clusters,i);

		/* Determine on which CAT partition to map this cluster */
		app=head_sized_list(&cluster->apps);

		/* Get the first one available */
		while (app!=NULL && !part_available((partition=app->cat_partition),used_clusters,reserved_clusters))
			app=next_sized_list(&cluster->apps,app);

		/* Search for empty partition */
		j=0;

		while (j<MAX_CLUSTERS && !part_available_idx(j,used_clusters,reserved_clusters))
			j++;

		if  (j>=MAX_CLUSTERS)
			panic("We ran out of free partitions");

		partition=__get_partition_by_index(part_set,j);

		/* Set this partition as reserved */
		reserved_clusters|=(1<<partition->part_index);

		/* Update default partition if that's the case */
		if (clusters->default_cluster==i)
			update_default_partition(part_set,partition);

		/* Reconfigure partition at the low level (CAT) */
		reconfigure_partition(partition,cluster->nr_ways,nr_ways-low_way-cluster->nr_ways);
		low_way+=cluster->nr_ways;

		/* Move applications to the right place */
		for (app=head_sized_list(&cluster->apps); app!=NULL; app=next_sized_list(&cluster->apps,app)) {

			/* MAIN FINE-GRAINED TRACING POINT */
			trace_app_assignment(now, app, partition);

			if (app->cat_partition!=partition)
				move_app_to_partition(app,partition);
		}
	}

	/* At least one part must remain (the defaullt one) */
	if (	partitions_to_remove) {
		cat_cache_part_t *cur,*aux=NULL;
		/* Remove as many additional partitions as needed */
		cur=head_sized_list(part_list);

		while (cur!=NULL && partitions_to_remove>0) {
			aux=next_sized_list(part_list,cur);


			if ((get_default_partition(part_set))!=cur &&  get_load_partition(cur)==0) {
				deallocate_partition_no_resize(part_set,cur);
				partitions_to_remove--;
			}

			cur=aux;
		}

	}

}