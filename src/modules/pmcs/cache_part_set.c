#include <pmc/cache_part.h>
#include <linux/random.h>

#define determine_part_mask(part) ((1<<(part)->nr_ways)-1)<<(part)->low_way


int init_cache_part_set(cache_part_set_t* part_set, intel_cat_support_t* cat_support )
{
	int i=0;
	/* Allocate memory for partition pool */
	part_set->partition_pool=kmalloc(sizeof(cat_cache_part_t)*cat_support->cat_nr_cos_available-1,GFP_KERNEL);

	if (!part_set->partition_pool)
		return -ENOMEM;

	init_sized_list (&part_set->assigned_partitions,
	                 offsetof(cat_cache_part_t,
	                          links));

	init_sized_list (&part_set->free_partitions,
	                 offsetof(cat_cache_part_t,
	                          links));

	init_sized_list (&part_set->defered_cos_assignment,
	                 offsetof(app_t,
	                          link_defered_cos_assignment));

	/* Initially all partitions are unused  */
	for (i=0; i<cat_support->cat_nr_cos_available-1; i++) {
		cat_cache_part_t* part=&part_set->partition_pool[i];
		/* Basic initialization */
		part->clos_id=i+1; /* Note that CLOS 0 is reserved for the OS) */
		part->part_id=-1; /* Unassigned */
		part->part_index=i;
		part->low_way=0;
		part->high_way=cat_support->cat_cbm_length-1;
		part->nr_ways=cat_support->cat_cbm_length;
		part->nr_apps=0;
		part->has_extra_way=0;
		part->bias=0;
		part->part_mask=determine_part_mask(part);
		part->part_old_mask=part->part_mask;
		insert_sized_list_tail(&part_set->free_partitions, part);
		/* Init list of applications (per partition) */
		init_sized_list (&part->assigned_applications,
		                 offsetof(app_t,
		                          link_in_partition));
		part->nr_light_sharing=0;
		part->pset=part_set;

	}

	/* For the way bouncing algorithm */
	part_set->nr_bouncing_ways=0;
	part_set->default_partition=NULL; /* Nothing thus far */
	part_set->cat_support=cat_support;

	return 0;
}

void free_up_part_set(cache_part_set_t* part_set)
{
	if (part_set->partition_pool) {
		kfree(part_set->partition_pool);
		part_set->partition_pool=NULL;
	}
}

/******* ###### Functions to manipulate cache partitions ###### ******/

/* Returns number of gap (1...n-1) - has nothing to do with ways- where to insert a new partition */
static inline int suitable_place_for_insertion(cache_part_set_t* pset, unsigned int nr_ways, unsigned int nr_old_partitions)
{

	/* Calculate fair partition assignment features */
	int nr_fair_ways=nr_ways/nr_old_partitions;
	int	nr_remaining_ways=nr_ways%nr_old_partitions;
	int hint_id;
	int max_val;
	int bias;
	int i=0;
	cat_cache_part_t* part;
	cat_cache_part_t* next;
	sized_list_t* list=&pset->assigned_partitions;

	/* Check whether partitions are perfectly balanced or not */
	if (nr_remaining_ways==0) {
		/** Pick one randomly **/
		hint_id=(get_random_int()%(nr_old_partitions-1));
		hint_id++; /* Normalize gap */
	} else {
		max_val=-1;
		hint_id=-1;

		part=head_sized_list(list);

		/* Valid values in this case (1...n-1) */
		for (i=1; i<nr_old_partitions; i++,part=next) {
			next=next_sized_list(list,part);
			bias=part->nr_ways+next->nr_ways-2*nr_fair_ways;

			if (bias > max_val) {
				hint_id=i;
				max_val=bias;
			}
		}
	}
	return hint_id;
}


