/*
 * pmctrack.h
 *
 ******************************************************************************
 *
 * Copyright (c) 2015 Juan Carlos Saez <jcsaezal@ucm.es>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 ******************************************************************************
 *
 */

#ifndef PMCTRACK_H
#define PMCTRACK_H
#include <pmc_user.h> /* Note: This file is found in PMCTrack's kernel module source code */
#include <stdio.h>

#ifndef  _GNU_SOURCE
#define _GNU_SOURCE
#endif

#define MAX_COUNTER_CONFIGS 5  /* Max number of event multiplexing experiments */

/* Forward-declare opaque descriptor */
struct pmctrack_desc;
typedef struct pmctrack_desc pmctrack_desc_t;

/* Basic structure to store event-to-counter mapping information */
typedef struct counter_mapping {
	int nr_counter;
	char* events[MAX_COUNTER_CONFIGS];
	unsigned int experiment_mask;
} counter_mapping_t;

/*
 * Initialize and return a PMCTrack descriptor after establishing a "connection" with
 * the kernel module.
 * A max number of samples must be passed to the function to allocate a buffer
 * to store PMC and virtual-counter samples. If zero is passed, a page-sized shared-memory
 * region with the kernel will be used to store the samples. (Due to efficiency reasons,
 * this is the preferred option if few samples are generated)
 *
 * On error, pmctrack_init() returns NULL.
 *
 */
pmctrack_desc_t* pmctrack_init(unsigned int max_nr_samples);

/* Free up descriptor */
int pmctrack_destroy(pmctrack_desc_t* desc);

/*
 * This function makes it possible to create a copy of a descriptor
 * for another thread in the application. It is more efficient to clone
 * a descriptor than creating a new one with pmctrack_init()
 */
pmctrack_desc_t* pmctrack_clone_descriptor(pmctrack_desc_t* orig);

/*
 * Tell PMCTrack's kernel module the desired PMC and virtual counter
 * configuration. The configuration must be specified using the raw format
 * (the only one that the kernel "understands") for both PMC and virtual counters.
 *
 * ==Parameters==
 * desc: PMCTrack descriptor
 * strcfg: NULL-terminated array of strings with counter configurations in the raw format
 * 			(e.g., {"pmc0,pmc1","pmc2=0xc0,pmc3=0x2e","NULL"})
 * virtcfg: virtual counter string configuration in the raw format (e.g., virt0,virt1)
 * mux_timeout_ms: Sampling period for TBS. If 0 is used, only one sample will be generated.
 *				   If EBS is used, mux_timeout_ms has no effect.
 *
 * The function returns 0 on success, and a non-zero value upon failure.
 */
int pmctrack_config_counters(pmctrack_desc_t* desc, const char* strcfg[],
                             const char* virtcfg, int mux_timeout_ms);

/*
 * Tell PMCTrack's kernel module the desired PMC and virtual counter
 * configuration. The configuration must be specified using the default format
 * used by the pmctrack command-line tool, which accepts event mnemonics for both
 * PMC and virtual counters.
 *
 * ==Parameters==
 * desc: PMCTrack descriptor
 * strcfg: NULL-terminated array of strings with counter configurations
 * virtcfg: virtual counter string configuration
 * mux_timeout_ms: Sampling period for TBS. If 0 is used, only one sample will be generated.
 *				   If EBS is used, mux_timeout_ms has no effect.
 *
 * The function returns 0 on success, and a non-zero value upon failure.
 */
int pmctrack_config_counters_mnemonic(pmctrack_desc_t* desc, const char* strcfg[],
                                      const char* virtcfg, int mux_timeout_ms,
                                      int pmu_id);

/*
 * Start a monitoring session in per-thread mode. Note that PMC and/or
 * virtual counter configurations must have been specified beforehand.
 *
 * The function returns 0 on success, and a non-zero value upon failure.
 */
int pmctrack_start_counters(pmctrack_desc_t* desc);

/*
 * Stops a monitoring session in per-thread mode. This function also
 * retrieves PMC and virtual counter samples collected by the kernel.
 * Samples are stored in the PMCTrack descriptor, and can be accessed
 * after invoking this function.
 *
 * The function returns 0 on success, and a non-zero value upon failure.
 */
int pmctrack_stop_counters(pmctrack_desc_t* desc);

/*
 * Start a monitoring session in system-wide mode. Note that PMC and/or
 * virtual counter configurations must have been specified beforehand.
 *
 * The function returns 0 on success, and a non-zero value upon failure.
 */
int pmctrack_start_counters_syswide(pmctrack_desc_t* desc);

/*
 * Stops a monitoring session in system-wide mode. This function also
 * retrieves PMC and virtual counter samples collected by the kernel.
 * Samples are stored in the PMCTrack descriptor, and can be accessed
 * after invoking this function.
 *
 * The function returns 0 on success, and a non-zero value upon failure.
 */
int pmctrack_stop_counters_syswide(pmctrack_desc_t* desc);

/*
 * Prints a summary with all the monitoring information collected
 * in the last per-thread or system-wide monitoring session.
 *
 * ==Parameters==
 * desc: PMCTrack descriptor
 * outfile: stdio descriptor of the file where the messages will be dumped
 * extended_output: If using the event-multiplexing capability (more than one event set)
 * the extended output parameter must be set to one for a correct formatting
 * of the generated output. Otherwise, stick to the regular format (extended_output=0).
 *
 */
void pmctrack_print_counts(pmctrack_desc_t* desc, FILE* outfile, int extended_output);

/*
 * Returns an array of samples collected after stopping a monitoring session.
 * The number of samples in the array is stored in the nr_samples output parameter.
 */
pmc_sample_t* pmctrack_get_samples(pmctrack_desc_t* desc,int* nr_samples);

/*
 * Retrieve the event-to-PMC mapping information. The function should be used
 * only if pmctrack_config_counters_mnemonic() was used to configure performance
 * counters.
 *
 * ==Parameters==
 * desc (input): PMCTrack descriptor
 * mapping (output): Array where libpmctrack stores mapping information. The array
 * 					must have MAX_PERFORMANCE_COUNTERS elements and its memory must
 *					be allocated by the program.
 * nr_experiments (output): Number of event sets (multiplexing experiments) used
 * used_pmcmask (output): Bitmask of performance counters used
 *
 * The function returns 0 on success, and a non-zero value upon failure.
 */
int pmctrack_get_event_mapping( pmctrack_desc_t* desc,
                                counter_mapping_t* mapping,
                                unsigned int* nr_experiments,
                                unsigned int* used_pmcmask);

#endif
