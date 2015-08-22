/*
 *  include/pmc/pmc_bit_layout.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 * 
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef PMC_BIT_LAYOUT_H
#define PMC_BIT_LAYOUT_H


#if defined(CONFIG_PMC_CORE_2_DUO)
#include <pmc/core2duo/pmc_bit_layout.h>
#elif defined(CONFIG_PMC_CORE_I7)
#include <pmc/corei7/pmc_bit_layout.h>
#elif defined(CONFIG_PMC_PENTIUM_4_HT)
#include <pmc/p4ht/pmc_bit_layout.h>
#elif defined(CONFIG_PMC_AMD)
#include <pmc/amd/pmc_bit_layout.h>
#elif defined(CONFIG_PMC_ARM)
#include <pmc/arm/pmc_bit_layout.h>
#elif defined(CONFIG_PMC_ARM64)
#include <pmc/arm64/pmc_bit_layout.h>
#elif defined(CONFIG_PMC_PHI)
#include <pmc/phi/pmc_bit_layout.h>
#else
"There is no monitoring support for current architecture"
#endif

#endif /*PMC_BIT_LAYOUT_H*/
