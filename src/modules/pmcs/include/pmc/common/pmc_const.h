/*
 *  include/pmc/common/pmc_const.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 * 
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef PMC_COMMON_PMC_CONST_H
#define PMC_COMMON_PMC_CONST_H

/*This file contains architecture-independent definitions for experiments */
#define ZERO_32           0x00000000
#ifndef BIT_31
#define BIT_31            (0x1<<31)
#endif

/* Maximum number of characters in event IDs */
#define MAX_EXP_ID 60

#endif
