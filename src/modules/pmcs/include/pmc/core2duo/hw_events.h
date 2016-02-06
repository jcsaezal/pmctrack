/*
 *  include/pmc/core2duo/hw_events.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */


#ifndef CORE2DUO_HW_EVENTS_H
#define CORE2DUO_HW_EVENTS_H


#include <pmc/core2duo/pmc_const.h>
#include <pmc/common/msr.h>
#include <pmc/common/pmc_types.h>

#ifndef NULL
#define NULL 0
#endif


/* Different configuration modes supported by the backend of the monitoring tool*/
typedef enum {_SIMPLE=0, _TAGGING,_FIXED ,_NUM_EVENT_TYPES} ll_event_type;


/******************************************************************************/
/********************************** SIMPLE EVENTS *****************************/
/******************************************************************************/
/* Configurable events */
typedef struct {
	_pmc_t pmc;	/* PMC involved into the count */
	_msr_t evtsel;	/* Event selection and count control register */
}
simple_exp;

/* Initialization */
static inline void init_simple_exp (	simple_exp* se,
                                        uint_t pmc_msr_adress,
                                        uint_t pmc_adress,
                                        uint_t evtsel_adress,
                                        uint64_t evtsel_value,
                                        uint64_t reset_value_pmc);


/* This function starts the count of a simple event */
static inline void startCount_exp ( simple_exp * exp );

/* This function starts the count of a simple event */
static inline void restartCount_exp ( simple_exp * exp );

/* This function stops the count of a simple event */
static inline void stopCount_exp ( simple_exp * exp );

/* This function reads the value from the simple event's PMC */
static inline void readCounter_exp ( simple_exp * exp );

/* This function clears the count of a simple event */
static inline void clear_exp ( simple_exp * exp );

/* Restore the context of pmc */
static inline void restoreContext_exp ( simple_exp * exp );


/******************************************************************************/
/********************************** FIXED EVENTS ******************************/
/******************************************************************************/
typedef struct {
	_msr_t pmc;	/* PMC involved into the count */
	_msr_t evtsel; /* Event selection and count control register
				Constant address => MSR_PERF_FIXED_CTR_CTRL
			*/
}
fixed_count_exp;


/* Initialization */
static inline void init_fixed_count_exp ( fixed_count_exp* se,
        uint_t pmc_msr_adress,	/* There are only three allowed
					   values for fixed-count counters
					*/
        uint64_t evtsel_value, /* Mask value (must be similar to the other
			 			init_fixed_count_exp)
					*/

        uint64_t reset_value_pmc	/* Set reset value needed for EBS */
                                        );

/* This function starts the count of a fixed-count event */
static inline void startCount_fixed_exp ( fixed_count_exp * exp );

/* This function starts the count of a fixed-count event */
static inline void restartCount_fixed_exp ( fixed_count_exp * exp );

/* This function stops the count of a fixed-count event */
static inline void stopCount_fixed_exp ( fixed_count_exp * exp );

/* This function clears the count of a fixed-count event */
static inline void clear_fixed_exp ( fixed_count_exp * exp );

/* This function reads the value from the fixed-count event's PMC */
static inline void readCounter_fixed_exp ( fixed_count_exp * exp );

/* Restore the context of pmc */
static inline void restoreContext_fixed_exp ( fixed_count_exp * exp );

/********************************************************************************/
/******************* PROCESSOR-SPECIFIC DEFINITION OF HW_EVENTS *****************/
/********************************************************************************/

struct hw_event {
	union {
		simple_exp s_exp;
		fixed_count_exp f_exp;
	} g_event;
	/*Event Type*/
	ll_event_type type;
};


/* An additional initialization function is provided here */
static inline   void init_hw_event ( struct hw_event* exp, ll_event_type type );


#ifdef _DEBUG_USER_MODE
/* Debug functions for accessing to PMCs from User-mode */
#include <pmc_functions_debug.h>
#endif


/* Load implementation */
#include <pmc/core2duo/hw_events_inline.h>

#endif
