/*
 *  include/pmc/ll_events.h
 *
 *	Data types and functions to deal with low-level events (HW event counts)
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef PMC_LL_EVENTS_H
#define PMC_LL_EVENTS_H
#include <pmc/common/pmc_const.h>
#include <pmc/common/pmc_types.h>

/*
 * HW event forward declaration.
 * A definition of this structure must be provided for
 * every supported architecture
 */
struct hw_event;
typedef struct hw_event hw_event_t;


/*************** Available operations for hw_events  ***************************/

/* This function starts the count of a HW event (from its previous value) */
static inline   void __start_count_hw_event ( struct hw_event* exp );

/* This function starts the count of a HW event (from 0) */
static inline   void __restart_count_hw_event ( struct hw_event* exp );

/* This function stops the count of a HW event */
static inline   void __stop_count_hw_event ( struct hw_event* exp );

/* This function clears the count of a HW event */
static inline   void __clear_count_hw_event ( struct hw_event* exp );

/* This function reads the value from the HW event's PMC */
static inline   void __read_count_hw_event (struct hw_event* exp );

/* This function returns the last value gathered from the PMC */
static inline uint64_t __get_last_value_hw_event (struct hw_event* exp );

/* This function saves the context of a HW event */
static inline void __save_context_hw_event (struct hw_event* exp);

/* This function saves the context of a HW event */
static inline void __restore_context_hw_event (struct hw_event* exp );

/* This function returns PMCs reset value */
static inline uint64_t __get_reset_value_hw_event (struct hw_event* exp );


/** This code loads architecture-specific definition and implementation of a hw_event **/

#if defined(CONFIG_PMC_CORE_2_DUO)
#include <pmc/core2duo/hw_events.h>
#include <pmc/core2duo/pmc_bit_layout.h>
#include <pmc/core2duo/pmc_const.h>
#elif defined(CONFIG_PMC_CORE_I7)
#include <pmc/corei7/hw_events.h>
#include <pmc/corei7/pmc_bit_layout.h>
#include <pmc/corei7/pmc_const.h>
#elif defined(CONFIG_PMC_PENTIUM_4_HT)
#include <pmc/p4ht/hw_events.h>
#elif defined(CONFIG_PMC_AMD)
#include <pmc/amd/hw_events.h>
#include <pmc/amd/pmc_bit_layout.h>
#include <pmc/amd/pmc_const.h>
#elif defined(CONFIG_PMC_ARM)
#include <pmc/arm/hw_events.h>
#include <pmc/arm/pmc_bit_layout.h>
#include <pmc/arm/pmc_const.h>
#elif defined(CONFIG_PMC_ARM64)
#include <pmc/arm64/hw_events.h>
#include <pmc/arm64/pmc_bit_layout.h>
#include <pmc/arm64/pmc_const.h>
#elif defined(CONFIG_PMC_PHI)
#include <pmc/phi/hw_events.h>
#include <pmc/phi/pmc_bit_layout.h>
#include <pmc/phi/pmc_const.h>
#else
"There is no monitoring support for current architecture"
#endif


/* A low level event is a HW event with an additional identifier*/
typedef struct {
	char id[MAX_EXP_ID]; 	/*Event ID*/
	struct hw_event event;
	unsigned int pmc_id;	/* Performance counter id associated with this event */
}
low_level_exp;


/*************** Available operations for low_level_events  ***************************/
static inline void init_low_level_exp ( low_level_exp* exp,const char *name)
{
	strcpy ( exp->id,name );
	exp->pmc_id=0;
}


static inline void init_low_level_exp_id ( low_level_exp* exp,const char *name, unsigned int pmc_id)
{
	strcpy ( exp->id,name );
	exp->pmc_id=pmc_id;
}


/* Wrapper operations for low_level_exps */
#define __start_count(p_exp)		__start_count_hw_event(&((p_exp)->event))
#define __restart_count(p_exp)		__restart_count_hw_event(&((p_exp)->event))
#define __stop_count(p_exp)  		__stop_count_hw_event(&((p_exp)->event))
#define __clear_count(p_exp)  		__clear_count_hw_event(&((p_exp)->event))
#define __read_count(p_exp)   		__read_count_hw_event(&((p_exp)->event))
#define __get_last_value(p_exp) 	__get_last_value_hw_event(&((p_exp)->event))
#define __save_context_event(p_exp)   	__save_context_hw_event(&((p_exp)->event))
#define __restore_context_event(p_exp) 	__restore_context_hw_event(&((p_exp)->event))
#define __get_reset_value(p_exp) 	__get_reset_value_hw_event(&((p_exp)->event))


#endif
