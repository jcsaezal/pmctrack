/* =======================================================================
 * Dummy scheduling plugin for PMCSched
 * Author Carlos Bilbao
 * =======================================================================
*/

#include <pmc/pmcsched.h>

static void sched_kthread_periodic_dummy (sized_list_t* migration_list) {}

static void on_active_thread_dummy(pmcsched_thread_data_t* t) {}

static void on_inactive_thread_dummy(pmcsched_thread_data_t* t) {}

static void on_exit_thread_dummy (pmcsched_thread_data_t* t) {}

sched_ops_t dummy_plugin = {
	.policy                   = SCHED_DUMMY_MM,
	.description              = "Dummy default plugin (Proof of concept)",
	.flags                      = PMCSCHED_CUSTOM_LOCK,
	.sched_kthread_periodic   = sched_kthread_periodic_dummy,
	.counter_config=NULL, /* No counter configuration */
	.on_active_thread         = on_active_thread_dummy,
	.on_inactive_thread       = on_inactive_thread_dummy,
	.on_exit_thread           = on_exit_thread_dummy,
};
