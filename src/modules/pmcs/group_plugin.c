/* ======================================
 * Group scheduling plugin for PMCSched
 * Author Juan Carlos Saez (Oct 2021)
 * ======================================
*/

#include <pmc/pmcsched.h>
#include <linux/random.h>


void noinline trace_sticky_migration(struct task_struct* p, pmcsched_thread_data_t* t, int src_group, int dst_group)
{
	asm(" ");
}

static void
sched_kthread_periodic_group (sized_list_t* foo_list)
{

	sched_thread_group_t* cur_group=get_cur_group_sched();
	sched_thread_group_t* dst_group;
	sized_list_t local_list;
	sized_list_t* migration_list=&cur_group->migration_list;
	pmcsched_thread_data_t *elem,*next;
	migration_data_t* m;
	unsigned long flags;

	spin_lock_irqsave(&cur_group->lock,flags);

	if (sized_list_length(migration_list)==0) {
		spin_unlock_irqrestore(&cur_group->lock,flags);
		return;
	}

	init_sized_list(&local_list,
	                offsetof(pmcsched_thread_data_t, migration_links));

	elem = head_sized_list(migration_list);
	while(elem != NULL) {
		m=&elem->migration_data;
		/* Do not attempt to migrate already migrated threads (probably sleeping...) */
		if (m->state==MIGRATION_STARTED) {
			trace_sticky_migration(elem->prof->this_tsk,elem,m->src_group,m->dst_group);
			/* Get next first */
			next=next_sized_list(migration_list,elem);
			remove_sized_list(migration_list,elem);
			elem=next;
			continue;
		}

		dst_group=get_group_sched_by_id(m->dst_group);
		cpumask_copy(&m->dst_cpumask,
		             &dst_group->cpu_group->shared_cpu_map);

		/* To prevent process from going away */
		get_task_struct(elem->prof->this_tsk);
		m->state=MIGRATION_STARTED;
		insert_sized_list_tail(&local_list,elem);
		elem = next_sized_list(migration_list,elem);
	}

	spin_unlock_irqrestore(&cur_group->lock,flags);

	/* Lockfree migration process */
	for (elem = head_sized_list(&local_list);
	     elem != NULL ;
	     elem = next_sized_list(&local_list,elem)) {

		/* Thread exit ...*/
		if (!elem->migration_data.state) {
			trace_printk("Thread completed while migration in progress\n");
		} else if (set_cpus_allowed_ptr(elem->prof->this_tsk,
		                                &elem->migration_data.dst_cpumask) < 0) {
			trace_printk("%s: set_cpus_allowed_ptr failed.\n",__func__);
		}

		/* Restore task's reference counter */
		put_task_struct(elem->prof->this_tsk);
	}
}

static void
sched_timer_periodic_group (void)
{
	char buf[150]="";
	char* dest=buf;
	static int migration_counter=0;
	sched_thread_group_t* cur_group=get_cur_group_sched();
	app_t_pmcsched* cur;
	app_t* app;
	pmcsched_thread_data_t *first, *t;
	uint64_t cache_usage;
	uint_t llc_id=cur_group->cpu_group->group_id;

	if (sized_list_length(&cur_group->active_apps)==0)
		return;


	/* Traverse list  */
	for (cur=head_sized_list(&cur_group->active_apps);
	     cur!=NULL;
	     cur=next_sized_list(&cur_group->active_apps,cur)) {
		first=head_sized_list(&cur->app_active_threads);
		app=&cur->app_cache;

		intel_cmt_update_supported_events(&pmcs_cmt_support,&app->app_cmt_data,llc_id);
		cache_usage=app->app_cmt_data.last_llc_utilization[llc_id][0];
		/* Retrieve command from path */
		// get_task_comm(comm,first->prof->this_tsk);
		dest+=sprintf(dest,"%d(%d - %llu - %zuT) ",app->process->tgid,app->app_cmt_data.rmid,cache_usage,sized_list_length(&cur->app_active_threads));
	}

	trace_printk("[Group %i]. Active applications (#threads): %s\n",cur_group->cpu_group->group_id,buf);


	/* Random migration... */
	migration_counter++;

	if (migration_counter==3) {
		migration_counter=0;

		/* Grab the first thread of the first app and migrate it to a random group */
		cur=head_sized_list(&cur_group->active_apps);
		t=head_sized_list(&cur->app_active_threads);

		if (!t->migration_data.state) {
			int cpu_group_count;
			int target_group;

			get_platform_cpu_groups(&cpu_group_count);

			/* Generate random group id */
			do {
				target_group=get_random_long()%cpu_group_count;
			} while (target_group==llc_id);


			t->migration_data.state=MIGRATION_REQUESTED;
			t->migration_data.dst_group=target_group;
			t->migration_data.src_group=llc_id;
			insert_sized_list_tail(&cur_group->migration_list,t);
			cur_group->activate_kthread=1;
		} else {
			trace_printk("Attempted remigration for list with %zd items\n",sized_list_length(&cur_group->migration_list));
			cur_group->activate_kthread=1;
		}
	}
}

