/*
 *  include/pmc/arm64/hw_events_inline.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef ARM64_HW_EVENTS_INLINE_H
#define ARM64_HW_EVENTS_INLINE_H

#include <linux/kernel.h>
#include <linux/string.h>

/*******************************************************************************
*******************************************************************************/


/* Initialization */
static inline void init_simple_exp (	simple_exp* se,
                                        uint_t pmc_idx,
                                        uint_t evtsel_value,
                                        uint_t reset_value_pmc)
{
	se->pmc.counter_idx=pmc_idx;
	se->pmc.reset_value=reset_value_pmc;
	se->pmc.new_value=reset_value_pmc;

	se->evtsel.counter_idx=pmc_idx;
	se->evtsel.reset_value=EVTSEL_RESET_VALUE;
	se->evtsel.new_value=evtsel_value;

}



/* This function starts the count of a simple event */
static   inline void startCount_exp ( simple_exp * exp )
{
	armv8pmu_write_counter(exp->pmc.counter_idx,exp->pmc.new_value); /* The counter is set to its previous value (context saved) */
	armv8pmu_write_evtype(exp->evtsel.counter_idx,exp->evtsel.new_value);	/* Evtsel Configuration */
	armv8pmu_enable_counter(exp->evtsel.counter_idx);
}

static inline void restartCount_exp ( simple_exp * exp )
{
	armv8pmu_disable_counter(exp->evtsel.counter_idx);
	armv8pmu_write_counter(exp->pmc.counter_idx,exp->pmc.reset_value); 	/* The counter is cleared */
	armv8pmu_write_evtype(exp->evtsel.counter_idx,exp->evtsel.new_value);	/* Evtsel Configuration */
	armv8pmu_enable_counter(exp->evtsel.counter_idx);
}

/* This function stops the count of a simple event */
static  inline  void stopCount_exp ( simple_exp * exp )
{
	armv8pmu_disable_counter(exp->evtsel.counter_idx);	/* Clear the enable bit */
}

/* This function clears the count of a simple event */
static inline void clear_exp ( simple_exp * exp )
{
	armv8pmu_write_counter(exp->pmc.counter_idx,0);	/* The counter is set to zero */
}



/* This function reads the value from the simple event's PMC */
static inline void readCounter_exp ( simple_exp * exp )
{
	exp->pmc.new_value=armv8pmu_read_counter( exp->pmc.counter_idx );
}

/* Restore the context of pmc */
static inline void restoreContext_exp ( simple_exp * exp )
{
	restartCount_exp(exp);
}


/**************************************************************************************************************/

/* Initialization */
static inline void init_fixed_count_exp ( fixed_count_exp* fe,
        uint_t pmc_idx,
        uint64_t reset_value_pmc	/* Set reset value needed for EBS */
                                        )
{
	fe->pmc.counter_idx=pmc_idx;
	fe->pmc.reset_value=reset_value_pmc;
	fe->pmc.new_value=reset_value_pmc;
}

/* This function starts the count of a fixed-count event */
static inline void startCount_fixed_exp ( fixed_count_exp * exp )
{
	armv8pmu_write_counter(exp->pmc.counter_idx,exp->pmc.new_value); /* The counter is set to its previous value (context saved) */
	armv8pmu_enable_counter(exp->pmc.counter_idx);
}

/* This function starts the count of a fixed-count event */
static inline void restartCount_fixed_exp ( fixed_count_exp * exp )
{
	armv8pmu_disable_counter(exp->pmc.counter_idx);
	armv8pmu_write_counter(exp->pmc.counter_idx,exp->pmc.reset_value); /* The counter is set to its previous value (context saved) */
	armv8pmu_enable_counter(exp->pmc.counter_idx);
}

/* This function stops the count of a fixed-count event */
static inline void stopCount_fixed_exp ( fixed_count_exp * exp )
{
	armv8pmu_disable_counter(exp->pmc.counter_idx);	/* Clear the enable bit */
}

/* This function clears the count of a fixed-count event */
static inline void clear_fixed_exp ( fixed_count_exp * exp )
{
	armv8pmu_write_counter(exp->pmc.counter_idx,0);	/* The counter is set to zero */
}

/* This function reads the value from the fixed-count event's PMC */
static inline void readCounter_fixed_exp ( fixed_count_exp * exp )
{
	exp->pmc.new_value = armv8pmu_read_counter( exp->pmc.counter_idx );
}


/* Restore the context of pmc by writing new value on it */
static inline void restoreContext_fixed_exp ( fixed_count_exp * exp )
{
	restartCount_fixed_exp(exp);
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

	u32 state;
	state = armv8pmu_pmcr_read();
	if( ! (state & ARMV8_PMCR_E)  ) {
		printk(KERN_INFO "    * Setting ARMV8_PMCR_E bit (start) \n");
		armv8pmu_pmcr_write(state | ARMV8_PMCR_E);
	}

	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		startCount_exp ( s_exp );
		break;
	case _FIXED:
		startCount_fixed_exp ( & ( exp->g_event.f_exp ) );
		break;
	default:
		break;

	}


}

/* This function starts the count of a HW event */
static inline void  __restart_count_hw_event ( struct hw_event* exp )
{
	simple_exp *s_exp=NULL;

	u32 state;
	state = armv8pmu_pmcr_read();
	if( ! (state & ARMV8_PMCR_E) ) {
		printk(KERN_INFO "    * Setting ARMV8_PMCR_E bit (start) \n");
		armv8pmu_pmcr_write(state | ARMV8_PMCR_E );
	}

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
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		stopCount_exp ( s_exp );
		break;
	case _FIXED:
		stopCount_fixed_exp ( & ( exp->g_event.f_exp ) );
		break;
	default:
		break;
	}

}



/* This function clears the count of a HW event */
static inline void __clear_count_hw_event ( struct hw_event* exp )
{
	simple_exp *s_exp=NULL;
	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		clear_exp ( s_exp );
		break;

	case _FIXED:
		clear_fixed_exp ( & ( exp->g_event.f_exp ) );
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



/* This function returns the last value gathered from the PMC */
static inline uint64_t __get_last_value_hw_event ( struct hw_event* exp )
{
	simple_exp *s_exp=NULL;
	switch ( exp->type ) {
	case _SIMPLE:
		s_exp=& ( exp->g_event.s_exp );
		return s_exp->pmc.new_value;
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
		return s_exp->pmc.reset_value;
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
		s_exp->pmc.reset_value=reset_val;
		break;
	case _FIXED:
		exp->g_event.f_exp.pmc.reset_value=reset_val;
		break;
	default:
		break;
	}
}

#endif
