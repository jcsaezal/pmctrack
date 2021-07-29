/*
 *  include/pmc/amd/hw_events_inline.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef AMD_HW_EVENTS_INLINE_H
#define AMD_HW_EVENTS_INLINE_H

#include <linux/kernel.h>
#include <linux/string.h>



/*******************************************************************************
*******************************************************************************/


/* Initialization */
static  inline  void amd_init_simple_exp (	amd_simple_exp* se,
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
static   inline void amd_startCount_exp ( amd_simple_exp * exp )
{
	writeMSR ( &exp->pmc.msr );	/* The counter is set to its previous value (context saved) */
	writeMSR ( &exp->evtsel );	/* Evtsel Configuration */
}

static inline void amd_restartCount_exp ( amd_simple_exp * exp )
{
	resetPMC ( &exp->pmc );		/* The counter is cleared */
	writeMSR ( &exp->evtsel );	/* Evtsel Configuration */
}

/* This function stops the count of a simple event */
static  inline  void amd_stopCount_exp ( amd_simple_exp * exp )
{
	resetMSR ( &exp->evtsel );	/* Clear the enable bit */
}

/* This function clears the count of a simple event */
static inline void amd_clear_exp ( amd_simple_exp * exp )
{
	resetPMC ( &exp->pmc );		/* The counter is set to zero */
}

/* This function reads the value from the simple event's PMC */
static inline void amd_readCounter_exp ( amd_simple_exp * exp )
{
	readPMC ( &exp->pmc );
}

/* Restore the context of pmc */
static inline void amd_restoreContext_exp ( amd_simple_exp * exp )
{
	writeMSR ( &exp->pmc.msr );
}




/******************************************************************************************/
/************* PROCESSOR-SPECIFIC IMPLEMENTATION OF HW_EVENTS' OPERATIONS *****************/
/******************************************************************************************/



/* An additional initialization function is provided here */
static inline   void amd_init_hw_event ( struct amd_hw_event* exp,ll_event_type type )
{
	exp->type=type;
	exp->count=0;
}



/* This function starts the count of a HW event */
static inline void  __amd_start_count_hw_event ( struct amd_hw_event* exp )
{
	amd_simple_exp *s_exp=NULL;


	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		amd_startCount_exp ( s_exp );
		break;
	default:
		break;


	}


}

/* This function starts the count of a HW event */
static inline void  __amd_restart_count_hw_event ( struct amd_hw_event* exp )
{
	amd_simple_exp *s_exp=NULL;


	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		amd_restartCount_exp ( s_exp );
		break;
	default:
		break;

	}


}

static inline uint64_t __amd_read_and_restart_count_hw_event (struct amd_hw_event* exp)
{
	amd_simple_exp *s_exp=NULL;
	uint64_t value=0;
	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		amd_readCounter_exp ( s_exp );
		value=s_exp->pmc.msr.new_value;
		s_exp->pmc.msr.new_value=0;
		amd_restartCount_exp ( s_exp );
		break;
	default:
		break;
	}
	return value;
}



/* This function stops the count of a HW event */
static inline void __amd_stop_count_hw_event ( struct amd_hw_event* exp )
{
	amd_simple_exp *s_exp=NULL;
	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		amd_stopCount_exp ( s_exp );
		break;
	default:
		break;

	}

}



/* This function clears the count of a HW event */
static inline void	__amd_clear_count_hw_event ( struct amd_hw_event* exp )
{
	amd_simple_exp *s_exp=NULL;
	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		amd_clear_exp ( s_exp );
		break;
	default:
		break;

	}
}


/* This function saves the context of a HW event (Simply reads the PMC and stores it in new value) */
static inline void __amd_save_context_hw_event (struct amd_hw_event* exp)
{
	amd_simple_exp *s_exp=NULL;
	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		amd_readCounter_exp ( s_exp );
		break;
	default:
		break;
	}
}

static inline uint64_t __amd_save_and_clear_context_hw_event (struct amd_hw_event* exp)
{
	amd_simple_exp *s_exp=NULL;
	uint64_t value=0;
	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		amd_readCounter_exp ( s_exp );
		value=s_exp->pmc.msr.new_value;
		s_exp->pmc.msr.new_value=0;
		amd_stopCount_exp ( s_exp);
		amd_clear_exp (s_exp);
		break;
	default:
		break;
	}
	return value;
}

/* This function saves the context of a HW event */
static inline void __amd_restore_context_hw_event (struct amd_hw_event* exp )
{
	amd_simple_exp *s_exp=NULL;
	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		amd_restoreContext_exp ( s_exp );
		break;
	default:
		break;
	}
}





/* This function reads the value from the HW event's PMC */
static inline void __amd_read_count_hw_event ( struct amd_hw_event* exp )
{
	amd_simple_exp *s_exp=NULL;
	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		amd_readCounter_exp ( s_exp );
		break;
	default:
		break;
	}


}



/* This function returns the last value gathered from the PMC */
static inline uint64_t __amd_get_last_value_hw_event ( struct amd_hw_event* exp )
{
	amd_simple_exp *s_exp=NULL;
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
static inline uint64_t __amd_get_reset_value_hw_event ( struct amd_hw_event* exp )
{
	amd_simple_exp *s_exp=NULL;
	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		return s_exp->pmc.msr.reset_value;
	default:
		break;
	}

	return 0;
}

#endif
