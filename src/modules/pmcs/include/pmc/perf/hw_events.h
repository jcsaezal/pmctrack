/*
 *  include/pmc/perf/hw_events.h
 *
 *  Copyright (c) 2020 Jaime Saez de Buruaga <jsaezdeb@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef PERF_HW_EVENTS_H
#define PERF_HW_EVENTS_H

#include <linux/perf_event.h>
#include <linux/sched.h>

#include <pmc/perf/pmc_const.h>

#ifndef NULL
#define NULL 0
#endif

struct hw_event {
	struct perf_event *event;
	struct task_struct *task;
	uint64_t reset_value;
	uint64_t last_read_value;
	uint64_t old_value;
};

/* An additional initialization function is provided here */
static inline   void init_hw_event ( struct hw_event* exp)
{
	exp->event=NULL;
	exp->reset_value=0;
	exp->last_read_value=0;
	exp->old_value=0;
}

/* Low level function implementation */
static inline void __start_count_hw_event (struct hw_event* exp) {}
static inline void __restart_count_hw_event (struct hw_event* exp) {}
static inline void __stop_count_hw_event (struct hw_event* exp) {}
static inline void __save_context_hw_event (struct hw_event* exp) {}
static inline void __restore_context_hw_event (struct hw_event* exp) {}

static inline void __clear_count_hw_event (struct hw_event* exp)
{
	exp->last_read_value=0;
}

static inline void __read_count_hw_event (struct hw_event* exp)
{
	uint64_t enabled, running;
	uint64_t newval;
	newval=perf_event_read_value(exp->event,&enabled,&running);
	exp->last_read_value=newval-exp->old_value;
	exp->old_value=newval;
	//perf_event_read_local(exp->event,&exp->last_read_value,&enabled,&running);
}

static inline uint64_t __get_last_value_hw_event (struct hw_event* exp)
{
	return exp->last_read_value;
}

static inline uint64_t __get_reset_value_hw_event (struct hw_event* exp)
{
	return exp->reset_value;
}

static inline void __set_reset_value_hw_event (struct hw_event* exp, uint64_t reset_val)
{
	exp->reset_value = reset_val;
}

#endif
