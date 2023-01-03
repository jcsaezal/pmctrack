/*
 *  include/pmc/amd/amd17_hw_events.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef AMD17_L3_EVENTS_H
#define AMD17_L3_EVENTS_H



/* Load implementation */
#include <pmc/amd/amd_hw_events.h>
#include <pmc/amd/pmc_bit_layout.h>
#include <pmc/amd/pmc_const.h>

typedef enum {
	PMC_SAVE_CTX,
	PMC_RESTORE_CTX,
	PMC_UPDATE_CTX
} pmc_context_t;


static void init_amd_l3_event(amd_hw_event_t* event, unsigned int evtsel,  unsigned int umask)
{
	l3evtsel_msr l3evtsel;
	amd_simple_exp* se=&event->g_event.s_exp;
	int counter_id;

	if (evtsel==AMD17_L3_MISSES_EVTSEL)
		counter_id=0;
	else
		counter_id=4;

	init_l3evtsel_msr(&l3evtsel);
	set_bit_field(&l3evtsel.m_evtsel, evtsel);
	set_bit_field(&l3evtsel.m_umask, umask);
	set_bit_field(&l3evtsel.m_en, 1);
	set_bit_field(&l3evtsel.m_slice_mask, 0xf); /* 4 slices active */
	set_bit_field(&l3evtsel.m_thread_mask, 0xff); /* 8 threads active (to be changed dynamically) */

	amd_init_simple_exp (se, 				/* Simple exp */
	                     L3_PERF_PMC_MSR0+counter_id*L3_PERF_PMC_MSR_INCR,
	                     L3_PERF_PMC_RDPMC0+counter_id, /* Assigned PMC ID */
	                     L3_PERF_EVTSEL0+counter_id*L3_PERF_EVTSEL_INCR,
	                     l3evtsel.m_value,
	                     0
	                    );
	//printk(KERN_INFO "L3_PERF_EVTSEL VALUE=0x%llx\n",l3evtsel.m_value);
}

static inline void init_amd_l3_events(amd_hw_event_t* events)
{
	init_amd_l3_event(&events[0],AMD17_L3_MISSES_EVTSEL,AMD17_L3_MISSES_UMASK);
	init_amd_l3_event(&events[1],AMD17_L3_ACCESSES_EVTSEL,AMD17_L3_ACCESSES_UMASK);
}


static uint64_t handle_amd_l3_event(amd_hw_event_t* event, pmc_context_t context, unsigned int cpu)
{
	uint64_t value=0;
	int ccx_core_id=cpu%4; /* Assuming SMT is disabled */
	amd_simple_exp* se;

	switch(context) {
	case PMC_SAVE_CTX:
		event->count+=__amd_save_and_clear_context_hw_event(event);
		break;
	case PMC_RESTORE_CTX:
		/* Select the right mask */
		se=&event->g_event.s_exp;
		if ((se->evtsel.new_value & 0xff)==AMD17_L3_MISSES_EVTSEL) {
			se->pmc.pmc_address=L3_PERF_PMC_RDPMC0+ccx_core_id;
			se->pmc.msr.address=L3_PERF_PMC_MSR0+ccx_core_id*L3_PERF_PMC_MSR_INCR;
			se->evtsel.address =L3_PERF_EVTSEL0+ccx_core_id*L3_PERF_EVTSEL_INCR;
		}
		/* Clear and set right thread mask */
		se->evtsel.new_value &=~(0xffULL<<56);
		se->evtsel.new_value |=(0x3ULL<<(56+2*ccx_core_id));
		__amd_restart_count_hw_event(event);
		value=event->count;
		break;
	case PMC_UPDATE_CTX:
		event->count+=__amd_read_and_restart_count_hw_event(event);
		value=event->count;
		event->count=0;
	}

	return value;
}

static void handle_amd_l3_events(amd_hw_event_t* events, pmc_context_t context, unsigned int cpu, uint64_t* values)
{
	int i=0;

	for (i=0; i<AMD_MAX_L3_EVENTS; i++) {
		if (values)
			values[i]=handle_amd_l3_event(&events[i],context,cpu);
		else
			handle_amd_l3_event(&events[i],context,cpu);
	}

}
#endif
