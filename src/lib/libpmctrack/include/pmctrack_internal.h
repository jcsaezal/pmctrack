/*
 * pmctrack_internal.h
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
 *  2015-05-10  Modified by Abel Serrano to move the code of low-level routines
 *              defined previously in pmctrack.c (command-line tool). This was a major
 *              code refactoring operation.
 *  2015-08-10  Modified by Juan Carlos Saez to include support for mnemonic-based
 *              event configurations and system-wide monitoring mode
 */

#ifndef PMCTRACK_INTERNAL_H
#define PMCTRACK_INTERNAL_H
#include <pmctrack.h>


/* Max length for user-provided PMC configuration strings */
#define MAX_CONFIG_STRING_SIZE 150
#define MAX_RAW_COUNTER_CONFIGS_SAFE (2*MAX_COUNTER_CONFIGS)

/* Various flag values for the "flags" field in struct pmctrack_desc */
#define PMCT_FLAG_SHARED_REGION 0x1
#define PMCT_FLAG_SYSWIDE 0x2
#define PMCT_FLAG_RAW_PMC_CONFIG 0x4
#define PMCT_FLAG_VIRT_COUNTER_MNEMONICS 0x8

/* 
 * Structure to manage a performance 
 * monitoring session with PMCTrack's kernel driver 
 */
struct pmctrack_desc {
	int fd_monitor;                    /* Descriptor for the special file enabling to read performance samples from the kernel */
	pmc_sample_t* samples;             /* Buffer to store PMC and virtual counter values */
	unsigned int pmcmask;              /* User PMC mask */
	unsigned int kern_pmcmask;         /* Kernel (forced) PMC mask */
	unsigned int nr_pmcs;              /* Number of performance counters in use */
	unsigned int nr_virtual_counters;  /* Number of virtual counters in use */
	unsigned int virtual_mask;         /* User Virtual-counter mask */
	unsigned int nr_experiments;       /* Number of PMC multiplexing experiments */
	unsigned int ebs_on;               /* Indicates if the Event-Based Sampling mode is enabled */
	unsigned int nr_samples;           /* Number of items temporarily stored in the "samples" array */
	unsigned int max_nr_samples;       /* Max capacity (# of samples) of the "samples" array */
	counter_mapping_t event_mapping[MAX_PERFORMANCE_COUNTERS]; /* Structure storing the event-to-PMC mapping */
	unsigned int global_pmcmask;       /* Overall PMC mask used (when using event mnemonics only) */    
	unsigned long flags;               /* Bitmask field (libpmctrack-specific flags) */
};

/*
 * The function parses a null-terminated array of strings 
 * with PMC configurations in the raw format (userpmccfg), and 
 * returns the following information: (1) Number of PMCs in use
 * across experiments in the specified configuration, (2) bitmask that indicates
 * which PMCs are used across experiments, (3) flag (ebs) that indicates
 * if the EBS mode is requested in any of the configuration strings and
 * (4) number of experiments detected (# items in the userpmccfg vector)
 *
 * The function returns 0 on success, and a non-zero value upon failure.
 */
int pmct_check_counter_config(const char* userpmccfg[],unsigned int* nr_counters,
                              unsigned int* counter_mask,
                              unsigned int* ebs,
                              unsigned int* nr_experiments);

/*
 * Parse a string that specifies the virtual-counter configuration
 * in the raw format and return the following information: 
 * (1) Number of virtual counters specified in the string and 
 * (2) bitmask that indicates which virtual counters are used
 *
 * The function returns 0 on success, and a non-zero value upon failure.
 */
int pmct_check_vcounter_config(const char* virtcfg,unsigned int* nr_virtual_counters,unsigned int* virtual_mask);

/* 
 * Tell PMCTrack's kernel module which PMC events must be monitored.
 *
 * ==Parameters==
 * strcfg: NULL-terminated array of string with counter configurations in the raw format
 *      (e.g., {"pmc0,pmc1","pmc2=0xc0,pmc3=0x2e","NULL"})
 * syswide: 1 -> enable syswide mode, 0 -> enable per-thread mode.
 *
 * The function returns 0 on success, and a non-zero value upon failure.
 */
int pmct_config_counters(const char* strcfg[], int syswide);

/* 
 * Tell PMCTrack's kernel module which virtual counters must be monitored.
 *
 * ==Parameters==
 * virtcfg: virtual counter configuration string in the raw format (e.g., virt0,virt1)
 * syswide: 1 -> enable syswide mode, 0 -> enable per-thread mode.
 *
 * The function returns 0 on success, and a non-zero value upon failure.
 */
