/* ======================================
 * Group scheduling plugin for PMCSched
 * Author Juan Carlos Saez (Oct 2021)
 * ======================================
*/

#include <pmc/pmcsched.h>
#include <pmc/pmcsched/amp_common.h>


extern void noinline trace_sticky_migration(struct task_struct* p, pmcsched_thread_data_t* t, int src_group, int dst_group);

static void
sched_kthread_periodic_busybcs (sized_list_t* foo_list)
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
sched_timer_periodic_busybcs (void)
{
	sched_thread_group_t* cur_group=get_cur_group_sched();
	sched_thread_group_t* big_core_group=get_group_sched_by_id(AMP_FAST_CORE);
	sched_thread_group_t* small_core_group=get_group_sched_by_id(AMP_SLOW_CORE);
	sched_thread_group_t* other_group;
	uint_t group_id=cur_group->cpu_group->group_id;
	int big_cores_available=0;
	int threads_on_small_cores=0;
	pmcsched_thread_data_t* t;
	int nr_migrations_pending;
	int remote_group_id=group_id==AMP_FAST_CORE?AMP_SLOW_CORE:AMP_FAST_CORE;
	int nr_migrations=0;
	int total_core_count=big_core_group->cpu_group->nr_online_cpus+small_core_group->cpu_group->nr_online_cpus;
	int nr_threads_total;

	other_group=get_group_sched_by_id(remote_group_id);

	/* Try to lock, to avoid deadlock  */
	if (spin_trylock(&other_group->lock)==0)
		return;

	nr_migrations_pending=sized_list_length(&big_core_group->migration_list)+sized_list_length(&small_core_group->migration_list);
	nr_threads_total=sized_list_length(&big_core_group->active_threads)+sized_list_length(&small_core_group->active_threads);
	big_cores_available=big_core_group->cpu_group->nr_online_cpus-sized_list_length(&big_core_group->active_threads);

	/* Ignore oversusbscription for now, as well as transient scenarios */
	if (nr_threads_total>total_core_count
	    || nr_migrations_pending > 0)
		goto exit_unlock;

	if (group_id==AMP_FAST_CORE) {
		threads_on_small_cores=sized_list_length(&small_core_group->active_threads);

		/* Do nothing if no threads were detected on small cores,
		 or no big cores available. */
		if (big_cores_available<=0 ||
		    threads_on_small_cores==0 )
			goto exit_unlock;

		/* Steal threads from small cores */
		t=head_sized_list(&small_core_group->active_threads);

		while(t && big_cores_available>0) {
			/* Initiate migration process */
			t->migration_data.state=MIGRATION_REQUESTED;
			t->migration_data.dst_group=AMP_FAST_CORE;
			t->migration_data.src_group=AMP_SLOW_CORE;
			insert_sized_list_tail(&small_core_group->migration_list,t);
			big_cores_available--;
			nr_migrations++;
			t=next_sized_list(&small_core_group->active_threads,t);
		}

		if (nr_migrations)
			wake_up_process(small_core_group->kthread);

	} else {
		/* Actions from small core */
		/* Big cores not oversubscribed?? */
		if (big_cores_available>=0)
			goto exit_unlock;

		/* Act only in case of oversubscription */
		/* Steal threads from big cores */
		t=head_sized_list(&big_core_group->active_threads);

		while(t && big_cores_available>=0) {
			/* Initiate migration process */
			t->migration_data.state=MIGRATION_REQUESTED;
			t->migration_data.dst_group=AMP_SLOW_CORE;
			t->migration_data.src_group=AMP_FAST_CORE;
			insert_sized_list_tail(&big_core_group->migration_list,t);
			big_cores_available++;
			nr_migrations++;
			t=next_sized_list(&big_core_group->active_threads,t);
		}

		if (nr_migrations)
			wake_up_process(big_core_group->kthread);

	}
exit_unlock:
	spin_unlock(&other_group->lock);
}

static void
on_active_thread_busybcs(pmcsched_thread_data_t* t)
{
	sched_thread_group_t* cur_group=get_cur_group_sched();
	app_t_pmcsched* app=get_group_app_cpu(t,cur_group->cpu_group->group_id);
	sched_thread_group_t* old_group=t->cur_group;

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

	/* Thread was migrated ... */
	if (old_group && t->cur_group!=old_group) {


	}

}


/* Implemented to enable deactivations from a remote CPU */
static void
on_inactive_thread_busybcs(pmcsched_thread_data_t* t)
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
}

static void on_exit_thread_busybcs (pmcsched_thread_data_t* t)
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


static void on_migrate_thread_busybcs(pmcsched_thread_data_t* t,
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
		on_inactive_thread_busybcs(t);
		spin_unlock_irqrestore(&old_group->lock,flags);
	}

	spin_lock_irqsave(&cur_group->lock,flags);
	on_active_thread_busybcs(t);
	spin_unlock_irqrestore(&cur_group->lock,flags);
}


static int probe_busybcs(void )
{
	int cpu_group_count;
	get_platform_cpu_groups(&cpu_group_count);
	return cpu_group_count>1 && get_nr_coretypes()==2;
}



sched_ops_t busybcs_plugin = {
	.policy                   = SCHED_BUSYBCS_MM,
	.description              = "AMP Scheduler that keeps big cores busy",
	.counter_config=NULL, /* No counter configuration */
	.flags                      = PMCSCHED_CPUGROUP_LOCK,
	.probe_plugin = probe_busybcs,
	.sched_kthread_periodic   = sched_kthread_periodic_busybcs,
	.sched_timer_periodic   = sched_timer_periodic_busybcs,
	.on_active_thread         = on_active_thread_busybcs,
	.on_inactive_thread       = on_inactive_thread_busybcs,
	.on_exit_thread           = on_exit_thread_busybcs,
	.on_migrate_thread        = on_migrate_thread_busybcs,
};