/** ### Functions for manual adjustment of partitions ## **/
void reconfigure_partition(cat_cache_part_t* part, unsigned int ways_assigned, unsigned int low_way)
{
	part->low_way=low_way;
	part->high_way=low_way+ways_assigned-1;
	part->nr_ways=ways_assigned;
	part->part_mask=determine_part_mask(part);
	part->part_old_mask=part->part_mask;
	intel_cat_set_capacity_bitmask(part->pset->cat_support,part->clos_id,part->part_mask);
}


/* NOTE: To be used along with reconfigure_partition() later */
cat_cache_part_t* allocate_empty_partition(cache_part_set_t* part_set)
{
	cat_cache_part_t* new_partition=head_sized_list(&part_set->free_partitions);

	/* Remove new partition from linked list, since we're assigning it */
	remove_sized_list(&part_set->free_partitions,new_partition);

	new_partition->low_way=0;
	new_partition->high_way=1;
	new_partition->nr_ways=1;
	new_partition->part_mask=0x1;
	new_partition->part_old_mask=new_partition->part_mask;

	/* Assign part_id just in case ... */

	insert_sized_list_tail(&part_set->assigned_partitions,new_partition);
	new_partition->part_id=sized_list_length(&part_set->assigned_partitions)-1;

	return new_partition;
}

/* This is dangerous: we must ensure that the partition has been allocated */
cat_cache_part_t* __get_partition_by_index(cache_part_set_t* part_set, int idx)
{
	return &part_set->partition_pool[idx];
}


/*
 * Generic function to minimize the cost when adding more partitions
 * def part_remove_generic()
 */
void deallocate_partition(cache_part_set_t* pset, cat_cache_part_t* partition, unsigned int nr_ways)
{
	unsigned int idx_part_to_remove=partition->part_id;
	sized_list_t* list=&pset->assigned_partitions;
	unsigned int nr_old_partitions=sized_list_length(list);
	unsigned int nr_target_partitions=nr_old_partitions-1;
	unsigned int nr_fair_ways,nr_remaining_ways,nr_extra_ways_neighbor,give_away;
	unsigned int val,center;
	unsigned int low,high;
	unsigned int next_available_way;
	unsigned char ascending;
	unsigned char extra_way;
	int i; //idx;
	cat_cache_part_t *cur; //*next,*prev;

	/* 	Determine whether the removed partition is located to the right or to the left*/
	center=nr_ways>>1;
	val=(partition->nr_ways>>1)+partition->low_way;
	ascending=(val<=center);

#ifdef DEBUG
	trace_printk("Removing partition #%d. Cur #items=%lu\n",idx_part_to_remove,sized_list_length(list));
#endif
	/* Treat easy case first */
	if (nr_target_partitions==0) {
		remove_sized_list(list,partition);
		pset->nr_bouncing_ways=0;
		goto out_remove;
	}

	/* Calculate fair partition assignment */
	nr_fair_ways=nr_ways/nr_target_partitions;
	nr_remaining_ways=nr_ways%nr_target_partitions;
	pset->nr_bouncing_ways=nr_remaining_ways;

	/* Decide on how much to give to the neighbors */
	if (idx_part_to_remove==0 || idx_part_to_remove==(nr_old_partitions-1)) {
		nr_extra_ways_neighbor=0;
	} else {
		if (nr_remaining_ways<2)
			give_away=nr_remaining_ways;
		else
			give_away=2;
		nr_extra_ways_neighbor=give_away;
		nr_remaining_ways=nr_remaining_ways-give_away;
	}


	/* Remove partition from list (first things first) */
	remove_sized_list(list,partition);

	if (ascending) {
		next_available_way=0;

		for (cur=head_sized_list(list),i=0; cur!=NULL; i++,cur=next_sized_list(list,cur)) {
			low=next_available_way;
			high=next_available_way+nr_fair_ways-1;
			extra_way=0;

			/* Check whether or not it deserves an extra way */
			if ((nr_extra_ways_neighbor>0) && ((i==idx_part_to_remove) || (i==idx_part_to_remove-1))) {
				high++;
				extra_way=1;
				nr_extra_ways_neighbor--;
			} else if (nr_remaining_ways > 0) {
				high++;
				extra_way=1;
				nr_remaining_ways--;
			}

			/* Initialize this_partition */
			cur->low_way=low;
			cur->high_way=high;
			cur->nr_ways=high-low+1;
			cur->part_id=i;
			cur->has_extra_way=extra_way;
			cur->bias=0; /* Reset bias counter */
			cur->part_mask=determine_part_mask(cur);
			cur->part_old_mask=cur->part_mask;
			intel_cat_set_capacity_bitmask(pset->cat_support,cur->clos_id,cur->part_mask);

			/* Proceed with next partition */
			next_available_way=high+1;
		}
	} else {
		next_available_way=nr_ways-1;

		for (cur=tail_sized_list(list),i=nr_old_partitions-2; cur!=NULL; i--,cur=prev_sized_list(list,cur)) {
			high=next_available_way;
			low=next_available_way-nr_fair_ways+1;
			extra_way=0;

			/* Check whether or not it deserves an extra way */
			if ((nr_extra_ways_neighbor>0) && ((i==idx_part_to_remove) || (i==idx_part_to_remove-1))) {
				low--;
				extra_way=1;
				nr_extra_ways_neighbor--;
			} else if (nr_remaining_ways > 0) {
				low--;
				extra_way=1;
				nr_remaining_ways--;
			}

			/* Update this_partition */
			cur->low_way=low;
			cur->high_way=high;
			cur->nr_ways=high-low+1;
			cur->part_id=i;
			cur->has_extra_way=extra_way;
			cur->bias=0; /* Reset bias counter */
			cur->part_mask=determine_part_mask(cur);
			cur->part_old_mask=cur->part_mask;
			intel_cat_set_capacity_bitmask(pset->cat_support,cur->clos_id,cur->part_mask);

			/* Proceed with next partition */
			next_available_way=low-1;
		}
	}

out_remove:
#ifdef DEBUG
	trace_printk("Removed partition #%d. Cur #items=%lu\n",idx_part_to_remove,sized_list_length(list));
#else
	return;
#endif
}

