#ifndef PMC_INTEL_RDT_H
#define PMC_INTEL_RDT_H

#include <linux/types.h>
#include <pmc/pmu_config.h> //For cpuid_regs_t
#include <pmc/mc_experiments.h> /* For uint_t */

/* API Intel CMT */
#define DISABLE_RMID 0  	/* RMID reserved for the OS */

#define MSR_IA32_PQR_ASSOC	0xc8f
#define MSR_IA32_QM_CTR		0xc8e
#define MSR_IA32_QM_EVTSEL	0xc8d
#define RMID_VAL_ERROR		(1ULL << 63)
#define RMID_VAL_UNAVAIL	(1ULL << 62)
#define RMID_MAX_LLCS		32	/* Max number of processor "chips" in the machine */
#define CMT_MAX_EVENTS		3 	/* L3_USAGE, L3_TOTAL_BW, L3_LOCAL_BW */
#define L3_OCCUPANCY_EVENT_ID	0x01
#define L3_TOTAL_BW_EVENT_ID	0x02
#define L3_LOCAL_BW_EVENT_ID	0x03

/*
 * Memory bandwith cumulative counters
 * are 24-bit wide on Intel and 44-bit wide on AMD !!
 * That's the reason for this macro.
 */
#define MAX_CMT_COUNT	((1ULL<<24ULL)-1ULL)
#define MAX_CMT_COUNT_AMD	((1ULL<<44ULL)-1ULL)

#define IA32_LLC_MASK_0 0xc90	/* For COS 0 */
#define IA32_LLC_MASK_1 0xc91  /* For COS 1  (and so on) */
#define IA32_L2_QOS_EXT_BW_THRTL_0 0xd50 /* For COS 0 */

typedef struct {
	/* Fields to store CMT and MBM global parameters */
	unsigned int total_rmids;	/* Number of available RMIDs */
	unsigned int upscaling_factor;		/* Factor to transform read values into byte counts */
	unsigned int event_l3_occupancy;
	unsigned int event_l3_total_bw;
	unsigned int event_l3_local_bw;
	uint64_t	 mbm_max_count;
} intel_cmt_support_t;

/* CAT SPECIFIC stuff */
typedef struct {
	unsigned int cat_nr_cos_available;
	unsigned int cat_cbm_length;
	unsigned int cat_cbm_mask;
} intel_cat_support_t;


typedef struct {
	unsigned int mba_is_supported;
	unsigned int mba_nr_cos_available;
	unsigned int mba_is_linear;
	unsigned int mba_max_throtling;
} intel_mba_support_t;

typedef struct {
	unsigned int rmid;
	uint64_t last_llc_utilization[RMID_MAX_LLCS][CMT_MAX_EVENTS];
	uint64_t last_cmt_value[RMID_MAX_LLCS][CMT_MAX_EVENTS];
	unsigned int cos_id;
} intel_cmt_thread_struct_t;


/* Structure to expose an RDT event for a specific rmid */
typedef struct {
	unsigned int rmid;
	uint64_t value;
} intel_rdt_event_t;

/* Supported RMID allocation policies */
typedef enum {
	RMID_FIFO,
	RMID_LIFO,
	RMID_RANDOM,
	NR_RMID_ALLOC_POLICIES
}
rmid_allocation_policy_t;

extern const char* rmid_allocation_policy_str[NR_RMID_ALLOC_POLICIES];

static inline void initialize_cmt_thread_struct(intel_cmt_thread_struct_t* data)
{
	int i,j;
	/* Default 0: just in case */
	data->cos_id=0;

	for (i=0; i<RMID_MAX_LLCS; i++) {
		for(j=0; j<CMT_MAX_EVENTS; j++) {
			data->last_llc_utilization[i][j]=0;
			data->last_cmt_value[i][j]=0;
		}
	}

}


int intel_cmt_probe(void);
int amd_qos_probe(void);

int intel_cmt_initialize(intel_cmt_support_t* cmt_support);
int intel_cat_initialize(intel_cat_support_t* cat_support);
int intel_mba_initialize(intel_mba_support_t* mba_support);

int intel_cmt_release(intel_cmt_support_t* cmt_support);
int intel_cat_release(intel_cat_support_t* cat_support);
int intel_mba_release(intel_mba_support_t* mba_support);


int intel_cat_print_capacity_bitmasks(char* str, intel_cat_support_t* cat_support);

int intel_cat_set_capacity_bitmask(intel_cat_support_t* cat_support, unsigned int idx, unsigned int mask);


int intel_cat_print_capacity_bitmasks_cpu(char* str, intel_cat_support_t* cat_support, int cpu);

int intel_cat_set_capacity_bitmask_cpu(intel_cat_support_t* cat_support, unsigned int idx, unsigned int mask, int cpu);



int intel_mba_print_delay_values(char* str, intel_mba_support_t* mba_support);
int intel_mba_set_delay_values(intel_mba_support_t* mba_support, unsigned int idx, unsigned int val);


void intel_cmt_update_supported_events(intel_cmt_support_t* cmt_support,intel_cmt_thread_struct_t* data, unsigned int llc_id);


/*
 * Return array with per RMID event counts
 * the length of the array and the aggregate
 * system-wide count
 */
intel_rdt_event_t* intel_rdt_syswide_read_localbw(intel_cmt_support_t* cmt_support, unsigned int llc_id, unsigned int* count, uint64_t* aggregate_count);

/* Remove a unused RMID from the free pool and return it. */
unsigned int get_rmid(void);

/*
 * Decrease the reference counter of a given RMID.
 * If the reference counter reaches 0, put the RMID back in
 * the free RMID pool.
 */
void put_rmid(uint_t rmid);

/* Increase the reference counter of a given RMID */
void use_rmid(unsigned int rmid);


/* Functions to select/retrieve the current allocation policy for Intel CMT */
rmid_allocation_policy_t get_cmt_policy(void);
void set_cmt_policy(rmid_allocation_policy_t new_policy);



/** Low level functions to access CMT/MBM MSRs **/
static inline u64 __rmid_read(unsigned long rmid, unsigned int event)
{
	u64 val;

	/* Select desired event by writting to MSR_IA32_QM_EVTSEL
	 * and then read the event count
	*/
	wrmsr(MSR_IA32_QM_EVTSEL, event, rmid);
	rdmsrl(MSR_IA32_QM_CTR, val);
	return val;
}

static inline void __set_rmid_and_cos(unsigned int rmid, unsigned int cosid)
{
	wrmsr(MSR_IA32_PQR_ASSOC,rmid,cosid);
}

static inline void __unset_rmid(void)
{
	wrmsr(MSR_IA32_PQR_ASSOC,DISABLE_RMID,0);
}

int get_cat_cbm(unsigned long* mask, intel_cat_support_t* cat_support, unsigned int clos, int cpu);

int get_mba_setting(unsigned long* val, intel_mba_support_t* mba_support, unsigned int clos, int cpu);


#endif