static void
on_active_thread_group(pmcsched_thread_data_t* t)
{
	sched_thread_group_t* cur_group=get_cur_group_sched();
	app_t_pmcsched* app=get_group_app_cpu(t,cur_group->cpu_group->group_id);

#ifdef DEBUG
	trace_printk("ACTIVE t=%p sched_group=%p app=%p\n",t,cur_group,app);
#endif

	t->cur_group=cur_group;
	/* Point to the right per-group resource monitoring, and allocation data */
	t->cmt_data=&app->app_cache.app_cmt_data;

	/* Insert structure in per-application and global lists */
	insert_sized_list_tail(&app->app_active_threads,t);
	insert_sized_list_tail(&cur_group->active_threads,t);

	/* Check if it's a new active application */
	if (sized_list_length(&app->app_active_threads)==1) {
		insert_sized_list_tail(&cur_group->active_apps,app);
#ifdef DEBUG
		trace_printk("An application just became active\n");
	} else {
		trace_printk("A thread of a multithreaded program just became active\n");
#endif
	}
}


/* Implemented to enable deactivations from a remote CPU */
static void
on_inactive_thread_group(pmcsched_thread_data_t* t)
{
	sched_thread_group_t* cur_group=t->cur_group;
	app_t_pmcsched* app=get_group_app_cpu(t,cur_group->cpu_group->group_id);

#ifdef DEBUG
	trace_printk("INACTIVE t=%p sched_group=%p app=%p\n",t,cur_group,app);
#endif

	/* Remove structure from per-application and global lists */
	remove_sized_list(&app->app_active_threads,t);
	remove_sized_list(&cur_group->active_threads,t);

	/* Check if it became an inactive application */
	if (sized_list_length(&app->app_active_threads)==0) {
		remove_sized_list(&cur_group->active_apps,app);
#ifdef DEBUG
		trace_printk("An application just became inactive\n");
	} else {
		trace_printk("A thread of a multithreaded program just became inactive\n");
#endif
	}

	t->cur_group=NULL;
}

static void on_exit_thread_group (pmcsched_thread_data_t* t)
{
	sched_thread_group_t* cur_group=t->cur_group;

	if (cur_group && t->migration_data.state) {
		remove_sized_list(&cur_group->migration_list,t);
		t->migration_data.state=MIGRATION_COMPLETED;
#ifdef DEBUG
		trace_printk("Interrupted migration due to thread exit (PID=%d).\n",t->prof->this_tsk->pid);
#endif
	}
}


static void on_migrate_thread_group(pmcsched_thread_data_t* t,
                                    int prev_cpu, int new_cpu)
{
	sched_thread_group_t* cur_group=get_cur_group_sched();
	sched_thread_group_t* old_group=t->cur_group;
	unsigned long flags;

	if (prev_cpu==-1 || (cur_group==old_group))
		return;

	if (old_group) {
		spin_lock_irqsave(&old_group->lock,flags);
		if (t->migration_data.state) {
			if (t->migration_data.dst_group!=cur_group->cpu_group->group_id)
				trace_printk("WARNING: Migration to wrong group\n");
			remove_sized_list(&old_group->migration_list,t);
			t->migration_data.state=MIGRATION_COMPLETED;
		}
		on_inactive_thread_group(t);
		spin_unlock_irqrestore(&old_group->lock,flags);
	}

	spin_lock_irqsave(&cur_group->lock,flags);
	on_active_thread_group(t);
	spin_unlock_irqrestore(&cur_group->lock,flags);
}



sched_ops_t group_plugin = {
	.policy                   = SCHED_GROUP_MM,
	.description              = "Group Scheduling Plugin (Proof of concept)",
	.flags                      = PMCSCHED_CPUGROUP_LOCK,
	.sched_kthread_periodic   = sched_kthread_periodic_group,
	.sched_timer_periodic   = sched_timer_periodic_group,
	.counter_config=NULL, /* No counter configuration */
	.on_active_thread         = on_active_thread_group,
	.on_inactive_thread       = on_inactive_thread_group,
	.on_exit_thread           = on_exit_thread_group,
	.on_migrate_thread        = on_migrate_thread_group,
};