/* Removes partition without adjusting it */
void deallocate_partition_no_resize(cache_part_set_t* pset, cat_cache_part_t* partition)
{
	remove_sized_list(&pset->assigned_partitions,partition);
	/* Insert partition at the front of the pool (stack-like) */
	insert_sized_list_head(&pset->free_partitions, partition);
}

void remove_empty_partitions(cache_part_set_t* pset, int auto_resize)
{
	cat_cache_part_t *cur,*aux=NULL;
	sized_list_t* part_list=&pset->assigned_partitions;

	/* Remove as many additional partitions as needed */
	cur=head_sized_list(part_list);

	while (cur!=NULL) {
		aux=next_sized_list(part_list,cur);
		/* Remove empty partitions and resize the rest */
		if (get_load_partition(cur)==0) {

			if (auto_resize) {
				deallocate_partition(pset,cur,pset->cat_support->cat_cbm_length);
				/* Insert partition at the front of the pool (stack-like) */
				insert_sized_list_head(&pset->free_partitions, cur);
			} else
				deallocate_partition_no_resize(pset, cur);
		}

		cur=aux;
	}
}


static void do_insert_partition(cache_part_set_t* pset, cat_cache_part_t* new_partition, unsigned int nr_old_partitions, unsigned int nr_ways, int gap_id)
{
	int nr_target_partitions=nr_old_partitions+1;
	unsigned int max_gap_id=nr_old_partitions;
	unsigned char ascending;
	unsigned char extra_way;
	unsigned int nr_extra_ways_neighbor,give_away;
	unsigned int low,high;
	unsigned int next_available_way;
	int i;
	cat_cache_part_t *cur; //*this_partition,*next,*prev;
	sized_list_t* list=&pset->assigned_partitions;

	/* Calculate fair partition assignment */
	int nr_fair_ways=nr_ways/nr_target_partitions;
	int	nr_remaining_ways=nr_ways%nr_target_partitions;

	pset->nr_bouncing_ways=nr_remaining_ways;

#ifdef DEBUG
	trace_printk("Inserting new partition using gap #%d. Cur #items=%lu\n",gap_id,sized_list_length(list));
#endif
	/* Treat easy case separately */
	if (nr_target_partitions==1) {
		new_partition->low_way=0;
		new_partition->high_way=nr_fair_ways-1;
		new_partition->nr_ways=nr_fair_ways;
		new_partition->part_id=0;
		new_partition->has_extra_way=0;
		new_partition->bias=0; /* Reset bias counter */
		new_partition->part_mask=determine_part_mask(new_partition);
		new_partition->part_old_mask=new_partition->part_mask;
		intel_cat_set_capacity_bitmask(pset->cat_support,new_partition->clos_id,new_partition->part_mask);
		insert_sized_list_tail(list,new_partition);
		goto out_insert;
	}

	/** Determine whether the inserted partition is located
		to the right or to the left **/
	ascending=(gap_id<=(nr_old_partitions>0x1)); /* Divide by 2 */

	/* Decide on how much to give to the neighbors */
	if (gap_id==0 || gap_id==max_gap_id) {
		nr_extra_ways_neighbor=0;
	} else {
		if (nr_remaining_ways<2)
			give_away=nr_remaining_ways;
		else
			give_away=2;
		nr_extra_ways_neighbor=give_away;
		nr_remaining_ways=nr_remaining_ways-give_away;
	}


	/* Insert partition where it belongs first */


	if (gap_id==0)
		insert_sized_list_head(list,new_partition);
	else if (gap_id>=nr_target_partitions-1)
		insert_sized_list_tail(list,new_partition);
	else {
		/* Find position to insert */
		for (cur=head_sized_list(list),i=0; cur!=NULL && i!=gap_id; i++, cur=next_sized_list(list,cur)) {}

		insert_before_sized_list(list,cur,new_partition);
	}


	if (ascending) {
		next_available_way=0;

		for (cur=head_sized_list(list),i=0; cur!=NULL; i++,cur=next_sized_list(list,cur)) {
			low=next_available_way;
			high=next_available_way+nr_fair_ways-1;
			extra_way=0;

			/* Check whether or not it deserves an extra way */
			if ((nr_extra_ways_neighbor>0) && ((i==gap_id+1) || (i==gap_id-1))) {
				high++;
				extra_way=1;
				nr_extra_ways_neighbor--;
			} else if ((nr_remaining_ways > 0) && (i!=gap_id)) {
				high++;
				extra_way=1;
				nr_remaining_ways--;
			}

			/* Initialize this_partition */
			cur->low_way=low;
			cur->high_way=high;
			cur->nr_ways=high-low+1;
			cur->part_id=i;
			cur->has_extra_way=extra_way;
			cur->bias=0; /* Reset bias counter */
			cur->part_mask=determine_part_mask(cur);
			cur->part_old_mask=cur->part_mask;

			intel_cat_set_capacity_bitmask(pset->cat_support,cur->clos_id,cur->part_mask);
			/* Proceed with next partition */
			next_available_way=high+1;
		}
	} else {
		next_available_way=nr_ways-1;
		cur=tail_sized_list(list);

		for (cur=tail_sized_list(list),i=nr_target_partitions-1; cur!=NULL; i--,cur=prev_sized_list(list,cur)) {
			high=next_available_way;
			low=next_available_way-nr_fair_ways+1;
			extra_way=0;

			/* Check whether or not it deserves an extra way */
			if ((nr_extra_ways_neighbor>0) && ((i==gap_id+1) || (i==gap_id-1))) {
				low--;
				extra_way=1;
				nr_extra_ways_neighbor--;
			} else if ((nr_remaining_ways > 0) && (i!=gap_id)) {
				low--;
				extra_way=1;
				nr_remaining_ways--;
			}

			/* Initialize this_partition */
			cur->low_way=low;
			cur->high_way=high;
			cur->nr_ways=high-low+1;
			cur->part_id=i;
			cur->has_extra_way=extra_way;
			cur->bias=0; /* Reset bias counter */
			cur->part_mask=determine_part_mask(cur);
			cur->part_old_mask=cur->part_mask;
			intel_cat_set_capacity_bitmask(pset->cat_support,cur->clos_id,cur->part_mask);

			/* Proceed with next partition */
			next_available_way=low-1;
		}
	}

out_insert:
#ifdef DEBUG
	trace_printk("Inserted new partition using gap #%d. Cur #items=%lu\n",gap_id,sized_list_length(list));
#else
	return;
#endif

}

