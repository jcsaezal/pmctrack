/*
 *  include/pmc/amd/hw_events_inline.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef AMD_HW_EVENTS_INLINE_H
#define AMD_HW_EVENTS_INLINE_H


#ifdef _DEBUG_USER_MODE
#include "printk.h"
#include <stdio.h>
#include <string.h>
/* OLD PMC Debug functions */
#ifdef OLD_DEBUG_USER_MODE
#include <newcalls.h>
#endif

#elif defined(__linux__)
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/smp.h>
#else	/* Solaris kernel */
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/ctype.h>
#include <sys/archsystm.h>
#include <sys/cmn_err.h>
#endif  /* _DEBUG_USER_MODE */


/*******************************************************************************
*******************************************************************************/


/* Initialization */
static  inline  void init_simple_exp (	simple_exp* se,
                                        uint_t pmc_msr_adress,
                                        uint_t pmc_adress,
                                        uint_t evtsel_adress,
                                        uint64_t  evtsel_value,
                                        uint64_t reset_value_pmc)
{
	se->pmc.pmc_address=pmc_adress;
	se->pmc.msr.address=pmc_msr_adress;
	se->pmc.msr.reset_value=reset_value_pmc;
	se->pmc.msr.new_value=reset_value_pmc; /* Initialy the new value should match the reset value!! */

	se->evtsel.address =evtsel_adress;
	se->evtsel.reset_value=0;
	se->evtsel.new_value=evtsel_value;

}



/* This function starts the count of a simple event */
static   inline void startCount_exp ( simple_exp * exp )
{
	writeMSR ( &exp->pmc.msr );	/* The counter is set to its previous value (context saved) */
	writeMSR ( &exp->evtsel );	/* Evtsel Configuration */
}

static inline void restartCount_exp ( simple_exp * exp )
{
	resetPMC ( &exp->pmc );		/* The counter is cleared */
	writeMSR ( &exp->evtsel );	/* Evtsel Configuration */
}

/* This function stops the count of a simple event */
static  inline  void stopCount_exp ( simple_exp * exp )
{
	resetMSR ( &exp->evtsel );	/* Clear the enable bit */
}

/* This function clears the count of a simple event */
static inline void clear_exp ( simple_exp * exp )
{
	resetPMC ( &exp->pmc );		/* The counter is set to zero */
}

/* This function reads the value from the simple event's PMC */
static inline void readCounter_exp ( simple_exp * exp )
{
	readPMC ( &exp->pmc );
}

/* Restore the context of pmc */
static inline void restoreContext_exp ( simple_exp * exp )
{
	writeMSR ( &exp->pmc.msr );
}




/******************************************************************************************/
/************* PROCESSOR-SPECIFIC IMPLEMENTATION OF HW_EVENTS' OPERATIONS *****************/
/******************************************************************************************/



/* An additional initialization function is provided here */
static inline   void init_hw_event ( struct hw_event* exp,ll_event_type type )
{
	exp->type=type;
}


static inline void update_l3_evtsel(simple_exp *se)
{
	int cpu;
	int ccx_core_id; /* Assuming SMT is disabled */

	/* Do nothing for normal events */
	if (se->pmc.pmc_address<L3_PERF_PMC_RDPMC0)
		return;

	cpu=smp_processor_id();
	ccx_core_id=cpu%4;
	if ((se->evtsel.new_value & 0xff)==AMD17_L3_MISSES_EVTSEL) {
		se->pmc.pmc_address=L3_PERF_PMC_RDPMC0+ccx_core_id;
		se->pmc.msr.address=L3_PERF_PMC_MSR0+ccx_core_id*L3_PERF_PMC_MSR_INCR;
		se->evtsel.address =L3_PERF_EVTSEL0+ccx_core_id*L3_PERF_EVTSEL_INCR;
	}
	/* Clear and set right thread mask */
	se->evtsel.new_value &=~(0xffULL<<56);
	se->evtsel.new_value |=(0x3ULL<<(56+2*ccx_core_id));
}



/* This function starts the count of a HW event */
static inline void  __start_count_hw_event ( struct hw_event* exp )
{
	simple_exp *s_exp=NULL;


	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		update_l3_evtsel( s_exp );
		startCount_exp ( s_exp );
		break;
	default:
		break;


	}
}


/* This function starts the count of a HW event */
static inline void  __restart_count_hw_event ( struct hw_event* exp )
{
	simple_exp *s_exp=NULL;


	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		update_l3_evtsel( s_exp );
		restartCount_exp ( s_exp );
		break;
	default:
		break;

	}


}



/* This function stops the count of a HW event */
static inline void __stop_count_hw_event ( struct hw_event* exp )
{
	simple_exp *s_exp=NULL;
	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		stopCount_exp ( s_exp );
		break;
	default:
		break;

	}

}



/* This function clears the count of a HW event */
static inline void	__clear_count_hw_event ( struct hw_event* exp )
{
	simple_exp *s_exp=NULL;
	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		clear_exp ( s_exp );
		break;
	default:
		break;

	}
}


/* This function saves the context of a HW event (Simply reads the PMC and stores it in new value) */
static inline void __save_context_hw_event (struct hw_event* exp)
{
	simple_exp *s_exp=NULL;
	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		readCounter_exp ( s_exp );
		break;
	default:
		break;
	}
}

/* This function restores the context of a HW event */
static inline void __restore_context_hw_event (struct hw_event* exp )
{
	simple_exp *s_exp=NULL;
	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		update_l3_evtsel( s_exp );
		restoreContext_exp ( s_exp );
		break;
	default:
		break;
	}
}





/* This function reads the value from the HW event's PMC */
static inline void __read_count_hw_event ( struct hw_event* exp )
{
	simple_exp *s_exp=NULL;
	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		readCounter_exp ( s_exp );
		break;
	default:
		break;
	}


}



/* This function returns the last value gathered from the PMC */
static inline uint64_t __get_last_value_hw_event ( struct hw_event* exp )
{
	simple_exp *s_exp=NULL;
	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		return s_exp->pmc.msr.new_value;
	//break;
	default:
		break;
	}

	return 0;
}

/* This function returns PMC's reset value */
static inline uint64_t __get_reset_value_hw_event ( struct hw_event* exp )
{
	simple_exp *s_exp=NULL;
	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		return s_exp->pmc.msr.reset_value;
	default:
		break;
	}

	return 0;
}


/* This function returns PMC's reset value */
static inline void __set_reset_value_hw_event ( struct hw_event* exp, uint64_t reset_val)
{
	simple_exp *s_exp=NULL;
	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		s_exp->pmc.msr.reset_value=reset_val;
		break;
	default:
		break;
	}
}


#endif
