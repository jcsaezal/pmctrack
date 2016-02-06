/*
 *  include/pmc/arm/pmc_const.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef SYS_PMC_ARM_PMC_CONST_H
#define SYS_PMC_ARM_PMC_CONST_H

#define MAX_HL_EXPS 20

/* 6 PMCs (max in the Cortex A15) + 1 FIXED COUNTER */
#define MAX_LL_EXPS 7

/* Exynos octa core, and odroid boards have as much as 8 cores */
#define NR_LOGICAL_PROCESSORS 8

/* Disable user and OS flags on reset */
#define EVTSEL_RESET_VALUE ((1<<31)|(1<<30))


#endif