/* Functions and global data to manage partitions */
cat_cache_part_t* allocate_new_partition(cache_part_set_t* pset, unsigned int nr_ways, int hint_id)
{
	unsigned int nr_old_partitions=sized_list_length(&pset->assigned_partitions);
	cat_cache_part_t* new_partition=head_sized_list(&pset->free_partitions);

	/* Remove new partition from linked list, since we're assigning it */
	remove_sized_list(&pset->free_partitions,new_partition);

	if (nr_old_partitions<2)
		hint_id=0;
	else if (hint_id==-1 || hint_id >nr_old_partitions)
		hint_id=suitable_place_for_insertion(pset,nr_ways,nr_old_partitions);

	do_insert_partition(pset,new_partition,nr_old_partitions,nr_ways,hint_id);

	return new_partition;
}

/* Functions and global data to manage partitions */

//#define TRACE_MOVEMENT
#ifdef TRACE_MOVEMENT
static inline void trace_app_movement(app_t* app, cat_cache_part_t* from, cat_cache_part_t* to,int where)
{
	dyn_cache_part_thread_data_t* t=head_sized_list(&app->app_active_threads);
	struct task_struct* p = t->prof->this_tsk;
	char comm[TASK_COMM_LEN];
	get_task_comm(comm, p);

	/* Leaves */
	if (from==NULL)
		trace_printk("Task with pid %d (%s) added at %d to partition %u\n",(int) p->pid,comm,where,to->part_index);
	else if (to==NULL)
		trace_printk("Task with pid %d (%s) removed at %d from partition %u\n",(int) p->pid,comm,where,from->part_index);
	else
		trace_printk("Task with pid %d (%s) moved at %d from partition %u to %u\n",(int) p->pid,comm,where,from->part_index,to->part_index);
}
#else
static noinline void trace_app_movement(app_t* app, cat_cache_part_t* from, cat_cache_part_t* to,int where)
{
	asm(" ");
}
#endif


