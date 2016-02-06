/*
 *  include/pmc/common/msr.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef  COMMON_MSR_H
#define  COMMON_MSR_H
#include <pmc/common/pmc_types.h>

/**************************************************************************************************************/
/*###############################         MSR LOW-LEVEL OPERATIONS            ###############################*/

/*
 * Data structures for MSRs y PMC's.
 */
typedef struct {
	uint_t address;
	uint64_t reset_value;
	uint64_t new_value;
}
_msr_t;

/* A PMC is an MSR resgister with an additional counter identifier (rdpmc instruction) */
typedef struct {
	uint_t  pmc_address;
	_msr_t msr;
}
_pmc_t;

/* This function resets an MSR register with an established value (reset value) */
static inline void resetMSR ( _msr_t* handler );

/* Set a new value in an MSR register */
static inline void writeMSR ( _msr_t* handler );

/* Read the current value stored in an MSR register */
static inline void readMSR ( _msr_t* handler );

/* This function resets a PMC with an established value (reset value) */
static inline void resetPMC ( _pmc_t* handler );

/* Read the current value stored in a PMC register */
static inline void readPMC ( _pmc_t* pmc_handler );


/* Include inline implementation */
#include <pmc/common/msr_inline.h>


#endif


