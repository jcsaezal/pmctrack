/*
 *  include/pmc/corei7/hw_events_inline.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef COREI7_HW_EVENTS_INLINE_H
#define COREI7_HW_EVENTS_INLINE_H


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


/**************************************************************************************************************/

/* Initialization */
static inline void init_fixed_count_exp ( fixed_count_exp* fe,
        uint_t pmc_msr_adress,
        uint64_t evtsel_value,
        uint64_t reset_value_pmc)
{
	fe->pmc.address=pmc_msr_adress;//
	fe->pmc.reset_value=reset_value_pmc;
	fe->pmc.new_value=reset_value_pmc; /* Initialy the new value should match the reset value!! */

	fe->evtsel.address = MSR_PERF_FIXED_CTR_CTRL; /**** Common value ****/
	fe->evtsel.reset_value=0;		/* Clear 8 bytes */
	fe->evtsel.new_value=evtsel_value;	/* 8-byte copy */
}

/* This function starts the count of a fixed-count event */
static inline void startCount_fixed_exp ( fixed_count_exp * exp )
{
	writeMSR ( &exp->pmc );		/* The counter is set with the last value it had */
	writeMSR ( &exp->evtsel );	/* Evtsel Configuration*/
}

/* This function starts the count of a fixed-count event */
static inline void restartCount_fixed_exp ( fixed_count_exp * exp )
{
	writeMSR ( &exp->evtsel );	/* Evtsel Configuration*/
	resetMSR ( &exp->pmc );		/* The counter is cleared */
}

/* This function stops the count of a fixed-count event */
static inline void stopCount_fixed_exp ( fixed_count_exp * exp )
{
	resetMSR ( &exp->evtsel );	/* Clear the enable bit */

}

/* This function clears the count of a fixed-count event */
static inline void clear_fixed_exp ( fixed_count_exp * exp )
{
	resetMSR ( &exp->pmc );		/* The counter is set to zero */

}

/* This function reads the value from the fixed-count event's PMC */
static inline void readCounter_fixed_exp ( fixed_count_exp * exp )
{
	readMSR ( &exp->pmc );
}


/* Restore the context of pmc by writing new value on it */
static inline void restoreContext_fixed_exp ( fixed_count_exp * exp )
{
	writeMSR ( &exp->pmc );
}



/******************************************************************************************/
/************* PROCESSOR-SPECIFIC IMPLEMENTATION OF HW_EVENTS' OPERATIONS *****************/
/******************************************************************************************/


/* An additional initialization function is provided here */
static inline   void init_hw_event ( struct hw_event* exp,ll_event_type type )
{
	exp->type=type;
}



/* This function starts the count of a HW event */
static inline void  __start_count_hw_event ( struct hw_event* exp )
{
	simple_exp *s_exp=NULL;


	switch ( exp->type ) {
#ifdef OLD_DEBUG_USER_MODE
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		_KERNEL_CALL ( k_start_count,s_exp );

		break;
	case _FIXED:
		_KERNEL_CALL ( k_start_count_fixed,& ( exp->g_event.f_exp ) );
		break;
	default:
		break;
#else
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		startCount_exp ( s_exp );
		break;
	case _FIXED:
		startCount_fixed_exp ( & ( exp->g_event.f_exp ) );
		break;
	default:
		break;
#endif

	}


}

/* This function starts the count of a HW event */
static inline void  __restart_count_hw_event ( struct hw_event* exp )
{
	simple_exp *s_exp=NULL;


	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		restartCount_exp ( s_exp );
		break;
	case _FIXED:
		restartCount_fixed_exp ( & ( exp->g_event.f_exp ) );
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
#ifdef OLD_DEBUG_USER_MODE
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		_KERNEL_CALL ( k_stop_count,s_exp );

		break;
	case _FIXED:
		_KERNEL_CALL ( k_stop_count_fixed,& ( exp->g_event.f_exp ) );
		break;
	default:
		break;
#else
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		stopCount_exp ( s_exp );
		break;
	case _FIXED:
		stopCount_fixed_exp ( & ( exp->g_event.f_exp ) );
		break;
	default:
		break;
#endif
	}

}



/* This function clears the count of a HW event */
static inline void	__clear_count_hw_event ( struct hw_event* exp )
{
	simple_exp *s_exp=NULL;
	switch ( exp->type ) {
#ifdef OLD_DEBUG_USER_MODE
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		_KERNEL_CALL ( k_clear_count,s_exp );

		break;
	case _FIXED:
		_KERNEL_CALL ( k_clear_count_fixed,& ( exp->g_event.f_exp ) );
		break;
	default:
		break;
#else
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		clear_exp ( s_exp );
		break;

	case _FIXED:
		clear_fixed_exp ( & ( exp->g_event.f_exp ) );
		break;
	default:
		break;
#endif
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
	case _FIXED:
		readCounter_fixed_exp ( & ( exp->g_event.f_exp ) );
		break;
	default:
		break;
	}
}

/* This function saves the context of a HW event */
static inline void __restore_context_hw_event (struct hw_event* exp )
{
	simple_exp *s_exp=NULL;
	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		restoreContext_exp ( s_exp );
		break;
	case _FIXED:
		restoreContext_fixed_exp ( & ( exp->g_event.f_exp ) );
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
#ifdef OLD_DEBUG_USER_MODE
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		_KERNEL_CALL ( k_read_counter,s_exp );

		break;
	case _FIXED:
		_KERNEL_CALL ( k_read_counter_fixed,& ( exp->g_event.f_exp ) );
		break;
	default:
		break;
#else
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		readCounter_exp ( s_exp );
		break;
	case _FIXED:
		readCounter_fixed_exp ( & ( exp->g_event.f_exp ) );
		break;
	default:
		break;
#endif
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
	case _FIXED:
		return exp->g_event.f_exp.pmc.new_value;
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
	//break;
	case _FIXED:
		return exp->g_event.f_exp.pmc.reset_value;
	//break;
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
	case _FIXED:
		exp->g_event.f_exp.pmc.reset_value=reset_val;
		break;
	default:
		break;
	}
}



#endif