static inline void  __add_app_to_partition(app_t* app, cat_cache_part_t* partition, int where)
{
	trace_app_movement(app,app->cat_partition,partition,where);

	/* We assume in this case that partition!=NULL */
	app->cat_partition=partition;
	/* Importante cuando se va intentar pillar la misma particion */
	app->last_partition=app->cat_partition->part_index;  /*app->cat_partition->part_id; */
	partition->nr_apps++; /* Update nr_apps!! */
	/* Update cos for consistency */
	app->app_cmt_data.cos_id=partition->clos_id;
	insert_sized_list_tail(&partition->assigned_applications,app);
}

static inline void  __remove_app_from_partition(app_t* app)
{
	cat_cache_part_t* partition=app->cat_partition;
	app->last_partition=partition->part_index;
	partition->nr_apps--;
	app->cat_partition=NULL;
	remove_sized_list(&partition->assigned_applications,app);
	trace_app_movement(app,partition,NULL,0);

}


static inline void  __move_app_to_partition(app_t* app,cat_cache_part_t* new_partition, int where)
{
	cat_cache_part_t* partition=app->cat_partition;

	if (partition == new_partition)
		return;

	__remove_app_from_partition(app);
	__add_app_to_partition(app,new_partition,where);

#ifdef DEBUG
	{
		dyn_cache_part_thread_data_t* t=head_sized_list(&app->app_active_threads);
		struct task_struct* p = t->prof->this_tsk;
		char comm[TASK_COMM_LEN];
		get_task_comm(comm, p);

		trace_printk("Task with pid %d (%s) moved at %d from partition %u to %u\n",(int) p->pid,comm,where,partition->part_id,new_partition->part_id);
	}
#endif
}


