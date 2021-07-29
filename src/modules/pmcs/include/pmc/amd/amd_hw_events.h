/*
 *  include/pmc/amd/amd_hw_events.h
 *
 *  Copyright (c) 2021 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef AMD_HW_EVENTS_H
#define AMD_HW_EVENTS_H

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
amd_simple_exp;

/* Initialization */
static inline void amd_init_simple_exp (	amd_simple_exp* se,
        uint_t pmc_msr_adress,
        uint_t pmc_adress,
        uint_t evtsel_adress,
        uint64_t evtsel_value,
        uint64_t reset_value_pmc);

/* This function starts the count of a simple event */
static inline void amd_startCount_exp ( amd_simple_exp * exp );

/* This function starts the count of a simple event */
static inline void amd_restartCount_exp ( amd_simple_exp * exp );

/* This function stops the count of a simple event */
static inline void amd_stopCount_exp ( amd_simple_exp * exp );

/* This function reads the value from the simple event's PMC */
static inline void amd_readCounter_exp ( amd_simple_exp * exp );

/* This function clears the count of a simple event */
static inline void amd_clear_exp ( amd_simple_exp * exp );

/* Restore the context of pmc */
static inline void amd_restoreContext_exp ( amd_simple_exp * exp );


/********************************************************************************/
/******************* PROCESSOR-SPECIFIC DEFINITION OF HW_EVENTS *****************/
/********************************************************************************/

typedef struct amd_hw_event {
	union {
		amd_simple_exp s_exp;
	} g_event;
	/*Event Type*/
	ll_event_type type;
	uint64_t count; /* value to accumulate counts across context switches*/
} amd_hw_event_t;


/* An additional initialization function is provided here */
static inline   void amd_init_hw_event ( struct amd_hw_event* exp, ll_event_type type );


/* Load implementation */
#include <pmc/amd/amd_hw_events_inline.h>

#endif