int pmct_config_virtual_counters(const char* virtcfg, int syswide);

/* 
 * Setup timeout for TBS or scheduler-driven monitoring mode (specified in ms).
 * If the kernel forced the PMC configuration a non-zero value should be specified
 * as the "kernel_control" parameter
 *
 * The function returns 0 on success, and a non-zero value upon failure.
 *
 */
int pmct_config_timeout(int msecs, int kernel_control);

/*
 * Tell PMCTrack's kernel module to start a monitoring session in per-thread mode 
 *
 * The function returns 0 on success, and a non-zero value upon failure.
 */
int pmct_start_counting( void );

/* 
 * Print a header in the "normalized" format for a table of 
 * PMC and virtual-counter samples
 *
 * ==Parameters==
 * nr_experiments: Number of event-multiplexing experiments in use
 * pmcmask: bitmask indicating which PMCs are in use
 * virtual_mask: bitmask indicating which virtual counters are in use
 * extended_output: Use a non-zero value if nr_experiments>1 or different
 *                  events sets are monitored in the various cores of 
 *                  an asymmetric multicore system
 * syswide: Use a non-zero value if the system-wide mode is enabled.
 *
 */
void pmct_print_header (FILE* fo, unsigned int nr_experiments,
                        unsigned int pmcmask,
                        unsigned int virtual_mask,
                        int extended_output,
                        int syswide);

/* 
 * Print a sample row in the "normalized" format for a table of 
 * PMC and virtual-counter samples
 *
 * ==Parameters==
 * nr_experiments: Number of event-multiplexing experiments in use
 * pmcmask: bitmask indicating which PMCs are in use
 * virtual_mask: bitmask indicating which virtual counters are in use
 * extended_output: Use a non-zero value if nr_experiments>1 or different
 *                  events sets are monitored in the various cores of 
 *                  an asymmetric multicore system
 * nsample: Number of sample to be included in the row
 * sample: Actual sample with PMC and virtual-counter data
 *
 */
void pmct_print_sample (FILE* fo, unsigned int nr_experiments,
                        unsigned int pmcmask,
                        unsigned int virtual_mask,
                        unsigned int extended_output,
                        int nsample,
                        pmc_sample_t* sample);

/* 
 * Accumulate PMC and virtual-counter values from one sample into 
 * another sample.
 * (This function is used to implement the "-A" option 
 * of the pmctrack command-line tool)
 *
 * ==Parameters==
 * nr_experiments: Number of event-multiplexing experiments in use
 * pmcmask: bitmask indicating which PMCs are in use
 * virtual_mask: bitmask indicating which virtual counters are in use
 * copy_metainfo: Use a non-zero value to copy metainfo from "sample" 
 *                 into "accum"
 * sample: Actual sample with PMC and virtual-counter data
 * sample: Sample with accumulate data
 */
void pmct_accumulate_sample (unsigned int nr_experiments,
                             unsigned int pmcmask,
                             unsigned int virtual_mask,
                             unsigned char copy_metainfo,
                             pmc_sample_t* sample,
                             pmc_sample_t* accum);

/* 
 * Become the monitor process of another process with PID=pid.
 * Upon invocation to this function the monitor process will
 * be able to retrieve PMC and/or virtual-counter samples
 * using a special file exported by PMCTrack's kernel module
 *
 * If config_pmcs !=0, the attached process will inherit PMC and
 * virtual counter configuration from the parent process
 *
 * The function returns 0 on success, and a non-zero value upon failure.
 * 
 */
int pmct_attach_process (pid_t pid, int config_pmcs);

/*
 * Detach process from monitor 
 */
int pmct_detach_process (pid_t pid);


/*
 * Obtain a file descriptor of the special file exported by
 * PMCTrack's kernel module file to retrieve performance samples 
 *
 * The function returns a number greater or equal than zero upon success, 
 * and a negative value upon failure.
 *
 */
int pmct_open_monitor_entry(void);

/*
 * Retrieve performance samples from the special file exported by
 * PMCTrack's kernel module
 *
 * ==Parameters==
 * fd: File descriptor obtained with pmct_open_monitor_entry()
 * samples: Array used to store the retrieved samples
 * max_samples: Maximum capacity of the "samples" array
 *
 * The function returns 0 on success, and a non-zero value upon failure.
 *
 */
int pmct_read_samples (int fd, pmc_sample_t* samples, int max_samples);