void move_app_to_partition(app_t* app, cat_cache_part_t* new_partition)
{
	__move_app_to_partition(app,new_partition,0);

	/* SCHEDULE Update CLOS & RMID for each active thread .. THIS SHOULD BE DEFERRED WORK */
	insert_sized_list_tail(&new_partition->pset->defered_cos_assignment,app);
}


static inline cat_cache_part_t* get_least_loaded_partition(cache_part_set_t* part_set, int hint)
{

	cat_cache_part_t *cur=NULL;
	cat_cache_part_t *candidate_part=NULL;
	int load_min=9999;
	int cur_load=0;

	sized_list_t* list=&part_set->assigned_partitions;

	if (is_empty_sized_list(list))
		return NULL;

	/* No hint (newcomer) */
	if ((hint==-1) && part_set->default_partition) {
		return part_set->default_partition;
	}

	for (cur=head_sized_list(list); cur!=NULL; cur=next_sized_list(list,cur)) {
		/* Try to get the last one first */
		if (cur->part_index == hint) // && get_load_partition(cur)==0)
			return cur;

		if  ((cur_load=get_load_partition(cur))<load_min) {
			load_min=cur_load;
			candidate_part=cur;
		}
	}

	return candidate_part;
}

/* Invoked with the global lock held */
void assign_partition_to_application(cache_part_set_t* part_set, app_t* app, cat_cache_part_t* forced)
{
	cat_cache_part_t* partition;

	if (forced) {
		__add_app_to_partition(app,forced,0);
	} /* Handle extreme case: we ran out of partitions */
	else if (is_empty_sized_list(&part_set->free_partitions)) {
		app->last_partition=-1;
		app->cat_partition=NULL;
		app->app_cmt_data.cos_id=0; /* Use default partition */
		return;
	} else {

		partition=get_least_loaded_partition(part_set,app->last_partition);

		/* Allocate a new partition if no suitable partition was found */
		if (!partition)
			partition=allocate_new_partition(part_set,part_set->cat_support->cat_cbm_length,app->last_partition);

		__add_app_to_partition(app,partition,0);
	}

}


/* Invoked with the global lock held */
void  remove_application_from_partition(app_t* app)
{
	cat_cache_part_t* partition;
	/* Nothing to do? */
	if (!app->cat_partition) {
		app->last_partition=-1;
	} else {
		partition=app->cat_partition;
		__remove_app_from_partition(app);
	}
}


static noinline void trace_partition(cat_cache_part_t *part)
{
	asm(" ");

#ifdef DEBUG
	if (part)
		trace_printk("Part=(%u-%u),#ways=%u,ID=%u,CLOS=%u,MASK=0x%x,NR_APPS=%u\n",
		             part->low_way,part->high_way,part->nr_ways,part->part_id,part->clos_id,((1<<part->nr_ways)-1)<<part->low_way,part->nr_apps);
#endif
}


/* Global lock must be held */
void print_partition_info(cache_part_set_t* part_set)
{
	cat_cache_part_t *cur;
	sized_list_t* list=&part_set->assigned_partitions;

	if (is_empty_sized_list(list))
		return;

#ifdef DEBUG
	trace_printk("************** PARTITION_INFO **************\n");
#endif
	trace_partition(NULL);

	for (cur=head_sized_list(list); cur!=NULL; cur=next_sized_list(list,cur)) {
		trace_partition(cur);
	}

	trace_partition(cur);
#ifdef DEBUG
	trace_printk("*****************************************\n");
#endif
}

/********** END ROUTINES FOR PARTITION HANDLING ***********/
