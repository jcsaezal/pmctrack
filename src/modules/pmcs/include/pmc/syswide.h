/*
 *  include/pmc/syswide.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef SYSWIDE_H
#define SYSWIDE_H

#include <linux/proc_fs.h>

/* Global init/cleanup functions */
int syswide_monitoring_init(void);
void syswide_monitoring_cleanup(void);

/* Return nozero if syswide_monitoring was actually disabled */
int syswide_monitoring_enabled(void);

/* Return non-zero if syswide_monitoring is not using PMCs
	- This enables to use virtual counters in system-wide mode
	  while using PMCs and other virtual counters
	  in any of the available per-thread modes
 */
int syswide_monitoring_switch_in(int cpu);
int syswide_monitoring_switch_out(int cpu);

/* Returns true if "p" is current syswide monitor process */
int is_syswide_monitor(struct task_struct* p);

/* Start/Stop syswide_monitoring */
int syswide_monitoring_start(void);
int syswide_monitoring_stop(void);


/* Pause/Resume syswide_monitoring */
int syswide_monitoring_pause(void);
int syswide_monitoring_resume(void);


#endif



