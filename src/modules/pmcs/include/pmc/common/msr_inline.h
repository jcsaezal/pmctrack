/*
 *  include/pmc/common/msr_inline.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef COMMON_MSR_INLINE_H
#define COMMON_MSR_INLINE_H
#include <asm/msr.h>


/* This function resets an MSR register with an established value (reset value) */
static inline void resetMSR ( _msr_t* handler )
{
	wrmsrl(handler->address,handler->reset_value);
}

/* Set a new value in an MSR register */
static inline void writeMSR ( _msr_t* handler )
{
	wrmsrl(handler->address,handler->new_value);
}

/* Read the current value stored in an MSR register */
static inline void readMSR ( _msr_t* handler )
{
	rdmsrl(handler->address,handler->new_value);
}

/* This function resets a PMC with an established value (reset value) */
static inline void resetPMC ( _pmc_t* handler )
{
	wrmsrl(handler->msr.address,handler->msr.reset_value);
}

/* Read the current value stored in a PMC register */
static inline void readPMC ( _pmc_t* pmc_handler )
{
	pmc_handler->msr.new_value = native_read_pmc(pmc_handler->pmc_address);
}

#endif
