/*
 *  include/pmc/common/pmc_types.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef PMC_COMMON_PMC_TYPES_H
#define PMC_COMMON_PMC_TYPES_H


#if defined(_DEBUG_USER_MODE) && defined (__linux__)

typedef unsigned int _uint32_t;
typedef unsigned int uint32_t;
typedef unsigned int uint_t;
typedef unsigned long long uint64;
typedef unsigned int uint32;
typedef unsigned char uint8;
typedef unsigned long long uint64_t;
typedef struct {
	uint32_t low;
	uint32_t high;
}
_uint64_t;

#elif defined (__linux__)
#include <linux/types.h>
typedef unsigned int uint_t;


#elif  defined(_DEBUG_USER_MODE) && defined(__SOLARIS__)
#include <sys/types.h>
#else
#include <sys/types.h>
#endif


#endif
