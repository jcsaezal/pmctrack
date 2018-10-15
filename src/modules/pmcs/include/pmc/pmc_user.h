/*
 *  include/pmc/pmc_user.h
 *
 *  Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 *  This code is licensed under the GNU GPL v2.
 */

#ifndef PMC_USER_H
#define PMC_USER_H
#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <sys/types.h>
#include <stdint.h>
#endif

#ifndef MAX_PERFORMANCE_COUNTERS
#define MAX_PERFORMANCE_COUNTERS 11
#endif

#ifndef MAX_VIRTUAL_COUNTERS
#define MAX_VIRTUAL_COUNTERS 3
#endif

/* Available sample types */
typedef enum {
	PMC_TICK_SAMPLE=0,
	PMC_EBS_SAMPLE,
	PMC_EXIT_SAMPLE,
	PMC_MIGRATION_SAMPLE,
	PMC_SELF_SAMPLE,
	PMC_NR_SAMPLE_TYPES
} sample_type_t;

/* Structure to store PMC and virtual-counter values */
typedef struct pmc_sample {
	sample_type_t type;     /* Sample type */
	int coretype;           /* Core type where this sample was registered */
	int exp_idx;            /* Index of the experiment set related to this counter setup */
	pid_t pid;              /* To store a process id (per-thread mode) or CPU (system-wide mode) */
	uint64_t elapsed_time;	/* Reference (from the time the previous sample was gathered) */
	unsigned int pmc_mask;  /* PMC mask for this sample */
	unsigned int nr_counts; /* Number of performance counts associated with this sample */
	uint64_t pmc_counts[MAX_PERFORMANCE_COUNTERS]; /* Raw PMC counts */
	unsigned int virt_mask;  /* Virtual counter mask for this sample */
	unsigned int nr_virt_counts; /* NUmber of virtual counts associated with this sample */
	uint64_t virtual_counts[MAX_VIRTUAL_COUNTERS];	/* Raw virtual-counter values */
} pmc_sample_t;

#endif