/*
 * Request a memory region shared between kernel and user space to
 * enable efficient communication between the monitor process and 
 * PMCTrack's kernel module when retrieving performance samples.
 *
 * ==Parameters==
 * monitor_fd: File descriptor obtained with pmct_open_monitor_entry()
 * max_samples (output): Maximum capacity (in # of samples) of the shared memory region
 *
 * The function returns a non-NULL pointer on success, and NULL upon failure. 
 */
pmc_sample_t* pmct_request_shared_memory_region(int monitor_fd, unsigned int* max_samples);

/*
 * Set up the size of the kernel buffer used to store PMC and virtual 
 * counter values 
 *
 * The function returns 0 on success, and a non-zero value upon failure.
 */
int pmct_set_kernel_buffer_size(unsigned int nr_bytes);

/*
 * Tell PMCTrack's kernel module to start a monitoring session in system-wide mode 
 *
 * The function returns 0 on success, and a non-zero value upon failure.
 */
int pmct_syswide_start_counting( void );

/* PMCTrack PMU_INFO structures plus event mnemonic translation engine */

#define MAX_CORE_TYPES 2
#define NAME_HW_EVENT_SIZE 50
#define CODE_HW_EVENT_SIZE 10
#define NAME_KEY_PMC_PROPERTY_SIZE 30
#define VALUE_PMC_PROPERTY_SIZE 50
#define MAX_NR_PROPERTIES 10
#define MAX_NR_SUBEVENTS 30
#define MAX_NR_EVENTS 50
#define NAME_MODEL_SIZE 50
#define FILE_LINE_SIZE 100
#define PATH_CSV_SIZE 100
#define MAX_NR_EVENTS_CFG 30
#define ROW_CFG_SIZE 150

/* A key-value pair */
typedef struct {
	char key[NAME_KEY_PMC_PROPERTY_SIZE];
	char value[VALUE_PMC_PROPERTY_SIZE];
} pmc_property_t;

/* Structure that represents a HW subevent */
typedef struct {
	char name[NAME_HW_EVENT_SIZE];
	pmc_property_t *properties[MAX_NR_PROPERTIES];
	unsigned int nr_properties;
} hw_subevent_t;

/* Structure that represents a HW event */
typedef struct hw_event {
	int pmcn;                      /* PMC number (if any) */
	char name[NAME_HW_EVENT_SIZE]; /* Event's name */          
	char code[CODE_HW_EVENT_SIZE]; /* Event's code (hex string) */    
	hw_subevent_t *subevents[MAX_NR_SUBEVENTS];  /* List of subevents */ 
	unsigned int nr_subevents;      /* Number of subevents */ 
} hw_event_t;

/* 
 * Structure that holds information about
 * a Performance Monitoring Unit (PMU)
 */
typedef struct {
	char model[NAME_MODEL_SIZE]; /* Processor model string of the PMU */
	unsigned int nr_fixed_pmcs;  /* Number of fixed-function PMCs */
	unsigned int nr_gp_pmcs;     /* Number of general-purpose PMCs */
	hw_event_t *events[MAX_NR_EVENTS]; /* List of HW events supported */
	unsigned int nr_events;      /* Number of HW events supported */
} pmu_info_t;

/* 
 * Structure that holds information about
 * available virtual counters
 */
typedef struct {
	char* name[MAX_VIRTUAL_COUNTERS];  /* Array of virtual counters */
	int nr_virtual_counters;           /* Number of virtual counters available */
} virtual_counter_info_t;

/*
 * Retrieve the information associated with a given PMU
 * 
 * ==Parameters==
 * nr_coretype: PMU number. Asymmetric multicore platforms feature different PMUS, one per core type.
 *              On symmetric systems, use 0 for this parameter.
 * processor_model: String that encodes the processor model whose PMU info we want to
 *                  retrieve. When a NULL value for this parameter is passed, the function
 *                  provides information on the current platform (the model string is
 *                  obtained automatically).
 *
 * The function returns a non-NULL pointer on success, and NULL upon failure. 
 */
pmu_info_t* pmct_get_pmu_info(unsigned int nr_coretype, const char* processor_model);

/*
 * Get the number of PMUs associated with a given processor model.
 * If NULL is passed as a parameter, the function provides the PMU count
 * of the current machine.
 */
int pmct_get_nr_pmus_model(const char* processor_model);

/* Get the number of PMUs detected in the current machine. */
int pmct_get_nr_pmus(void);

/* Print a summary of the properties of a PMU */
void pmct_print_pmu_info(pmu_info_t *pmu_info, int verbose);

/* Print a listing of the events supported by a given PMU */
void pmct_print_event_list(pmu_info_t *pmu_info, int verbose);

