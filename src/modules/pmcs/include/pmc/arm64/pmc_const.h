/*
 *  include/pmc/arm64/pmc_const.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef ARM64_PMC_CONST_H
#define ARM64_PMC_CONST_H

#define MAX_HL_EXPS 20

/*
 * 6 GP PMCs (in the Cortex A53 and Cortex A57 cores)
 * + 1 fixed-function (cycle) counter
 */
#define MAX_LL_EXPS 7

/* Future 64-bit octa cores ... */
#define NR_LOGICAL_PROCESSORS 8

/* Disable user and OS flags on reset */
#define EVTSEL_RESET_VALUE ((1<<31)|(1<<30))

#endif
