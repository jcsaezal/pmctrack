/*
 *  include/pmc/arm/hw_events.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 * 
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef ARM_HW_EVENTS_H
#define ARM_HW_EVENTS_H


#include <pmc/arm/pmc_const.h>
#include <pmc/arm/pmc_bit_layout.h>
#include <pmc/common/pmc_types.h>

#ifndef NULL
#define NULL 0
#endif

typedef struct {
	uint_t counter_idx;
	uint_t reset_value;
	uint_t new_value;
}
_pmu_reg_t;





/* Different configuration modes supported by the backend of the monitoring tool*/
typedef enum {_SIMPLE=0, _FIXED ,_NUM_EVENT_TYPES} ll_event_type;


/******************************************************************************/
/********************************** SIMPLE EVENTS *****************************/
/******************************************************************************/

/* Configurable events */
typedef struct {
	_pmu_reg_t pmc;		/* PMC involved into the count */
	_pmu_reg_t evtsel;	/* Event selection and count control register */
}
simple_exp;

/* Initialization */
static inline void init_simple_exp (	simple_exp* se,
                                        uint_t pmc_idx,
                                        uint_t evtsel_value,
                                        uint_t reset_value_pmc);


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
	_pmu_reg_t pmc;	/* PMC involved into the count (note that the counter is fixed) */
}
fixed_count_exp;


/* Initialization */
static inline void init_fixed_count_exp ( fixed_count_exp* se,
        uint_t pmc_idx,
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

/* Load implementation */
#include <pmc/arm/hw_events_inline.h>

#endif