/* 
 * This function takes care of translating a mnemonic-based 
 * PMC configuration string into the raw format.
 * Note that we may run out of physical counters to monitor
 * the requested events. In that case, extra experiments may be 
 * allocated. As such, the function returns an array of
 * raw-formatted (feasible) configuration strings.
 *
 * ==Parameters==
 * strcfg (in):  mnemonic-based PMC configuration string.
 *               Event mnemonics or hex-codes may be used to specify events. 
 * nr_coretype (in): PMU id selected to perform the translation.
 * processor_model (in): Processor model string selected to perform the translation. If 
 *                  a NULL value is passed, the function will use the information 
 *                  on the PMU id selected for the current machine.
 * raw_cfgs (out): NULL-terminated array of strings with counter configurations 
 *               in the raw format (e.g., {"pmc0,pmc1","pmc2=0xc0,pmc3=0x2e","NULL"})
 * nr_experiments (out): Number of elements in raw_cfgs
 * used_counter_mask (out): Bitmask indicating PMCs used in raw_cfgs
 * mapping (out): Array with the resulting event-to-PMC mapping information. 
 *                The array must have MAX_PERFORMANCE_COUNTERS elements and its memory must
 *                be allocated by the program that invokes the function.
 *
 * The function returns 0 on success, and a non-zero value upon failure.
 */
int pmct_parse_counter_string(const char *strcfg, int nr_coretype,
                              const char* processor_model,
                              char *raw_cfgs[],
                              unsigned int *nr_experiments,
                              unsigned int *used_counter_mask,
                              counter_mapping_t *mapping);

/* 
 * This function generates a summary indicating which physical
 * performance counter is used to hold the counts for the various 
 * events on each multiplexing experiment. 
 */
void pmct_print_counter_mappings(FILE* fout,
                                 counter_mapping_t* mappings,
                                 unsigned int pmcmask,
                                 unsigned int nr_experiments);

/* 
 * This is a wrapper function for pmct_parse_counter_string(). It accepts
 * an array of PMC configuration strings in the RAW format or in the format 
 * used by pmctrack command-line tool (mnemonic based). 
 * 
 * If the RAW format is used (non-zero "raw" parameter) this function
 * will create a copy of user_cfg_str in the output
 * parameter (raw_cfgs) and returns the number of experiments detected as well
 * as a bitmask with the number of PMCs used.
 * 
 * If the input PMC configuration is specified in the pmctrack default format
 * (raw=0), this function will invoke pmct_parse_counter_string() as many times 
 * as the number of strings in user_cfg_str. In this scenario, the return values
 * have the same meaning than those of pmct_parse_counter_string().
 */
int pmct_parse_pmc_configuration(const char* user_cfg_str[],
                                 int raw,
                                 int nr_coretype,
                                 char* raw_cfgs[],
                                 unsigned int *nr_experiments,
                                 unsigned int *used_counter_mask,
                                 counter_mapping_t *mapping);

/* 
 * Get the number of virtual counters available 
 * (Note: this value depends on the current active 
 *   monitoring module)
 */
int pmct_get_nr_virtual_counters_supported(void);

/* 
 * Retrieve information on virtual counters  
 * (Note: this value depends on the current active 
 *   monitoring module)
 */
virtual_counter_info_t* pmct_get_virtual_counter_info(void);

/* 
 * Print a listing of the virtual counters supported by
 * the current active monitoring module
 */
void pmct_print_virtual_counter_list(void);

/* 
 * Print a listing of the virtual counters specified
 * in the "virtual_mask" bitmask
 * (Note: The behavior of this function depends on the current active 
 *   monitoring module)
 */
void pmct_print_selected_virtual_counters(FILE* fout, unsigned int virtual_mask);

/* 
 * This function takes care of translating
 * a mnemonic-based virtual-counter configuration string 
 * into a raw-formatted configuration string (kernel format).
 *
 * ==Parameters==
 * virtcfg (in): virtual counter string configuration.
 * virtual_mask (out): Bitmask indicating PMCs used in raw_virtcfg
 * nr_virtual_counters (out): Number of virtual counters used by the configuration
 * mnemonics_used (out): If mnemonics were used in virtcfg a non-zero value
 *                       will be returned in this parameter
 * raw_virtcfg (out): Resulting virtual-counter configuration string  
 *                    in the raw format (e.g., virt0,virt1). The function
 *                    takes care of allocating memory for this string.
 */
int pmct_parse_vcounter_config(const char* virtcfg,
                               unsigned int *virtual_mask,
                               unsigned int* nr_virtual_counters,
                               int* mnemonics_used,
                               char** raw_virtcfg);
#endif
