/*
 *  include/pmc/amd/hw_events.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 * 
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef AMD_HW_EVENTS_H
#define AMD_HW_EVENTS_H


#include <pmc/amd/pmc_const.h>
#include <pmc/common/msr.h>
#include <pmc/common/pmc_types.h>

#ifndef NULL
#define NULL 0
#endif

typedef enum {_SIMPLE=0,_NUM_EVENT_TYPES} ll_event_type;

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


/********************************************************************************/
/******************* PROCESSOR-SPECIFIC DEFINITION OF HW_EVENTS *****************/
/********************************************************************************/

struct hw_event {
	union {
		simple_exp s_exp;
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
#include <pmc/amd/hw_events_inline.h>

#endif
